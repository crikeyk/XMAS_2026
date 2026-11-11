#pragma once
#include <stdint.h>

void accel_init(void);
int accel_who(void);
void accel_write(uint8_t reg, uint8_t val);
uint8_t accel_read(uint8_t reg);
int accel_x_counts(void);
void accel_xyz_counts(int *x, int *y, int *z);
/* Hold CS high. Do not gpio_init the SPI pins: that glitches CS. */
void accel_pins_idle(void);
/* 10 Hz low-power, duration 0. Interrupt still fires; ~4 µA instead of ~185 µA. */
void accel_low_power_wake(void);
int accel_int1_high(void);
