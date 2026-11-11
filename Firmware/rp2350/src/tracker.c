#include "tracker.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define TWO_PI (2.0 * PI)
#define THRESH 0.4
#define KP 0.2
#define KW 0.3
#define KI 0.05
#define E_MAX 1.2
#define OUT_MAX 2
#define HOLD 2.5
#define STOP_S 1.0
#define PEAK_DROP 0.7
#define MIN_HALF 0.04
#define MAX_HALF 0.6
#define MOTION_G 0.25

static double wrap_pi(double e) {
    while (e > PI) e -= TWO_PI;
    while (e < -PI) e += TWO_PI;
    return e;
}

static double wrap_tau(double a) {
    a = fmod(a, TWO_PI);
    if (a < 0) a += TWO_PI;
    return a;
}

static double target_of(int ev) {
    if (ev == 1) return PI / 2.0;
    if (ev == -1) return 3.0 * PI / 2.0;
    if (ev == 2) return 0.0;
    return PI; /* -2 */
}

static void reset_stats(Tracker *t) {
    t->n_stat = 0;
    t->n_locks = t->n_loops = t->n_cols = t->n_skips = 0;
    t->n_out = t->n_reacq = t->n_odd = t->n_to = t->n_g1 = t->n_g2 = 0;
    t->have_lock_ms = 0;
    t->first_lock_ms = 0;
    t->t_locked = t->max_gap = t->xmax = t->pk = t->avg_pk = 0;
    t->tr_i = t->tr_n = t->ev_n = 0;
}

static void handle_event(Tracker *t, double tc, int ev) {
    if (!t->sess_active) {
        t->sess_active = 1;
        reset_stats(t);
        t->sess_t0 = tc;
    }
    double target = target_of(ev);
    int accept = 1;
    double half = tc - t->t_last;
    double wm = 0;
    if (t->ev_last != 0 && ev != t->ev_last && half >= MIN_HALF && half <= MAX_HALF)
        wm = PI / half;
    t->t_last = tc;
    t->ev_last = ev;
    if (t->locked) {
        double ph = t->phi + t->w * (tc - t->t_phi);
        double e0 = wrap_pi(target - ph);
        double wn = t->w;
        if (wm && 0.5 * t->w < wm && wm < 2.0 * t->w)
            wn = t->w + KW * (wm - t->w);
        ph = t->phi + wn * (tc - t->t_phi);
        double e = wrap_pi(target - ph);
        if (e > E_MAX || e < -E_MAX) {
            t->n_out++;
            t->out_run++;
            accept = 0;
            if (t->out_run >= OUT_MAX) {
                t->phi = target;
                t->t_phi = tc;
                t->t_c = tc;
                t->d = ev;
                if (wm) t->w = wm;
                t->out_run = 0;
                t->n_reacq++;
            }
        } else {
            t->out_run = 0;
            t->phi = wrap_tau(ph + KP * e);
            t->w = wn + KI * e * wn / PI;
            if (t->w > PI / MIN_HALF) t->w = PI / MIN_HALF;
            else if (t->w < PI / MAX_HALF) t->w = PI / MAX_HALF;
            t->t_phi = tc;
            if (t->n_stat < TR_STAT) {
                t->ts_ms[t->n_stat] = PI / t->w * 1000.0;
                t->err_ms[t->n_stat] = e0 / t->w * 1000.0;
                t->peak_g[t->n_stat] = t->pk;
                t->n_stat++;
            }
            t->avg_pk = (t->avg_pk == 0.0) ? t->pk : t->avg_pk + (t->pk - t->avg_pk) * 0.5;
        }
    } else if (t->d != 0 && ev != t->d) {
        half = tc - t->t_c;
        if (half < MIN_HALF) {
            t->n_odd++;
            accept = 0;
        } else if (half < MAX_HALF) {
            t->w = PI / half;
            t->phi = target;
            t->t_phi = tc;
            t->locked = 1;
            t->out_run = 0;
            t->n_locks++;
            if (!t->have_lock_ms) {
                t->first_lock_ms = (tc - t->sess_t0) * 1000.0;
                t->have_lock_ms = 1;
            }
            t->avg_pk = (t->avg_pk == 0.0) ? t->pk : t->avg_pk + (t->pk - t->avg_pk) * 0.5;
        }
    }
    if (t->debug) {
        if (t->ev_n == TR_EV) {
            memmove(t->ev_t, t->ev_t + 1, (TR_EV - 1) * sizeof t->ev_t[0]);
            memmove(t->ev_c, t->ev_c + 1, (TR_EV - 1) * sizeof t->ev_c[0]);
            memmove(t->ev_u, t->ev_u + 1, (TR_EV - 1) * sizeof t->ev_u[0]);
            t->ev_n--;
        }
        t->ev_t[t->ev_n] = (int)((tc - t->t_ref) * 1000.0);
        t->ev_c[t->ev_n] = ev;
        t->ev_u[t->ev_n] = accept ? 1 : 0;
        t->ev_n++;
    }
    if (accept) {
        t->pk = 0;
        t->t_c = tc;
        t->d = ev;
    }
}

void tracker_debug_event(Tracker *t, double tc, int ev) {
    handle_event(t, tc, ev);
}

void tracker_init(Tracker *t, const Settings *s, const Frames *frames, double now, double bias, double g_per_count) {
    int log_ready = t->log_ready;
    double log_now = t->log_now;
    memset(t, 0, sizeof *t);
    t->log_ready = log_ready;
    t->log_now = log_now;
    t->anchor = s->anchor;
    t->sign = s->sign ? s->sign : 1;
    t->calibrate = s->calibrate;
    t->passes = s->passes;
    t->debug = s->debug;
    t->shift_s = s->shift_ms / 1000.0;
    t->seconds = s->seconds > 0 ? s->seconds : 4.0;
    t->edge_flash = s->edge_flash;
    t->shape = s->shape;
    t->shape_eff = s->shape >= 0 ? s->shape : 0;
    t->width = s->width;
    t->frames = frames;
    t->w = PI / 0.15;
    t->draw_w = t->w;
    t->bias = bias;
    t->prev_t = now;
    t->prev_now = now;
    t->last_motion = now;
    t->t_ref = now;
    t->last_col = -1;
    t->g_per_count = g_per_count;
}

void tracker_on_sample(Tracker *t, double now, double xr) {
    if (t->debug && t->sess_active && now - t->t_c < 0.4) {
        t->tr_t[t->tr_i] = (int)((now - t->t_ref) * 1000.0);
        t->tr_x[t->tr_i] = (int)(xr * 1000.0);
        t->tr_i = (t->tr_i + 1) % TR_TRACE;
        t->tr_n++;
    }
    if (t->sess_active && fabs(xr) > t->xmax) t->xmax = fabs(xr);
    double x = xr * t->sign;
    double rate = 0.3;
    if (!t->sess_active && fabs(x - t->bias) < 0.5 * THRESH) rate = 3.0;
    double dt = now - t->prev_t;
    double step = dt * rate;
    if (step > 1.0) step = 1.0;
    if (step < 0) step = 0;
    t->bias += (x - t->bias) * step;
    double a = x - t->bias;
    if (fabs(a) > t->pk) t->pk = fabs(a);
    if (fabs(a) > MOTION_G) t->last_motion = now;

    double arm = 0.4 * t->avg_pk;
    if (arm < THRESH) arm = THRESH;
    int run_zero = (t->anchor == ANCHOR_ZERO) || (t->anchor == ANCHOR_AUTO && !t->peaky);
    if (run_zero) {
        if (a > arm) t->armed = 1;
        else if (a < -arm) t->armed = -1;
        int cross = 0;
        if (t->armed == 1 && a <= 0) { cross = 1; t->armed = 0; }
        else if (t->armed == -1 && a >= 0) { cross = -1; t->armed = 0; }
        if (cross) {
            double den = t->prev_a - a;
            double frac = den ? t->prev_a / den : 0;
            handle_event(t, t->prev_t + (now - t->prev_t) * frac, cross);
        }
    } else {
        t->armed = 0;
    }
    if (t->anchor != ANCHOR_ZERO) {
        t->acc_a2 += a * a;
        t->acc_n++;
        if (t->ptrack == 0) {
            if (a > arm && t->pdir != 1) t->ptrack = 1;
            else if (a < -arm && t->pdir != -1) t->ptrack = -1;
            if (t->ptrack) {
                t->pmax = a * t->ptrack;
                t->pt = now;
                t->pprev_v = t->prev_a * t->ptrack;
                t->pprev_t = t->prev_t;
                t->pnext_valid = 0;
            }
        } else {
            double v = a * t->ptrack;
            if (v > t->pmax) {
                t->pmax = v;
                t->pt = now;
                t->pprev_v = t->prev_a * t->ptrack;
                t->pprev_t = t->prev_t;
                t->pnext_valid = 0;
            } else {
                if (!t->pnext_valid) {
                    t->pnext_v = v;
                    t->pnext_t = now;
                    t->pnext_valid = 1;
                }
                if (v < t->pmax * PEAK_DROP) {
                    double tp = t->pt;
                    double den = t->pprev_v - 2.0 * t->pmax + t->pnext_v;
                    if (den < -1e-9) {
                        double dl = 0.5 * (t->pprev_v - t->pnext_v) / den;
                        if (dl > 0.5) dl = 0.5;
                        else if (dl < -0.5) dl = -0.5;
                        tp = t->pt + dl * ((dl > 0) ? (t->pnext_t - t->pt) : (t->pt - t->pprev_t));
                    }
                    int ev = 2 * t->ptrack;
                    t->pdir = t->ptrack;
                    t->ptrack = 0;
                    if (t->acc_n > 3 && t->acc_a2 > 0) {
                        double crest = t->pmax / sqrt(t->acc_a2 / t->acc_n);
                        t->crest_avg = (t->crest_avg == 0) ? crest : t->crest_avg + (crest - t->crest_avg) * 0.4;
                        if (t->shape < 0)
                            t->shape_eff = fmin(1.0, fmax(0.0, (t->crest_avg - 1.4) / 2.0));
                        if (t->anchor == ANCHOR_AUTO) {
                            if (t->peaky && t->crest_avg < 1.8) {
                                t->peaky = 0; t->locked = 0; t->d = 0; t->armed = 0;
                            } else if (!t->peaky && t->crest_avg > 2.2) {
                                t->peaky = 1; t->locked = 0; t->d = 0; t->armed = 0;
                            }
                        }
                    }
                    t->acc_a2 = 0;
                    t->acc_n = 0;
                    if (t->anchor == ANCHOR_PEAKS || (t->anchor == ANCHOR_AUTO && t->peaky))
                        handle_event(t, tp, ev);
                }
            }
        }
    }
    t->prev_a = a;
    t->prev_t = now;
}

int tracker_mask(Tracker *t, double now, int warnings) {
    if (t->locked && t->w > 0 && now - t->t_c > HOLD * PI / t->w) {
        t->locked = 0;
        t->n_to++;
        t->avg_pk = 0;
        t->pdir = 0;
        t->ptrack = 0;
    }
    if (t->sess_active && now - t->t_c > STOP_S) {
        t->log_now = now;
        t->log_ready = 1;
        t->sess_active = 0;
        t->locked = 0;
        t->d = 0;
        t->armed = 0;
        t->avg_pk = 0;
        t->pdir = 0;
        t->ptrack = 0;
    }
    const Frames *fr = t->frames;
    if (fr && fr->count > 1 && t->seconds > 0) {
        int fi = (int)(now / t->seconds) % fr->count;
        if (fi < 0) fi = 0;
        if (fi != t->cur_frame) {
            t->cur_frame = fi;
            t->last_col = -1;
        }
    }
    int mask = 0;
    int col = -1;
    if (t->locked && fr && fr->count > 0) {
        int fi = t->cur_frame;
        if (fi < 0 || fi >= fr->count) fi = 0;
        int n = fr->n[fi];
        double half = fr->half[fi];
        const uint16_t *cols = fr->cols[fi];
        if (!t->pass_frozen) {
            t->draw_phi = t->phi;
            t->draw_w = t->w;
            t->draw_t = t->t_phi;
        }
        double ph = t->draw_phi + t->draw_w * (now - t->draw_t + t->shift_s);
        double pos = -cos(ph);
        int direction = sin(ph) > 0 ? 1 : -1;
        if (half > 0 && pos >= -half && pos < half) {
            if (!t->pass_frozen) t->pass_dir = direction;
            t->pass_frozen = 1;
        } else if (t->pass_frozen && direction != t->pass_dir) {
            t->pass_frozen = 0;
        }
        double edge_pos = cos(PI * t->edge_flash);
        if (t->calibrate) {
            if (fabs(pos) < 0.03 || fabs(pos - half) < 0.03 || fabs(pos + half) < 0.03)
                mask = sin(ph) > 0 ? 0xFF00 : 0x00FF;
        } else if (t->passes && sin(ph) * t->passes <= 0) {
            mask = 0;
        } else if (t->edge_flash && (pos > edge_pos || pos < -edge_pos)) {
            mask = 0xFFFF;
        } else if (half > 0 && n > 0 && pos >= -half && pos < half) {
            col = (int)((pos / half + 1.0) * 0.5 * n);
            if (col < 0) col = 0;
            if (col >= n) col = n - 1;
            mask = cols[col];
        }
        if (t->prev_locked) {
            double g = now - t->prev_now;
            t->n_loops++;
            t->t_locked += g;
            if (g > t->max_gap) t->max_gap = g;
            if (g > 0.001) {
                t->n_g1++;
                if (g > 0.002) t->n_g2++;
            }
        }
    } else {
        t->pass_frozen = 0;
        double ph = fmod(now, 2.0);
        if (ph < 0) ph += 2.0;
        if (ph < 0.04 || (warnings && ph > 0.2 && ph < 0.24)) mask = 1;
    }
    if (col != t->last_col) {
        if (col >= 0) {
            t->n_cols++;
            if (t->last_col >= 0 && abs(col - t->last_col) > 1)
                t->n_skips += abs(col - t->last_col) - 1;
        }
        t->last_col = col;
    }
    t->prev_locked = t->locked;
    t->prev_now = now;
    return mask;
}
