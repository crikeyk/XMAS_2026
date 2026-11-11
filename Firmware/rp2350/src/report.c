#include "report.h"

#include "log.h"
#include "storage.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *anchor_name(int a) {
    if (a == ANCHOR_ZERO) return "zero";
    if (a == ANCHOR_AUTO) return "auto";
    return "peaks";
}

static const char *passes_name(int p) {
    if (p > 0) return "right";
    if (p < 0) return "left";
    return "both";
}

void report_print(const Tracker *t, const Settings *s, const Frames *frames) {
    int fi = t->cur_frame;
    if (!frames || fi < 0 || fi >= frames->count) fi = 0;
    int n = (frames && frames->count) ? frames->n[fi] : 0;
    double half = (frames && frames->count) ? frames->half[fi] : 0;
    char names[32] = "";
    if (frames) {
        for (int i = 0; i < frames->count; i++) {
            if (i) strncat(names, ",", sizeof names - strlen(names) - 1);
            strncat(names, frames->name[i], sizeof names - strlen(names) - 1);
        }
    }
    pov_log("POV> ===== run report v9: copy everything from here =====");
    pov_log("POV> show=%s cols=%d ANCHOR=%s SHAPE=%.1f(now %.2f) SIGN=%d SHIFT=%.1fms CAL=%d PASSES=%s STRETCH=%.2f WIDTH=%d%% RANGE=16g %.2fmg/count KP=0.20 KW=0.30 KI=0.05",
            names, n, anchor_name(t->anchor), s->shape, t->shape_eff, t->sign, s->shift_ms,
            t->calibrate, passes_name(t->passes), s->stretch, (int)(s->width + 0.5),
            t->g_per_count * 1000.0);
    if (t->anchor == ANCHOR_AUTO)
        pov_log("POV> swing sharpness (crest) = %.2f -> using %s", t->crest_avg, t->peaky ? "PEAKS" : "ZERO CROSSINGS");
    double clip = 0.95 * 2047.0 * t->g_per_count;
    pov_log("POV> bias=%+.3f g  max|x|=%.2f g%s", t->bias, t->xmax, t->xmax > clip ? "  ** CLIPPING **" : "");
    if (t->n_stat) {
        double sum = 0;
        for (int i = 0; i < t->n_stat; i++) sum += t->ts_ms[i];
        double mean = sum / t->n_stat;
        double var = 0, mn = t->ts_ms[0], mx = t->ts_ms[0];
        for (int i = 0; i < t->n_stat; i++) {
            double d = t->ts_ms[i] - mean;
            var += d * d;
            if (t->ts_ms[i] < mn) mn = t->ts_ms[i];
            if (t->ts_ms[i] > mx) mx = t->ts_ms[i];
        }
        double sd = sqrt(var / t->n_stat);
        double col_ms = (n > 0) ? (2.0 * half / n * mean / 3.14159265358979323846) : 0;
        pov_log("POV> swing events=%d  tracked half-swing=%.1f ms (about %.1f Hz)  min=%.1f max=%.1f sd=%.1f ms",
                t->n_stat, mean, mean > 0 ? 500.0 / mean : 0, mn, mx, sd);
        pov_log("POV> one column lasts ~%.2f ms at the swing centre", col_ms);
        if (t->n_stat) {
            double es = 0;
            for (int i = 0; i < t->n_stat; i++) es += t->err_ms[i] * t->err_ms[i];
            double rms = sqrt(es / t->n_stat);
            pov_log("POV> timing surprise per event: rms=%.1f ms = about %.1f columns (this is the blur limit)",
                    rms, col_ms > 0 ? rms / col_ms : 0);
        }
    }
    if (t->n_stat) {
        double ps = 0, pmin = t->peak_g[0];
        for (int i = 0; i < t->n_stat; i++) {
            ps += t->peak_g[i];
            if (t->peak_g[i] < pmin) pmin = t->peak_g[i];
        }
        double arm = 0.5 * t->avg_pk;
        if (arm < 0.4) arm = 0.4;
        pov_log("POV> swing peak |a|: mean=%.2f g  min=%.2f g  arm level now=%.2f g", ps / t->n_stat, pmin, arm);
    }
    if (t->have_lock_ms)
        pov_log("POV> locked after %.0f ms (first event -> display on)", t->first_lock_ms);
    pov_log("POV> locks=%d  re-acquired=%d  gave up (holdover)=%d | ignored: outliers=%d too-close=%d",
            t->n_locks, t->n_reacq, t->n_to, t->n_out, t->n_odd);
    if (t->n_loops && t->t_locked > 0)
        pov_log("POV> loop: %.0f loops/s, avg %.2f ms, worst gap %.1f ms, gaps >1ms=%d >2ms=%d (of %d)",
                t->n_loops / t->t_locked, 1000.0 * t->t_locked / t->n_loops, t->max_gap * 1000.0,
                t->n_g1, t->n_g2, t->n_loops);
    int tot = t->n_cols + t->n_skips;
    if (tot)
        pov_log("POV> columns shown=%d skipped=%d (%.0f%% missed)", t->n_cols, t->n_skips, 100.0 * t->n_skips / tot);
    pov_log("POV> ===== end of report =====");
}

void report_save_log(const Tracker *t) {
    if (t->n_stat < 2) return;
    double sum = 0;
    for (int i = 0; i < t->n_stat; i++) sum += t->ts_ms[i];
    double mean = sum / t->n_stat;
    double var = 0, es = 0, ps = 0;
    for (int i = 0; i < t->n_stat; i++) {
        double d = t->ts_ms[i] - mean;
        var += d * d;
        es += t->err_ms[i] * t->err_ms[i];
        ps += t->peak_g[i];
    }
    double sd = sqrt(var / t->n_stat);
    double rms = sqrt(es / t->n_stat);
    int tot = t->n_cols + t->n_skips;
    double skip = tot ? 100.0 * t->n_skips / tot : 0;
    char line[180];
    snprintf(line, sizeof line,
             "t=%.0fs half=%.0fms sd=%.0f pace=%.2fHz surprise=%.1fms peak=%.1fg skip=%.0f%% out=%d reacq=%d",
             t->log_now, mean, sd, mean > 0 ? 500.0 / mean : 0, rms, ps / t->n_stat, skip, t->n_out, t->n_reacq);
    storage_save_log(line);
}
