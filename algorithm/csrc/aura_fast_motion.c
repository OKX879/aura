/* 复数相位快速运动检测。 */
#include "aura_fast_motion.h"
#include "aura_kernels.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void dsort(double *x, int n) {
    int i, j;
    for (i = 1; i < n; ++i) {
        double v = x[i];
        j = i - 1;
        while (j >= 0 && x[j] > v) { x[j + 1] = x[j]; --j; }
        x[j + 1] = v;
    }
}

static double dmedian(double *x, int n) {
    if (n <= 0) return 0.0;
    dsort(x, n);
    if (n % 2) return x[n / 2];
    return 0.5 * (x[n / 2 - 1] + x[n / 2]);
}

static double quantile_linear(const double *x, int n, double q) {
    double *b;
    double pos, w, v;
    int lo, hi;
    if (n <= 0) return 0.0;
    b = (double *)malloc((size_t)n * sizeof(double));
    if (!b) return 0.0;
    memcpy(b, x, (size_t)n * sizeof(double));
    dsort(b, n);
    pos = q * (double)(n - 1);
    lo = (int)floor(pos);
    if (lo < 0) lo = 0;
    if (lo > n - 1) lo = n - 1;
    hi = lo + 1;
    if (hi > n - 1) hi = n - 1;
    w = pos - (double)lo;
    v = b[lo] * (1.0 - w) + b[hi] * w;
    free(b);
    return v;
}

void aura_fast_motion_init(aura_fast_motion *g) {
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->recent_s = 0.5;
    g->phase_threshold_rad = 0.10;
    g->excursion_threshold_rad = 2.5;
    g->quiet_windows_to_clear = 4;
}

void aura_fast_motion_reset(aura_fast_motion *g) {
    if (!g) return;
    g->active = 0;
    g->quiet_streak = 0;
}

int aura_fast_motion_update(aura_fast_motion *g,
                            const double *zr, const double *zi,
                            int T, int C, int R, double fs,
                            aura_fast_motion_snap *out) {
    int recent_n, start, nf, t, c, r;
    double *per_frame = NULL, *tmp_vec = NULL, *ph = NULL;
    double level = 0.0, excursion = 0.0;
    int detected = 0;
    if (!g || !out || !zr || !zi || T <= 0 || C <= 0 || R <= 0 || fs <= 0.0)
        return -1;
    memset(out, 0, sizeof(*out));
    out->reason = "insufficient_recent_complex_samples";
    out->active = g->active;
    out->quiet_streak = g->quiet_streak;
    recent_n = (int)(g->recent_s * fs + 0.5);
    if (recent_n < 8) recent_n = 8;
    if (T < recent_n + 1) {
        out->hold_remaining_windows = g->active
            ? (g->quiet_windows_to_clear - g->quiet_streak > 0
               ? g->quiet_windows_to_clear - g->quiet_streak : 0)
            : 0;
        return 0;
    }
    start = T - (recent_n + 1);
    nf = recent_n;
    per_frame = (double *)malloc((size_t)nf * sizeof(double));
    tmp_vec = (double *)malloc((size_t)(C * R) * sizeof(double));
    ph = (double *)malloc((size_t)(recent_n + 1) * sizeof(double));
    if (!per_frame || !tmp_vec || !ph) {
        free(per_frame); free(tmp_vec); free(ph);
        return -1;
    }
    for (t = 0; t < nf; ++t) {
        size_t base0 = (size_t)(start + t) * C * R;
        size_t base1 = base0 + (size_t)C * R;
        int i;
        for (i = 0; i < C * R; ++i) {
            double ar = zr[base0 + i], ai = zi[base0 + i];
            double br = zr[base1 + i], bi = zi[base1 + i];
            double rr = br * ar + bi * ai;
            double ii = bi * ar - br * ai;
            tmp_vec[i] = fabs(atan2(ii, rr));
        }
        per_frame[t] = dmedian(tmp_vec, C * R);
    }
    level = quantile_linear(per_frame, nf, 0.9);
    /* 逐通道相位峰峰值取中位 */
    for (c = 0; c < C; ++c) {
        for (r = 0; r < R; ++r) {
            int k;
            for (k = 0; k <= recent_n; ++k) {
                size_t idx = (size_t)(start + k) * C * R + (size_t)c * R + r;
                ph[k] = atan2(zi[idx], zr[idx]);
            }
            aura_unwrap_phase(ph, recent_n + 1, ph);
            {
                double mn = ph[0], mx = ph[0];
                for (k = 1; k <= recent_n; ++k) {
                    if (ph[k] < mn) mn = ph[k];
                    if (ph[k] > mx) mx = ph[k];
                }
                tmp_vec[(size_t)c * R + r] = mx - mn;
            }
        }
    }
    excursion = dmedian(tmp_vec, C * R);
    detected = (level >= g->phase_threshold_rad
                || excursion >= g->excursion_threshold_rad);
    if (detected) {
        g->active = 1;
        g->quiet_streak = 0;
        out->reason = (level >= g->phase_threshold_rad
                       && excursion >= g->excursion_threshold_rad)
            ? "fast_phase_and_excursion"
            : (level >= g->phase_threshold_rad)
                ? "fast_phase_increment" : "coherent_phase_excursion";
    } else if (g->active) {
        g->quiet_streak++;
        if (g->quiet_streak >= g->quiet_windows_to_clear) {
            g->active = 0;
            out->reason = "recovered_after_quiet_hold";
        } else {
            out->reason = "post_motion_hold";
        }
    } else {
        g->quiet_streak = 0;
        out->reason = "quiet";
    }
    out->active = g->active;
    out->detected = detected;
    out->level_rad = level;
    out->excursion_rad = excursion;
    out->quiet_streak = g->quiet_streak;
    out->hold_remaining_windows = g->active
        ? (g->quiet_windows_to_clear - g->quiet_streak > 0
           ? g->quiet_windows_to_clear - g->quiet_streak : 0)
        : 0;
    free(per_frame); free(tmp_vec); free(ph);
    return 0;
}
