#pragma once
#include <stdint.h>

void leds_init(void);
void leds_put(uint16_t mask);
/* Cathodes high (off) and the shift clock held low, before the chip powers down. */
void leds_park(void);
