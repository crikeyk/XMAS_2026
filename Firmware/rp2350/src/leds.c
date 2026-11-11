#include "leds.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

/* LED i cathode is GP(18-i). Low = on. GP25 is the 595 shift clock and must
 * stay low once the anodes have been latched high, or an SPI clock would
 * shift them. GP22 is the latch, and later becomes the accelerometer clock. */
#define PIN_SER  19
#define PIN_SHCP 25
#define PIN_STCP 22

static uint16_t shown;

void leds_init(void) {
    gpio_init(PIN_SER);
    gpio_init(PIN_SHCP);
    gpio_init(PIN_STCP);
    gpio_set_dir(PIN_SER, GPIO_OUT);
    gpio_set_dir(PIN_SHCP, GPIO_OUT);
    gpio_set_dir(PIN_STCP, GPIO_OUT);
    gpio_put(PIN_SER, 1);
    gpio_put(PIN_SHCP, 0);
    gpio_put(PIN_STCP, 0);
    for (int i = 0; i < 16; i++) {
        gpio_put(PIN_SHCP, 0);
        busy_wait_us(1);
        gpio_put(PIN_SHCP, 1);
        busy_wait_us(1);
    }
    gpio_put(PIN_STCP, 1);
    busy_wait_us(1);
    gpio_put(PIN_STCP, 0);
    gpio_put(PIN_SHCP, 0);
    for (int i = 0; i < 16; i++) {
        int pin = 18 - i;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 1);
    }
    shown = 0;
}

void leds_put(uint16_t mask) {
    uint16_t dif = (uint16_t)(mask ^ shown);
    for (int i = 0; i < 16; i++) {
        if (dif & (1u << i))
            gpio_put(18 - i, (mask & (1u << i)) ? 0 : 1);
    }
    shown = mask;
}

void leds_park(void) {
    leds_put(0);
    for (int i = 0; i < 16; i++) gpio_put(18 - i, 1);
    gpio_put(PIN_SHCP, 0);
    gpio_set_dir(PIN_SHCP, GPIO_OUT);
}
