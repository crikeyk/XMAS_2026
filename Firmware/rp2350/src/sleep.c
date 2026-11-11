#include "sleep.h"

#include "accel.h"
#include "leds.h"
#include "log.h"
#include "usb_app.h"

#include "hardware/gpio.h"
#include "hardware/powman.h"
#include "pico/aon_timer.h"
#include "pico/low_power.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define PIN_INT1 23
#define TAG 0x50534C50u /* 'PSLP' */

volatile int g_woke_from_pstate;

static void pstate_resume(pstate_bitset_t *pstate) {
    (void)pstate;
    g_woke_from_pstate = 1;
}

static uint32_t aon_ms(void) {
    struct timespec ts;
    if (!aon_timer_is_running()) {
        struct timespec zero = {0, 0};
        aon_timer_start(&zero);
    }
    if (!aon_timer_get_time(&ts)) return 0;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull);
}

int wake_threshold(double wake_g) {
    int ths = (int)(wake_g * 1000.0 / (8.0 * 16.0) + 0.5);
    if (ths < 1) ths = 1;
    if (ths > 127) ths = 127;
    return ths;
}

double wake_g_actual(double wake_g) {
    return wake_threshold(wake_g) * 8.0 * 16.0 / 1000.0;
}

void wake_int_setup(double wake_g) {
    int ths = wake_threshold(wake_g);
    accel_write(0x21, 0x01);          /* high-pass into the comparator only */
    accel_write(0x24, 0x08);          /* latch INT1 until INT1_SOURCE is read */
    accel_write(0x25, 0x00);          /* active high */
    accel_write(0x32, (uint8_t)ths);
    accel_write(0x33, 1);             /* 1 sample = 2.5 ms at 400 Hz, same as v8 */
    accel_write(0x30, 0x2A);          /* X or Y or Z high */
    accel_write(0x22, 0x40);          /* route it to the INT1 pin last */
}

static void wait_ms(int ms) {
    absolute_time_t until = make_timeout_time_ms(ms);
    while (!time_reached(until)) usb_task();
}

int wake_arm(void) {
    /* Clearing the latch makes this pin blip high again about 50 ms later.
     * A level wake treats that blip as "already awake", the power-down is
     * refused, and the core keeps running at about 10 mA. */
    for (int i = 0; i < 8; i++) {
        accel_read(0x26);
        wait_ms(30);
        accel_read(0x31);
        wait_ms(80);
        if (!accel_int1_high()) {
            wait_ms(40);
            if (!accel_int1_high()) return 1;
        }
    }
    return 0;
}

static void hold_out(uint pin, int high) {
    gpio_put(pin, high ? 1 : 0);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_set_function(pin, GPIO_FUNC_SIO);
}

static void hold_pins(void) {
    uint32_t keep = 0;
    for (int pin = 3; pin <= 19; pin++) keep |= 1u << pin;
    keep |= (1u << 22) | (1u << 23) | (1u << 24) | (1u << 25);
    low_power_set_pins_low_leakage_exclude_mask(keep);
    for (int pin = 3; pin <= 18; pin++) hold_out((uint)pin, 1);
    /* Drive MOSI / SCK / CS / SHCP as outputs without gpio_init(). That
     * function goes input first and a low CS clocks garbage into the
     * accelerometer. SPI mode 3 idle is SCK high, CS high. SHCP stays low
     * so the 74HC595s do not shift. */
    hold_out(19, 0);
    hold_out(22, 1);
    hold_out(24, 1);
    hold_out(25, 0);
    gpio_set_input_enabled(PIN_INT1, true);
    gpio_disable_pulls(PIN_INT1);
}

static void mark_sleep_time(void) {
    powman_hw->scratch[0] = TAG;
    powman_hw->scratch[1] = aon_ms();
}

static void usb_off(void) {
    usb_disconnect();
    absolute_time_t until = make_timeout_time_ms(20);
    while (!time_reached(until)) usb_task();
    tud_deinit(0);
}

static int enter_pstate(pstate_bitset_t *ps) {
    mark_sleep_time();
    hold_pins();
    if (accel_int1_high()) return -2;

    usb_off();
    /* A leftover alarm from the AON clock would make powman refuse to
     * isolate the core. GPIO23 is the only wake we want. */
    powman_disable_alarm_wakeup();
    powman_disable_all_wakeups();
    if (accel_int1_high()) return -2;

    /* Same path as the 5 s USB-gap bench: PRIMASK, no GPIO IRQ, no
     * SLEEPDEEP, infinite WFI. The GPIO-IRQ loop we had here was ordinary
     * sleep at about 1.5 mA; powman never cut the switched-core domain.
     * A successful power-down reboots and never returns. */
    int rc = low_power_pstate_until_gpio_pin_state(PIN_INT1, false, true, ps, pstate_resume);
    if (rc) {
        rc = low_power_pstate_until_gpio_pin_state(PIN_INT1, false, true, NULL, pstate_resume);
    }
    return rc;
}

int sleep_enter(double wake_g) {
    leds_put(0);
    leds_park();
    accel_pins_idle();
    wake_int_setup(wake_g);
    accel_low_power_wake();
    wait_ms(250);
    pov_log("POV> sleep arm who=0x%02x ctrl1=0x%02x cfg=0x%02x int1=%d",
            accel_who(), accel_read(0x20), accel_read(0x30), accel_int1_high());
    pov_log_flush();
    if (!wake_arm()) {
        pov_log("POV> PROBLEM: INT1 (GPIO23) stuck active; not sleeping");
        accel_init();
        leds_init();
        usb_init();
        return -1;
    }
    if (accel_int1_high()) {
        accel_init();
        leds_init();
        usb_init();
        return -2;
    }
    pstate_bitset_t ps = pstate_bitset_none();
    int rc = enter_pstate(&ps);
    /* Power-down reboots. Getting here means powman refused, so restore USB. */
    accel_init();
    leds_init();
    usb_init();
    return rc;
}

#ifdef POV_BENCH
int sleep_bench_ms(int ms) {
    leds_park();
    accel_pins_idle();
    hold_pins();
    pstate_bitset_t ps = pstate_bitset_none();
    mark_sleep_time();
    int rc = low_power_pstate_for_ms((uint32_t)ms, &ps, pstate_resume);
    if (rc) rc = low_power_pstate_for_ms((uint32_t)ms, NULL, pstate_resume);
    return rc;
}
#endif

int sleep_woke(void) {
    /* HAD_SWCORE_PD stays set after the first power-down, including across the
     * USB bootloader, so it cannot be used as "this boot woke". The resume
     * function runs only when this boot really came back from a P-state. */
    return g_woke_from_pstate ? 1 : 0;
}

int sleep_was_instant(void) {
    if (powman_hw->scratch[0] != TAG) return 0;
    uint32_t then = powman_hw->scratch[1];
    uint32_t now = aon_ms();
    powman_hw->scratch[0] = 0;
    return sleep_woke() && now >= then && (now - then) < 300;
}
