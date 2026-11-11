#pragma once

extern volatile int g_woke_from_pstate;

int wake_threshold(double wake_g);
double wake_g_actual(double wake_g);
void wake_int_setup(double wake_g);
int wake_arm(void);
/* Does not return when the chip actually powers down. */
int sleep_enter(double wake_g);
#ifdef POV_BENCH
int sleep_bench_ms(int ms);
#endif
int sleep_woke(void);
int sleep_was_instant(void);
