#pragma once
#include "content.h"

#define TR_STAT 400
#define TR_TRACE 600
#define TR_EV 40

typedef struct Tracker {
    int anchor;
    int sign;
    int calibrate;
    int passes;
    int debug;
    double shift_s;
    double seconds;
    double edge_flash;
    double shape;
    double shape_eff;
    double width;
    const Frames *frames;

    double phi, w, t_phi, t_c, t_last;
    double bias, prev_a, prev_t, pk, avg_pk;
    double draw_phi, draw_w, draw_t;
    int armed, d, ev_last, locked, sess_active, out_run;
    int pass_frozen, pass_dir;
    int ptrack, pdir, pnext_valid;
    double pmax, pt, pprev_v, pprev_t, pnext_v, pnext_t;
    double acc_a2;
    int acc_n;
    double crest_avg;
    int peaky;
    double last_motion;

    int cur_frame, last_col, prev_locked;
    double prev_now, t_ref;

    double ts_ms[TR_STAT], err_ms[TR_STAT], peak_g[TR_STAT];
    int n_stat;
    int n_locks, n_loops, n_cols, n_skips, n_out, n_reacq, n_odd, n_to, n_g1, n_g2;
    double first_lock_ms;
    int have_lock_ms;
    double t_locked, max_gap, xmax, sess_t0;
    int tr_t[TR_TRACE], tr_x[TR_TRACE], tr_i, tr_n;
    int ev_t[TR_EV], ev_c[TR_EV], ev_u[TR_EV], ev_n;
    int log_ready;
    double log_now;
    double g_per_count;
} Tracker;

void tracker_init(Tracker *t, const Settings *s, const Frames *frames, double now, double bias, double g_per_count);
void tracker_on_sample(Tracker *t, double now, double x_g);
int tracker_mask(Tracker *t, double now, int warnings);
/* Feed one swing event. Host test uses this to compare with the Python function. */
void tracker_debug_event(Tracker *t, double tc, int ev);
