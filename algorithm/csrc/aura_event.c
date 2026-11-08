/* 块式 PPI 事件检测器。 */
#include "aura_event.h"
#include "aura_kernels.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AURA_MIN_PPI_S 0.333
#define AURA_MAX_PPI_S 1.5
#define AURA_MIRROR_S 2.0
#define AURA_EDGE_S 1.5
#define AURA_WINDOW_S 16.0
#define AURA_STEP_S 1.0
#define AURA_DEDUP_TOL_S 0.05

typedef struct {
    double t, amp, age;
    int win;
} Ev;

static int dcmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median_copy(const double *x, int m) {
    double *b = (double *)malloc((size_t)(m > 0 ? m : 1) * sizeof(double));
    double v;
    if (!b || m <= 0) { free(b); return 0.0; }
    memcpy(b, x, (size_t)m * sizeof(double));
    qsort(b, (size_t)m, sizeof(double), dcmp);
    if (m % 2) v = b[m / 2];
    else v = 0.5 * (b[m / 2 - 1] + b[m / 2]);
    free(b);
    return v;
}

/* 线性插值分位数。 */
static double quantile_linear(const double *x, int n, double q) {
    double pos, w, v;
    int lo, hi;
    double *b;
    if (n <= 0) return 0.0;
    b = (double *)malloc((size_t)n * sizeof(double));
    if (!b) return 0.0;
    memcpy(b, x, (size_t)n * sizeof(double));
    qsort(b, (size_t)n, sizeof(double), dcmp);
    if (n == 1) { v = b[0]; free(b); return v; }
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

/* 居中滑动 RMS 包络。 */
static void envelope(const double *x, int n, double fs, double *env) {
    int win = (int)(1.5 * fs);
    int h, i;
    double *pad, sum = 0.0;
    if (win < 3) win = 3;
    if (win % 2 == 0) win++;
    h = win / 2;
    pad = (double *)malloc((size_t)(n + 2 * h) * sizeof(double));
    if (!pad) { for (i = 0; i < n; ++i) env[i] = 1.0; return; }
    for (i = 0; i < h; ++i) pad[i] = x[0] * x[0];
    for (i = 0; i < n; ++i) pad[h + i] = x[i] * x[i];
    for (i = 0; i < h; ++i) pad[h + n + i] = x[n - 1] * x[n - 1];
    for (i = 0; i < win; ++i) sum += pad[i];
    for (i = 0; i < n; ++i) {
        double v = sum / (double)win;
        env[i] = sqrt(v > 1e-12 ? v : 1e-12);
        sum -= pad[i];
        sum += pad[i + win];
    }
    free(pad);
}

/* 镜像延拓信号，返回两侧长度。 */
static double *mirror_extend(const double *sig, int n, int nm,
                             int *left_len, int *right_len) {
    int ll, rl, i;
    double *ext;
    *left_len = 0;
    *right_len = 0;
    if (nm <= 0 || n < 2) {
        ext = (double *)malloc((size_t)(n > 0 ? n : 1) * sizeof(double));
        if (ext && n > 0) memcpy(ext, sig, (size_t)n * sizeof(double));
        return ext;
    }
    ll = (n > nm) ? nm : n;
    rl = ll;
    ext = (double *)malloc((size_t)(n + ll + rl) * sizeof(double));
    if (!ext) return NULL;
    for (i = 0; i < ll; ++i)
        ext[i] = (n > nm) ? sig[nm - i] : sig[n - 1 - i];
    memcpy(&ext[ll], sig, (size_t)n * sizeof(double));
    for (i = 0; i < rl; ++i)
        ext[ll + n + i] = (n > nm) ? sig[n - 2 - i] : sig[n - 1 - i];
    *left_len = ll;
    *right_len = rl;
    return ext;
}

static void append_ev(Ev **ev, int *ne, int *cap, double t, double amp,
                      double age, int win) {
    if (*ne >= *cap) {
        int nc = *cap ? *cap * 2 : 64;
        Ev *p = (Ev *)realloc(*ev, (size_t)nc * sizeof(Ev));
        if (!p) return;
        *ev = p;
        *cap = nc;
    }
    (*ev)[*ne].t = t;
    (*ev)[*ne].amp = amp;
    (*ev)[*ne].age = age;
    (*ev)[*ne].win = win;
    (*ne)++;
}

static void detect_window(const double *sig, int n, double fs,
                          double win_start, double min_ppi, int win_idx,
                          Ev **ev, int *ne, int *cap) {
    int nm = (int)(AURA_MIRROR_S * fs);
    int edge = (int)(AURA_EDGE_S * fs);
    int ll = 0, rl = 0, ext_len, vs, ve, vn, i, np;
    double *ext, *valid = NULL, *env = NULL, *norm = NULL;
    int *peaks = NULL;
    double eff_min_ppi, floor_v;
    if (n < 4) return;
    eff_min_ppi = (min_ppi > 0.0) ? min_ppi : AURA_MIN_PPI_S;
    ext = mirror_extend(sig, n, nm, &ll, &rl);
    if (!ext) return;
    ext_len = n + ll + rl;
    vs = nm + edge;
    ve = nm + n - edge;
    if (vs < 0) vs = 0;
    if (ve > ext_len) ve = ext_len;
    if (vs > ext_len) vs = ext_len;
    if (ve <= vs) {
        edge = (n - (int)(2.0 * AURA_MIN_PPI_S * fs)) / 2;
        if (edge < 0) edge = 0;
        vs = nm + edge;
        ve = nm + n - edge;
        if (vs < 0) vs = 0;
        if (ve > ext_len) ve = ext_len;
        if (vs > ext_len) vs = ext_len;
        if (ve <= vs) { free(ext); return; }
    }
    vn = ve - vs;
    valid = (double *)malloc((size_t)vn * sizeof(double));
    env = (double *)malloc((size_t)vn * sizeof(double));
    norm = (double *)malloc((size_t)vn * sizeof(double));
    peaks = (int *)malloc((size_t)(vn / 2 + 2) * sizeof(int));
    if (!valid || !env || !norm || !peaks) {
        free(ext); free(valid); free(env); free(norm); free(peaks);
        return;
    }
    memcpy(valid, &ext[vs], (size_t)vn * sizeof(double));
    envelope(valid, vn, fs, env);
    floor_v = quantile_linear(env, vn, 0.02);
    if (floor_v < 1e-6) floor_v = 1e-6;
    for (i = 0; i < vn; ++i)
        norm[i] = valid[i] / (env[i] > floor_v ? env[i] : floor_v);
    {
        double min_dist = (double)((int)(eff_min_ppi * fs));
        double wlen = (double)(2 * (int)(eff_min_ppi * fs));
        int wl = (int)wlen;
        if (min_dist < 1.0) min_dist = 1.0;
        if (wl > vn - 1) wl = vn - 1;
        if (wl < 5) wl = 5;
        if (wl % 2 == 0) wl++;
        np = aura_find_peaks_w(norm, vn, min_dist, 0.60, 0.20, (double)wl,
                               peaks, vn / 2 + 2, NULL, NULL);
    }
    for (i = 0; i < np; ++i) {
        int p = peaks[i];
        double frac = 0.0;
        if (p > 0 && p < vn - 1) {
            double a = valid[p - 1], b = valid[p], c = valid[p + 1];
            double den = a - 2.0 * b + c;
            if (fabs(den) > 1e-12) {
                frac = 0.5 * (a - c) / den;
                if (frac < -0.5) frac = -0.5;
                if (frac > 0.5) frac = 0.5;
            }
        }
        {
            double t_rel = ((double)p + frac + (double)(vs - nm)) / fs;
            append_ev(ev, ne, cap, win_start + t_rel, valid[p], t_rel, win_idx);
        }
    }
    free(ext); free(valid); free(env); free(norm); free(peaks);
}

/* 返回本次新增事件数。 */
static int sliding_detect(const double *sig, int n, double fs,
                          double start, double min_ppi,
                          Ev **ev, int *ne, int *cap) {
    int win = (int)(AURA_WINDOW_S * fs);
    int step = (int)(AURA_STEP_S * fs);
    int i, before = *ne;
    if (n < win) {
        detect_window(sig, n, fs, start, min_ppi, 0, ev, ne, cap);
        return *ne - before;
    }
    for (i = 0; i + win <= n; i += step) {
        detect_window(&sig[i], win, fs, start + (double)i / fs,
                      min_ppi, i / step, ev, ne, cap);
    }
    return *ne - before;
}

static int ev_cmp(const void *a, const void *b) {
    const Ev *x = (const Ev *)a, *y = (const Ev *)b;
    if (x->t < y->t) return -1;
    if (x->t > y->t) return 1;
    if (x->win < y->win) return -1;
    if (x->win > y->win) return 1;
    return 0;
}

static int dedup(Ev *ev, int ne, double tol) {
    int i, nk = 0;
    if (ne <= 0) return 0;
    qsort(ev, (size_t)ne, sizeof(Ev), ev_cmp);
    for (i = 0; i < ne; ++i) {
        if (nk == 0 || ev[i].t - ev[nk - 1].t > tol) {
            ev[nk++] = ev[i];
        } else if (ev[i].age > ev[nk - 1].age) {
            ev[nk - 1] = ev[i];
        }
    }
    return nk;
}

static double estimate_median_ibi(const Ev *ev, int ne) {
    double *dt, v = 0.0;
    int i, nd = 0;
    if (ne < 8) return 0.0;
    dt = (double *)malloc((size_t)(ne - 1) * sizeof(double));
    if (!dt) return 0.0;
    for (i = 1; i < ne; ++i) {
        double d = ev[i].t - ev[i - 1].t;
        if (d >= AURA_MIN_PPI_S && d <= AURA_MAX_PPI_S) dt[nd++] = d;
    }
    if (nd >= 6) v = median_copy(dt, nd);
    free(dt);
    return v;
}

typedef struct { double h; int lag; } PeakH;

static int peakh_cmp(const void *a, const void *b) {
    const PeakH *x = (const PeakH *)a, *y = (const PeakH *)b;
    if (x->h > y->h) return -1;
    if (x->h < y->h) return 1;
    return (x->lag < y->lag) ? -1 : (x->lag > y->lag ? 1 : 0);
}

/* 归一化自相关，大 N 走 FFT。 */
static void acf_normalized(const double *x, int n, double *acf) {
    if (n < 2048) {
        aura_acf(x, n, acf);
    } else {
        int nfft = 1, i, k;
        double *re, *im, mean = 0.0;
        aura_complex *spec;
        while (nfft < 2 * n - 1) nfft <<= 1;
        re = (double *)calloc((size_t)nfft, sizeof(double));
        im = (double *)calloc((size_t)nfft, sizeof(double));
        spec = (aura_complex *)malloc((size_t)nfft * sizeof(aura_complex));
        if (!re || !im || !spec) {
            free(re); free(im); free(spec);
            aura_acf(x, n, acf);
            return;
        }
        for (i = 0; i < n; ++i) mean += x[i];
        mean /= (double)n;
        for (i = 0; i < n; ++i) re[i] = x[i] - mean;
        aura_fft(re, NULL, nfft, spec);
        for (k = 0; k < nfft; ++k) {
            re[k] = spec[k].re * spec[k].re + spec[k].im * spec[k].im;
            im[k] = 0.0;
        }
        aura_ifft(re, im, nfft, spec);
        for (k = 0; k < n; ++k) acf[k] = spec[k].re;
        for (k = 0; k < n; ++k) acf[k] /= acf[0];
        free(re); free(im); free(spec);
    }
}

static double acf_ref_period(const double *sig, int n, double fs) {
    double *acf = NULL;
    int lmin, lmax, m, np, i, ntop;
    int *pks = NULL;
    PeakH *ph = NULL;
    double T1, H1, T05 = -1.0, H05 = -1.0, H_cand, T_cand, half_T1 = 0.0;
    if (n < (int)(2.0 * fs)) return 0.0;
    acf = (double *)malloc((size_t)n * sizeof(double));
    if (!acf) return 0.0;
    acf_normalized(sig, n, acf);
    lmin = (int)(AURA_MIN_PPI_S * fs);
    lmax = (int)(AURA_MAX_PPI_S * fs);
    if (lmin < 1) lmin = 1;
    if (lmax > n - 1) lmax = n - 1;
    if (lmax <= lmin) { free(acf); return 0.0; }
    m = lmax - lmin + 1;
    pks = (int *)malloc((size_t)m * sizeof(int));
    ph = (PeakH *)malloc((size_t)m * sizeof(PeakH));
    if (!pks || !ph) { free(acf); free(pks); free(ph); return 0.0; }
    {
        double dist = (double)(lmin / 2);
        if (dist < 2.0) dist = 2.0;
        np = aura_find_peaks(&acf[lmin], m, dist, -1e308, 0.02,
                             pks, m, NULL, NULL);
    }
    if (np <= 0) { free(acf); free(pks); free(ph); return 0.0; }
    for (i = 0; i < np; ++i) {
        ph[i].lag = lmin + pks[i];
        ph[i].h = acf[ph[i].lag];
    }
    qsort(ph, (size_t)np, sizeof(PeakH), peakh_cmp);
    T1 = (double)ph[0].lag / fs;
    H1 = ph[0].h;
    if (T1 <= 0.0) { free(acf); free(pks); free(ph); return 0.0; }
    ntop = np < 5 ? np : 5;
    for (i = 0; i < ntop; ++i) {
        double Ti = (double)ph[i].lag / fs;
        double Hi = ph[i].h;
        double ratio = Ti / T1;
        if (ratio >= 0.42 && ratio <= 0.58 && Hi > H05) {
            T05 = Ti;
            H05 = Hi;
        }
    }
    if (T05 > 0.0 && H05 > 0.0 && H05 >= 0.35 * H1) {
        free(acf); free(pks); free(ph);
        return T05;
    }
    /* 重搏波污染周期检测 */
    if (T1 >= 0.55 && T1 <= 0.85) {
        int lag_lo = (int)round(1.20 * T1 * fs);
        int lag_hi = (int)round(1.70 * T1 * fs);
        T_cand = -1.0;
        H_cand = -1e300;
        if (lag_lo < lmin) lag_lo = lmin;
        if (lag_hi > lmax) lag_hi = lmax;
        if (lag_hi > lag_lo) {
            int best = lag_lo;
            for (i = lag_lo; i <= lag_hi; ++i)
                if (acf[i] > acf[best]) best = i;
            H_cand = acf[best];
            T_cand = (double)best / fs;
        }
        for (i = 0; i < ntop; ++i) {
            double Ti = (double)ph[i].lag / fs;
            double Hi = ph[i].h;
            double r = Ti / T1;
            if (r >= 1.20 && r <= 1.70 && Hi > H_cand) {
                T_cand = Ti;
                H_cand = Hi;
            }
        }
        if (T_cand > 0.0 && H_cand >= 0.50 * H1) {
            int qualified = 0;
            double r_cand = T_cand / T1;
            if (r_cand > 1.15 && T_cand < 1.4) {
                double half_cand = T_cand / 2.0;
                if (half_cand >= AURA_MIN_PPI_S && half_cand <= AURA_MAX_PPI_S) {
                    int hlo = (int)round(0.42 * T_cand * fs);
                    int hhi = (int)round(0.58 * T_cand * fs);
                    double h_half = 0.0;
                    if (hlo < lmin) hlo = lmin;
                    if (hhi > lmax) hhi = lmax;
                    if (hhi > hlo) {
                        for (i = hlo; i <= hhi; ++i)
                            if (acf[i] > h_half) h_half = acf[i];
                    }
                    qualified = !(h_half >= 0.75 * H1);
                } else {
                    qualified = 1;
                }
            }
            if (qualified) {
                int has_strong = 0;
                int hlo = (int)round(0.42 * T1 * fs);
                int hhi = (int)round(0.58 * T1 * fs);
                half_T1 = 0.5 * T1;
                if (hlo < lmin) hlo = lmin;
                if (hhi > lmax) hhi = lmax;
                if (hhi > hlo) {
                    double half_h = 0.0;
                    for (i = hlo; i <= hhi; ++i)
                        if (acf[i] > half_h) half_h = acf[i];
                    if (half_h >= 0.30 * H1) has_strong = 1;
                }
                for (i = 0; i < ntop; ++i) {
                    double Ti = (double)ph[i].lag / fs;
                    double Hi = ph[i].h;
                    double r2 = Ti / half_T1;
                    if (r2 >= 0.85 && r2 <= 1.15 && Hi >= 0.30 * H1) {
                        has_strong = 1;
                        break;
                    }
                }
                if (!has_strong) {
                    free(acf); free(pks); free(ph);
                    return T_cand;
                }
            }
        }
    }
    free(acf); free(pks); free(ph);
    if (H1 <= 0.0) return 0.0;
    return T1;
}

static int remove_dicrotic_events(Ev *ev, int ne, double ref) {
    double med;
    double *dt;
    int i, nd, nk = ne, dropped = 0;
    unsigned char *drop;
    if (ne < 3) return ne;
    dt = (double *)malloc((size_t)(ne - 1) * sizeof(double));
    drop = (unsigned char *)calloc((size_t)ne, sizeof(unsigned char));
    if (!dt || !drop) { free(dt); free(drop); return ne; }
    for (i = 1; i < ne; ++i) dt[i - 1] = ev[i].t - ev[i - 1].t;
    nd = ne - 1;
    med = ref;
    if (!(med >= AURA_MIN_PPI_S && med <= AURA_MAX_PPI_S))
        med = estimate_median_ibi(ev, ne);
    if (!(med > 0.0)) { free(dt); free(drop); return ne; }
    {
        double pair_lo_A1 = 1.5 * med, pair_hi_A1 = 2.5 * med;
        double pair_lo_A2 = 0.9 * med, pair_hi_A2 = 1.4 * med;
        double sum_lo_C = 0.80 * med, sum_hi_C = 1.80 * med;
        i = 0;
        while (i < nd - 1) {
            double a = dt[i], b = dt[i + 1], s = a + b;
            int match = 0;
            if (s >= pair_lo_A1 && s <= pair_hi_A1) {
                double ra = a / s;
                if (ra >= 0.25 && ra <= 0.40) match = 1;
                else {
                    double rb = b / s;
                    if (rb >= 0.25 && rb <= 0.45) match = 1;
                }
            }
            if (!match && s >= pair_lo_A2 && s <= pair_hi_A2) {
                if (a < 0.7 * med) {
                    double ra = a / s;
                    if (ra >= 0.25 && ra <= 0.42) match = 1;
                }
                if (!match && b < 0.7 * med) {
                    double rb = b / s;
                    if (rb >= 0.25 && rb <= 0.48) match = 1;
                }
            }
            if (!match && s >= sum_lo_C && s <= sum_hi_C) {
                double ra = a / s;
                if (ra >= 0.42 && ra <= 0.58) match = 1;
            }
            if (match) {
                drop[i + 1] = 1;
                ++dropped;
                i += 2;
                continue;
            }
            ++i;
        }
    }
    if (dropped > 0) {
        nk = 0;
        for (i = 0; i < ne; ++i) if (!drop[i]) ev[nk++] = ev[i];
    }
    free(dt); free(drop);
    return nk;
}

static int remove_halfbeat_events(Ev *ev, int ne, double ref) {
    double med, a_thr, *ts;
    int i, nk = ne, dropped = 0;
    unsigned char *drop;
    if (ne < 3) return ne;
    med = ref;
    if (!(med >= AURA_MIN_PPI_S && med <= AURA_MAX_PPI_S))
        med = estimate_median_ibi(ev, ne);
    if (!(med > 0.0)) return ne;
    a_thr = 0.80 * med;
    ts = (double *)malloc((size_t)ne * sizeof(double));
    drop = (unsigned char *)calloc((size_t)ne, sizeof(unsigned char));
    if (!ts || !drop) { free(ts); free(drop); return ne; }
    for (i = 0; i < ne; ++i) ts[i] = ev[i].t;
    for (i = 0; i < ne - 2; ++i) {
        double a, b, s;
        if (drop[i] || drop[i + 1]) continue;
        a = ts[i + 1] - ts[i];
        b = ts[i + 2] - ts[i + 1];
        s = a + b;
        if (a < a_thr) {
            int is_t1 = (s >= 0.90 * med && s <= 1.35 * med);
            int is_t2 = (s >= 1.65 * med && s <= 2.45 * med);
            if (is_t1 || is_t2) {
                drop[i + 1] = 1;
                ++dropped;
            }
        }
    }
    if (dropped > 0) {
        nk = 0;
        for (i = 0; i < ne; ++i) if (!drop[i]) ev[nk++] = ev[i];
    }
    free(ts); free(drop);
    return nk;
}

int aura_event_detect(const double *signal, int n, double fs,
                      double start_time_s, aura_event_result *out) {
    Ev *ev = NULL;
    int ne = 0, cap = 0, pass, nkeep, i, n_raw = 0;
    double med = 0.0, ref, adaptive = 0.0;
    if (!signal || !out || n <= 0 || fs <= 0.0) return -1;
    memset(out, 0, sizeof(*out));
    n_raw = sliding_detect(signal, n, fs, start_time_s, AURA_MIN_PPI_S,
                           &ev, &ne, &cap);
    nkeep = dedup(ev, ne, AURA_DEDUP_TOL_S);
    ne = nkeep;
    for (pass = 0; pass < 1; ++pass) {
        med = estimate_median_ibi(ev, ne);
        if (med > 0.0) {
            double cand = 0.6 * med;
            if (cand < AURA_MIN_PPI_S) cand = AURA_MIN_PPI_S;
            if (cand > 0.9) cand = 0.9;
            if (cand > AURA_MIN_PPI_S + 0.05) adaptive = cand;
        }
    }
    if (adaptive > 0.0) {
        Ev *ev2 = NULL;
        int ne2 = 0, cap2 = 0;
        n_raw = sliding_detect(signal, n, fs, start_time_s, adaptive,
                               &ev2, &ne2, &cap2);
        ne2 = dedup(ev2, ne2, AURA_DEDUP_TOL_S);
        free(ev);
        ev = ev2;
        ne = ne2;
    }
    ref = acf_ref_period(signal, n, fs);
    ne = remove_dicrotic_events(ev, ne, ref);
    ne = remove_halfbeat_events(ev, ne, ref);

    out->times_s = (double *)malloc((size_t)(ne > 0 ? ne : 1) * sizeof(double));
    out->amps = (double *)malloc((size_t)(ne > 0 ? ne : 1) * sizeof(double));
    out->ibis_ms = (double *)malloc((size_t)(ne > 1 ? ne - 1 : 1) * sizeof(double));
    out->ibi_times_s = (double *)malloc((size_t)(ne > 1 ? ne - 1 : 1) * sizeof(double));
    out->segment_ids = (int *)malloc((size_t)(ne > 1 ? ne - 1 : 1) * sizeof(int));
    out->segment_start_times_s =
        (double *)malloc((size_t)(ne > 1 ? ne - 1 : 1) * sizeof(double));
    if (!out->times_s || !out->amps || !out->ibis_ms || !out->ibi_times_s
            || !out->segment_ids || !out->segment_start_times_s) {
        aura_event_free(out);
        free(ev);
        return -1;
    }
    out->n_events = ne;
    out->n_raw = n_raw;
    for (i = 0; i < ne; ++i) {
        out->times_s[i] = ev[i].t;
        out->amps[i] = ev[i].amp;
    }
    {
        int cur_seg = 0;
        for (i = 1; i < ne; ++i) {
            double d = ev[i].t - ev[i - 1].t;
            if (!(d >= AURA_MIN_PPI_S && d <= AURA_MAX_PPI_S)) {
                ++cur_seg;
                continue;
            }
            out->ibis_ms[out->n_ibis] = d * 1000.0;
            out->ibi_times_s[out->n_ibis] = ev[i].t;
            out->segment_ids[out->n_ibis] = cur_seg;
            ++out->n_ibis;
        }
        if (out->n_ibis > 0) {
            out->n_segments = cur_seg + 1;
            for (i = 0; i < out->n_ibis; ++i) {
                if (i == 0 || out->segment_ids[i] != out->segment_ids[i - 1]) {
                    out->segment_start_times_s[out->n_segment_starts++] =
                        out->ibi_times_s[i];
                }
            }
        }
    }
    free(ev);
    return 0;
}

void aura_event_free(aura_event_result *r) {
    if (!r) return;
    free(r->times_s);
    free(r->amps);
    free(r->ibis_ms);
    free(r->ibi_times_s);
    free(r->segment_ids);
    free(r->segment_start_times_s);
    r->times_s = NULL;
    r->amps = NULL;
    r->ibis_ms = NULL;
    r->ibi_times_s = NULL;
    r->segment_ids = NULL;
    r->segment_start_times_s = NULL;
    r->n_events = r->n_ibis = r->n_raw = 0;
    r->n_segments = r->n_segment_starts = 0;
}
