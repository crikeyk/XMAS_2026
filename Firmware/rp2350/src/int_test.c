#include "int_test.h"

#include "accel.h"
#include "leds.h"
#include "log.h"
#include "usb_app.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIN1 23
#define PIN2 21
#define THS 2
#define XYZ_THR 40
#define SLEEP_TIMEOUT 60

static void wait_ms(int ms) {
    absolute_time_t until = make_timeout_time_ms(ms);
    while (!time_reached(until)) {
        usb_task();
        pov_log_flush();
    }
}

static int level(int pin) { return gpio_get((uint)pin) ? 1 : 0; }

static void probe(int pin, int out[3]) {
    gpio_disable_pulls((uint)pin);
    wait_ms(2);
    out[0] = level(pin);
    gpio_set_pulls((uint)pin, true, false);
    wait_ms(2);
    out[1] = level(pin);
    gpio_set_pulls((uint)pin, false, true);
    wait_ms(2);
    out[2] = level(pin);
    gpio_disable_pulls((uint)pin);
}

static int driven(const int t[3], int *lv) {
    if (t[1] == t[2]) { *lv = t[1]; return 1; }
    return 0;
}

static void describe(const int t[3], char *buf, int cap) {
    int lv = 0;
    if (driven(t, &lv))
        snprintf(buf, (size_t)cap, "%s, driven (none=%d up=%d down=%d)", lv ? "HIGH" : "LOW", t[0], t[1], t[2]);
    else
        snprintf(buf, (size_t)cap, "RELEASED/hi-Z (none=%d up=%d down=%d)", t[0], t[1], t[2]);
}

static void src_text(int v, char *buf, int cap) {
    char bits[32] = "";
    const char *names[] = {"XL", "XH", "YL", "YH", "ZL", "ZH", "IA"};
    for (int b = 0; b < 7; b++) if (v & (1 << b)) {
        if (bits[0]) strncat(bits, " ", sizeof bits - strlen(bits) - 1);
        strncat(bits, names[b], sizeof bits - strlen(bits) - 1);
    }
    if (!bits[0]) memcpy(bits, "none", 5);
    snprintf(buf, (size_t)cap, "0x%02X [%s]", v, bits);
}

static void configure(int active_low) {
    accel_write(0x22, 0x00);
    accel_write(0x25, 0x00);
    accel_write(0x20, 0x77);
    accel_write(0x23, 0x88 | (3 << 4));
    accel_write(0x21, 0x03);
    accel_write(0x24, 0x0A);
    accel_write(0x32, THS); accel_write(0x33, 1); accel_write(0x30, 0x2A);
    accel_write(0x36, THS); accel_write(0x37, 1); accel_write(0x34, 0x2A);
    accel_write(0x22, 0x40);
    accel_write(0x25, (uint8_t)(0x60 | (active_low ? 0x02 : 0x00)));
}

static int arm_both(void) {
    for (int i = 0; i < 10; i++) {
        accel_read(0x26);
        wait_ms(30);
        accel_read(0x31);
        accel_read(0x35);
        wait_ms(5);
        if (!(accel_read(0x31) & 0x40) && !(accel_read(0x35) & 0x40)) return 1;
    }
    return 0;
}

static void trial(const char *label, int active_low, char results[][140], int *nres) {
    const char *names[2] = {"INT1 (GPIO23)", "INT2 (GPIO21)"};
    pov_log("INT> ===== TRIAL %s: H_LACTIVE=%d (datasheet says active %s) =====",
            label, active_low ? 1 : 0, active_low ? "LOW" : "HIGH");
    configure(active_low);
    pov_log("INT> hold the board STILL for the next 2 seconds");
    wait_ms(1000);
    if (!arm_both()) pov_log("INT> note: the interrupt flag (IA) would not clear at rest - continuing anyway");
    wait_ms(200);

    int b_lv[2] = {level(PIN1), level(PIN2)};
    int b_pr[2][3];
    probe(PIN1, b_pr[0]);
    probe(PIN2, b_pr[1]);
    pov_log("INT> BEFORE (still): INT1=%d INT2=%d", b_lv[0], b_lv[1]);
    char desc[96], s1[40], s2[40];
    describe(b_pr[0], desc, sizeof desc);
    pov_log("INT>   %s -> %s", names[0], desc);
    describe(b_pr[1], desc, sizeof desc);
    pov_log("INT>   %s -> %s", names[1], desc);
    src_text(accel_read(0x31), s1, sizeof s1);
    src_text(accel_read(0x35), s2, sizeof s2);
    pov_log("INT>   INT1_SOURCE=%s INT2_SOURCE=%s", s1, s2);

    int pull_up[2] = {0, 0};
    for (int i = 0; i < 2; i++) {
        int lv;
        if (!driven(b_pr[i], &lv)) {
            pull_up[i] = 1;
            pov_log("INT>   note: %s is released at idle, so it is sampled with a pull-up while asleep", names[i]);
        }
    }
    gpio_set_pulls(PIN1, pull_up[0], 0);
    gpio_set_pulls(PIN2, pull_up[1], 0);
    wait_ms(5);
    int base[2] = {level(PIN1), level(PIN2)};
    int x0, y0, z0;
    accel_xyz_counts(&x0, &y0, &z0);

    leds_put(0);
    pov_log("INT> SLEEPING (LEDs off, sampling both pins every 20 ms). SHAKE the board to wake it. Timeout %d s.", SLEEP_TIMEOUT);
    double t0 = (double)time_us_64() / 1e6;
    int saw_move = 0;
    double t_move = 0;
    const char *why = "timeout";
    double last_hb = t0;
    while (1) {
        wait_ms(20);
        double now = (double)time_us_64() / 1e6;
        int lv1 = level(PIN1), lv2 = level(PIN2);
        int x, y, z;
        accel_xyz_counts(&x, &y, &z);
        if (!saw_move && (abs(x - x0) > XYZ_THR || abs(y - y0) > XYZ_THR || abs(z - z0) > XYZ_THR)) {
            saw_move = 1;
            t_move = now;
            pov_log("INT>   t=%.2fs: motion seen in x/y/z (%d/%d/%d counts)", now - t0, x - x0, y - y0, z - z0);
        }
        if (lv1 != base[0] || lv2 != base[1]) {
            pov_log("INT>   t=%.2fs: PIN CHANGE  INT1 %d->%d  INT2 %d->%d", now - t0, base[0], lv1, base[1], lv2);
            why = "pin change";
            break;
        }
        if (saw_move && now - t_move > 0.5) {
            why = "motion but NO pin change within 0.5 s";
            break;
        }
        if (now - last_hb > 2.0) {
            last_hb = now;
            pov_log("INT>   t=%.0fs still asleep: INT1=%d INT2=%d", now - t0, lv1, lv2);
        }
        if (now - t0 > SLEEP_TIMEOUT) break;
    }
    pov_log("INT> WAKE trigger: %s", why);

    double tw = (double)time_us_64() / 1e6;
    int last1 = level(PIN1), last2 = level(PIN2);
    int any = 0;
    while ((double)time_us_64() / 1e6 - tw < 0.15) {
        usb_task();
        int a = level(PIN1), b = level(PIN2);
        if (a != last1 || b != last2) {
            pov_log("INT>   +%.1f ms: INT1=%d INT2=%d", ((double)time_us_64() / 1e6 - tw) * 1000.0, a, b);
            last1 = a; last2 = b; any = 1;
        }
    }
    if (!any) pov_log("INT>   no further pin changes in the 150 ms after the trigger");

    int w_lv[2] = {level(PIN1), level(PIN2)};
    int w_pr[2][3];
    probe(PIN1, w_pr[0]);
    probe(PIN2, w_pr[1]);
    gpio_set_pulls(PIN1, pull_up[0], 0);
    gpio_set_pulls(PIN2, pull_up[1], 0);
    leds_put(0xFFFF);
    wait_ms(100);
    leds_put(0);
    int src1 = accel_read(0x31);
    int src2 = accel_read(0x35);
    double t_clr = (double)time_us_64() / 1e6;
    pov_log("INT> WAKE (latch still set, before clearing): INT1=%d INT2=%d", w_lv[0], w_lv[1]);
    describe(w_pr[0], desc, sizeof desc);
    pov_log("INT>   %s -> %s", names[0], desc);
    describe(w_pr[1], desc, sizeof desc);
    pov_log("INT>   %s -> %s", names[1], desc);
    src_text(src1, s1, sizeof s1);
    src_text(src2, s2, sizeof s2);
    pov_log("INT>   INT1_SOURCE=%s INT2_SOURCE=%s (read now = latch cleared)", s1, s2);

    pov_log("INT> AFTER clearing - keep the board still now");
    int marks[] = {0, 2, 10, 50, 200, 1000};
    for (unsigned i = 0; i < sizeof marks / sizeof marks[0]; i++) {
        while (((double)time_us_64() / 1e6 - t_clr) * 1000.0 < marks[i]) usb_task();
        pov_log("INT>   +%4d ms: INT1=%d INT2=%d", marks[i], level(PIN1), level(PIN2));
    }
    src_text(accel_read(0x31), s1, sizeof s1);
    src_text(accel_read(0x35), s2, sizeof s2);
    pov_log("INT>   INT1_SOURCE=%s INT2_SOURCE=%s", s1, s2);
    int a_pr[2][3];
    probe(PIN1, a_pr[0]);
    probe(PIN2, a_pr[1]);
    char d0[96], d1[96];
    describe(a_pr[0], d0, sizeof d0);
    describe(a_pr[1], d1, sizeof d1);
    pov_log("INT>   settled: %s -> %s | %s -> %s", names[0], d0, names[1], d1);

    for (int i = 0; i < 2 && *nres < 4; i++) {
        int bl = 0, wl = 0;
        int bd = driven(b_pr[i], &bl);
        int wd = driven(w_pr[i], &wl);
        char *dst = results[(*nres)++];
        if (b_pr[i][1] == w_pr[i][1] && b_pr[i][2] == w_pr[i][2]) {
            char bdsc[80], wdsc[80];
            describe(b_pr[i], bdsc, sizeof bdsc);
            describe(w_pr[i], wdsc, sizeof wdsc);
            snprintf(dst, 140, "TRIAL %s (H_LACTIVE=%d) %s: NO CHANGE between before and wake (before %s | wake %s)",
                     label, active_low ? 1 : 0, names[i], bdsc, wdsc);
        } else if (bd && wd) {
            snprintf(dst, 140, "TRIAL %s (H_LACTIVE=%d) %s: push-pull style. idle %s, active %s  => ACTIVE %s",
                     label, active_low ? 1 : 0, names[i], bl ? "HIGH" : "LOW", wl ? "HIGH" : "LOW", wl ? "HIGH" : "LOW");
        } else if (!bd && wd) {
            snprintf(dst, 140, "TRIAL %s (H_LACTIVE=%d) %s: open-drain style. idle RELEASED, active driven %s  => ACTIVE %s (needs a pull-%s)",
                     label, active_low ? 1 : 0, names[i], wl ? "HIGH" : "LOW", wl ? "HIGH" : "LOW", wl ? "down" : "up");
        } else {
            char bdsc[80], wdsc[80];
            describe(b_pr[i], bdsc, sizeof bdsc);
            describe(w_pr[i], wdsc, sizeof wdsc);
            snprintf(dst, 140, "TRIAL %s (H_LACTIVE=%d) %s: idle %s | active %s",
                     label, active_low ? 1 : 0, names[i], bdsc, wdsc);
        }
    }
}

void int_test_run(void) {
    gpio_init(PIN1);
    gpio_init(PIN2);
    gpio_set_dir(PIN1, GPIO_IN);
    gpio_set_dir(PIN2, GPIO_IN);
    gpio_disable_pulls(PIN1);
    gpio_disable_pulls(PIN2);
    char results[4][140];
    int nres = 0;
    trial("A", 0, results, &nres);
    pov_log("INT> trial B in 4 seconds - put the board down and let it settle");
    wait_ms(4000);
    trial("B", 1, results, &nres);
    accel_write(0x22, 0x00);
    accel_write(0x25, 0x00);
    accel_write(0x24, 0x00);
    accel_write(0x21, 0x00);
    accel_write(0x30, 0x00);
    accel_write(0x34, 0x00);
    accel_read(0x31);
    accel_read(0x35);
    leds_put(0);
    pov_log("INT> ===== SUMMARY =====");
    for (int i = 0; i < nres; i++) pov_log("INT> %s", results[i]);
    pov_log("INT> ===== end of INT test =====");
    pov_log("INT> test finished. Set INT_TEST = no in settings.txt, or it runs at every boot.");
}
