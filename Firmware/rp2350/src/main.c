#include "accel.h"
#include "content.h"
#include "flash_disk.h"
#include "int_test.h"
#include "leds.h"
#include "log.h"
#include "report.h"
#include "sleep.h"
#include "storage.h"
#include "tracker.h"
#include "usb_app.h"

#include "bsp/board_api.h"
#include "hardware/powman.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static Loaded g_loaded;
static Settings g_settings;
static Frames g_frames;
static Tracker g_track;

static void warn_cb(const char *msg) { pov_log("POV> PROBLEM: %s", msg); }

static double pov_now(void) { return (double)time_us_64() / 1000000.0; }

static void wait_ms(int ms) {
    absolute_time_t until = make_timeout_time_ms(ms);
    while (!time_reached(until)) {
        usb_task();
        pov_log_flush();
    }
}

static void show_line(const Frames *fr) {
    char names[40] = "";
    char cols[40] = "";
    for (int i = 0; i < fr->count; i++) {
        if (i) {
            strncat(names, ", ", sizeof names - strlen(names) - 1);
            strncat(cols, ", ", sizeof cols - strlen(cols) - 1);
        }
        strncat(names, fr->name[i], sizeof names - strlen(names) - 1);
        char bit[16];
        snprintf(bit, sizeof bit, "%d columns", fr->n[i]);
        strncat(cols, bit, sizeof cols - strlen(cols) - 1);
    }
    pov_log("POV> showing: %s  (%s)", names, cols);
}

static void apply_files(void) {
    g_settings.warn = warn_cb;
    g_settings.warnings = 0;
    settings_defaults(&g_settings);
    g_settings.warn = warn_cb;
    if (g_loaded.have_settings) settings_parse(&g_settings, g_loaded.settings);
    else pov_log("POV> no settings.txt found, using defaults");
    const char *msg = g_loaded.have_message ? g_loaded.message : "";
    const uint8_t *bmp = g_loaded.have_bmp ? g_loaded.bmp : NULL;
    frames_build(&g_settings, msg, bmp, g_loaded.bmp_len, &g_frames);
    show_line(&g_frames);
}

static void measure_gravity(double *g_out, double *bias_out) {
    const double nominal = 0.008;
    double t0 = pov_now();
    double sx = 0, sy = 0, sz = 0, sxx = 0, syy = 0, szz = 0;
    int cnt = 0;
    while (pov_now() - t0 < 0.5) {
        int x, y, z;
        accel_xyz_counts(&x, &y, &z);
        sx += x; sy += y; sz += z;
        sxx += (double)x * x; syy += (double)y * y; szz += (double)z * z;
        cnt++;
        usb_task();
        pov_log_flush();
    }
    if (cnt < 1) cnt = 1;
    double mx = sx / cnt, my = sy / cnt, mz = sz / cnt;
    double norm = sqrt(mx * mx + my * my + mz * mz);
    double jitter = sqrt(fmax(0.0, sxx / cnt - mx * mx) + fmax(0.0, syy / cnt - my * my) + fmax(0.0, szz / cnt - mz * mz));
    double g = nominal;
    const char *note = "nominal";
    if (norm > 5 && jitter < 0.05 * norm) {
        double g_meas = 1.0 / norm;
        if (g_meas > 0.0003 && g_meas < 0.05) {
            g = g_meas;
            note = "measured from gravity";
        } else {
            note = "NOMINAL (gravity reading looked wrong)";
        }
    } else if (norm > 5) {
        note = "NOMINAL (the board was moving at startup - keep it still after saving)";
    }
    *g_out = g;
    *bias_out = g_settings.sign * mx * g;
    pov_log("POV> ready (v9). range +/-16 g, %.2f mg/count (%s). resting x %+.3f g. Wave it briskly, left and right.",
            g * 1000.0, note, *bias_out);
}

int main(void) {
    board_init();
    leds_init();
    accel_init();

#ifdef POV_BENCH
    usb_init();
    if (!sleep_woke()) {
        pov_log("POV> bench: pstate 5s");
        wait_ms(2500);
        usb_disconnect();
        sleep_ms(150);
        int rc = sleep_bench_ms(5000);
        pov_log("POV> PROBLEM: bench sleep returned %d", rc);
        wait_ms(2000);
    } else {
        pov_log("POV> bench woke from pstate");
        wait_ms(2000);
    }
    reset_usb_boot(0, 0);
#endif

    int who = accel_who();
    if (who != 0x11) pov_log("POV> PROBLEM: accelerometer who=0x%02X (expected 11)", who);

    memset(&g_settings, 0, sizeof g_settings);
    g_settings.warn = warn_cb;
    settings_defaults(&g_settings);
    if (!storage_init() || !storage_load(&g_loaded)) {
        frames_build(&g_settings, "HELLO", NULL, 0, &g_frames);
        pov_log("POV> PROBLEM: the drive could not be read - showing HELLO");
    } else {
        apply_files();
    }

    usb_init();
    if (g_settings.int_test) int_test_run();

    int instant = sleep_was_instant();
    int woke = sleep_woke();
    double g = 0.008, bias = 0;
    measure_gravity(&g, &bias);
    memset(&g_track, 0, sizeof g_track);
    tracker_init(&g_track, &g_settings, &g_frames, pov_now(), bias, g);

    pov_log("POV> boot pwrup 0x%02lx had_pd %d woke %d instant %d",
            (unsigned long)(powman_hw->last_swcore_pwrup & 0x7fu),
            (powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS) ? 1 : 0,
            woke, instant);
    if (instant)
        pov_log("POV> woke very soon after sleep");
    else if (woke)
        pov_log("POV> woke on INT1 (GPIO23)");
    if (g_settings.sleep_on) {
        wake_int_setup(g_settings.wake_g);
        pov_log("POV> sleep ON (pstate): lights out after %.1f s still, wake on INT1 high at %.3f g. A computer with the drive open keeps the chip awake.",
                g_settings.sleep_s, wake_g_actual(g_settings.wake_g));
    }

    double t_rd = pov_now();
    double sleep_fail_until = -1.0;
    int lights_out = 0;
    int shown = -1;

    while (1) {
        usb_task();
        pov_log_flush();
        double now = pov_now();
        if (now - t_rd >= 0.0025) {
            double xr = accel_x_counts() * g_track.g_per_count;
            now = pov_now();
            tracker_on_sample(&g_track, now, xr);
            t_rd = now;
        }
        now = pov_now();
        int mask = tracker_mask(&g_track, now, g_settings.warnings);
        if (g_track.log_ready) {
            report_save_log(&g_track);
            if (g_settings.debug) report_print(&g_track, &g_settings, &g_frames);
            g_track.log_ready = 0;
        }
        int idle = g_settings.sleep_on && !g_track.locked && !g_track.sess_active &&
                   (now - g_track.last_motion > g_settings.sleep_s);
        if (idle && now >= sleep_fail_until) {
            if (usb_host_connected()) {
                lights_out = 1;
            } else {
                pov_log("POV> powering the chip off (powman) after %.1f s idle. Wake = INT1 (GPIO23) high, threshold %.3f g",
                        g_settings.sleep_s, wake_g_actual(g_settings.wake_g));
                pov_log_flush();
                int rc = sleep_enter(g_settings.wake_g);
                if (rc == 0) {
                    pov_log("POV> woke on INT1 (GPIO23), core was still on");
                    memset(&g_track, 0, sizeof g_track);
                    tracker_init(&g_track, &g_settings, &g_frames, pov_now(), bias, g);
                    if (g_settings.sleep_on) wake_int_setup(g_settings.wake_g);
                } else {
                    pov_log("POV> PROBLEM: sleep did not start (%d). Will retry in 10 s", rc);
                    sleep_fail_until = pov_now() + 10.0;
                }
                lights_out = 0;
                t_rd = pov_now();
                now = t_rd;
            }
        } else if (!idle) {
            lights_out = 0;
        }
        if (lights_out) mask = 0;
        if (mask != shown) {
            leds_put((uint16_t)mask);
            shown = mask;
        }
        if (flash_disk_host_idle(500)) {
            pov_log("POV> files changed, loading them");
            if (storage_load(&g_loaded)) apply_files();
            measure_gravity(&g, &bias);
            memset(&g_track, 0, sizeof g_track);
            tracker_init(&g_track, &g_settings, &g_frames, pov_now(), bias, g);
            if (g_settings.sleep_on) wake_int_setup(g_settings.wake_g);
            t_rd = pov_now();
            lights_out = 0;
            shown = -1;
        }
        if (!g_track.locked) sleep_us(200);
    }
}
