#include "accel.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"

#define PIN_MOSI 19
#define PIN_MISO 20
#define PIN_SCK  22
#define PIN_CS   24
#define PIN_INT1 23

static int s16_count(uint8_t lo, uint8_t hi) {
    int v = (hi << 8) | lo;
    if (v & 0x8000) v -= 65536;
    return v >> 4;
}

static void cs_low(void) { gpio_put(PIN_CS, 0); }
static void cs_high(void) { gpio_put(PIN_CS, 1); }

void accel_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { (uint8_t)(reg & 0x3F), val };
    cs_low();
    spi_write_blocking(spi0, buf, 2);
    cs_high();
}

uint8_t accel_read(uint8_t reg) {
    uint8_t cmd = (uint8_t)(0x80 | reg);
    uint8_t val = 0;
    cs_low();
    spi_write_blocking(spi0, &cmd, 1);
    spi_read_blocking(spi0, 0, &val, 1);
    cs_high();
    return val;
}

static void read_raw(uint8_t *buf, int n) {
    uint8_t cmd = (uint8_t)(0x28 | 0xC0);
    cs_low();
    spi_write_blocking(spi0, &cmd, 1);
    spi_read_blocking(spi0, 0, buf, (size_t)n);
    cs_high();
}

int accel_x_counts(void) {
    uint8_t b[2];
    read_raw(b, 2);
    return s16_count(b[0], b[1]);
}

void accel_xyz_counts(int *x, int *y, int *z) {
    uint8_t b[6];
    read_raw(b, 6);
    *x = s16_count(b[0], b[1]);
    *y = s16_count(b[2], b[3]);
    *z = s16_count(b[4], b[5]);
}

void accel_init(void) {
    spi_init(spi0, 1000 * 1000);
    spi_set_format(spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_init(PIN_INT1);
    gpio_set_dir(PIN_INT1, GPIO_IN);
    gpio_disable_pulls(PIN_INT1);
    accel_write(0x20, 0x77);             /* 400 Hz, X/Y/Z on */
    accel_write(0x23, 0x88 | (3 << 4));  /* block update, high-res, +/-16 g */
}

int accel_who(void) { return accel_read(0x0F); }

int accel_int1_high(void) { return gpio_get(PIN_INT1) ? 1 : 0; }

void accel_pins_idle(void) {
    /* gpio_init() makes a pin an input first. On CS that is a low chip-select
     * while MOSI and SCK are also moving, which writes garbage into the
     * accelerometer and kills the wake interrupt. Leave the SPI pins alone. */
    gpio_put(PIN_CS, 1);
    gpio_set_dir(PIN_CS, GPIO_OUT);
}

void accel_low_power_wake(void) {
    /* Drop HR so LPen is a real low-power mode, not a reserved combo. */
    accel_write(0x23, 0x80 | (3 << 4));  /* BDU, +/-16 g, no HR */
    accel_write(0x33, 0);                /* fire on the first sample over the threshold */
    accel_write(0x20, 0x2F);             /* ODR 10 Hz, LPen, X/Y/Z */
}
