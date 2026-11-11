#pragma once
#include <stdint.h>

#define COL_CAP 256
#define PITCH_TEXT 0.055
#define PITCH_IMG  0.075

typedef enum { SHOW_TEXT = 0, SHOW_IMAGE = 1, SHOW_BOTH = 2 } ShowMode;
typedef enum { ANCHOR_AUTO = 0, ANCHOR_ZERO = 1, ANCHOR_PEAKS = 2 } AnchorMode;

typedef struct Settings {
    ShowMode show;
    double seconds;
    double stretch;
    int lit_dark;
    int passes;          /* 0 both, 1 right, -1 left */
    int flip;
    int sign;            /* +1 or -1 */
    double shift_ms;
    int calibrate;
    AnchorMode anchor;
    double shape;        /* <0 means auto; the picture is still a sine */
    double width;        /* percent */
    int debug;
    double edge_flash;
    int sleep_on;
    double sleep_s;
    char sleep_mode[8];
    double wake_g;
    int int_test;
    int warnings;
    void (*warn)(const char *msg);
} Settings;

typedef struct Frames {
    uint16_t cols[2][COL_CAP];
    int n[2];
    double half[2];
    int count;
    char name[2][8];
} Frames;

void settings_defaults(Settings *s);
/* Parse the whole settings file. Bad lines warn and are skipped. */
void settings_parse(Settings *s, const char *text);

/* Build the column masks. bmp may be NULL when the picture is not needed. */
void frames_build(Settings *s, const char *message, const uint8_t *bmp, int bmp_len, Frames *out);
