/* Host-side check. Compiles the wand's content and tracker with the desktop compiler. */
#include "content.h"
#include "tracker.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(const char *path, int *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (!b) return NULL;
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    if (len_out) *len_out = (int)got;
    return b;
}

static void dump_masks(const char *tag, const uint16_t *m, int n, double half) {
    printf("%s_N %d\n%s_HALF %.9f\n%s_MASKS", tag, n, tag, half, tag);
    for (int i = 0; i < n; i++) printf("%s%04X", i ? "," : " ", m[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: host_check settings message bmp\n");
        return 2;
    }
    char *settings_txt = read_all(argv[1], NULL);
    char *message = read_all(argv[2], NULL);
    int bmp_len = 0;
    char *bmp = read_all(argv[3], &bmp_len);
    if (!settings_txt || !message || !bmp) {
        fprintf(stderr, "could not read inputs\n");
        return 2;
    }

    Settings s;
    memset(&s, 0, sizeof s);
    settings_defaults(&s);
    settings_parse(&s, settings_txt);
    printf("SHOW %d\nSECONDS %.6f\nSTRETCH %.6f\nLIT_DARK %d\nPASSES %d\nFLIP %d\nSIGN %d\n"
           "SHIFT %.6f\nCAL %d\nANCHOR %d\nWIDTH %.6f\nDEBUG %d\nSLEEP %d\nSLEEP_S %.6f\n"
           "WAKE %.6f\nINT_TEST %d\nWARNINGS %d\n",
           (int)s.show, s.seconds, s.stretch, s.lit_dark, s.passes, s.flip, s.sign,
           s.shift_ms, s.calibrate, (int)s.anchor, s.width, s.debug, s.sleep_on, s.sleep_s,
           s.wake_g, s.int_test, s.warnings);

    Frames fr;
    s.show = SHOW_BOTH;
    frames_build(&s, message, (const uint8_t *)bmp, bmp_len, &fr);
    for (int i = 0; i < fr.count; i++)
        dump_masks(fr.name[i], fr.cols[i], fr.n[i], fr.half[i]);

    s.show = SHOW_TEXT;
    frames_build(&s, message, NULL, 0, &fr);
    Tracker t;
    memset(&t, 0, sizeof t);
    tracker_init(&t, &s, &fr, 0.0, 0.0, 0.008);
    double tc = 0.0;
    int evs[] = {2, -2, 2, -2, 2, -2, 2, -2, 2, -2, 2, -2};
    for (unsigned i = 0; i < sizeof evs / sizeof evs[0]; i++) {
        tc += 0.11;
        tracker_debug_event(&t, tc, evs[i]);
    }
    printf("PHI %.9f\nW %.9f\nLOCKED %d\n", t.phi, t.w, t.locked);

    memset(&t, 0, sizeof t);
    tracker_init(&t, &s, &fr, 0.0, 0.0, 0.008);
    int seen[256];
    memset(seen, 0, sizeof seen);
    int nseen = 0;
    for (int i = 1; i <= 800; i++) {
        double now = i * 0.0025;
        double x = 8.0 * cos(2.0 * 3.14159265358979323846 * 4.5 * now);
        tracker_on_sample(&t, now, x);
        int mask = tracker_mask(&t, now, 0);
        (void)mask;
        if (t.locked && t.last_col >= 0 && t.last_col < 256 && !seen[t.last_col]) {
            seen[t.last_col] = 1;
            nseen++;
        }
    }
    printf("SWEEP_LOCKED %d\nSWEEP_COLS %d\n", t.n_locks, nseen);
    return 0;
}
