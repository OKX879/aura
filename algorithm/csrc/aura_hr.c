/* 心率估计与三路融合实现。 */
#include "aura_hr.h"
#include "aura_event.h"
#include "aura_kernels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int dcmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median_copy(const double *x, int m) {
    double *buf = (double *)malloc((size_t)(m > 0 ? m : 1) * sizeof(double));
    double v;
    if (!buf) return 0.0;
    memcpy(buf, x, (size_t)m * sizeof(double));
    qsort(buf, (size_t)m, sizeof(double), dcmp);
    if (m % 2) v = buf[m / 2];
    else v = 0.5 * (buf[m / 2 - 1] + buf[m / 2]);
    free(buf);
    return v;
}

static int rfft_real(const double *x, int n, aura_complex *half) {
    aura_complex *full = (aura_complex *)malloc((size_t)n * sizeof(aura_complex));
    int k, nh = n / 2 + 1;
    if (!full) return -1;
    if (aura_fft(x, NULL, n, full) != 0) {
        free(full);
        return -1;
    }
    for (k = 0; k < nh; ++k) half[k] = full[k];
    free(full);
    return 0;
}

static int irfft_half(const aura_complex *half, int n, double *out) {
    int k, nh = n / 2 + 1;
    aura_complex *full = (aura_complex *)malloc((size_t)n * sizeof(aura_complex));
    if (!full) return -1;
    full[0] = half[0];
    for (k = 1; k < nh; ++k) {
        full[k] = half[k];
        full[n - k].re = half[k].re;
        full[n - k].im = -half[k].im;
    }
    if (n % 2 == 0) full[n / 2] = half[n / 2];
    /* 拆分实虚部供 IFFT 使用 */
    {
        double *re = (double *)malloc((size_t)n * sizeof(double));
        double *im = (double *)malloc((size_t)n * sizeof(double));
        aura_complex *res = (aura_complex *)malloc((size_t)n * sizeof(aura_complex));
        if (!re || !im || !res) {
            free(re); free(im); free(res); free(full);
            return -1;
        }
        for (k = 0; k < n; ++k) { re[k] = full[k].re; im[k] = full[k].im; }
        aura_ifft(re, im, n, res);
        for (k = 0; k < n; ++k) out[k] = res[k].re;
        free(re); free(im); free(res);
    }
    free(full);
    return 0;
}

int aura_rfft_roundtrip(const double *x, int n, double *out) {
    int nh = n / 2 + 1, rc;
    aura_complex *half = (aura_complex *)malloc((size_t)nh * sizeof(aura_complex));
    if (!half) return -1;
    rc = rfft_real(x, n, half);
    if (rc == 0) rc = irfft_half(half, n, out);
    free(half);
    return rc;
}

double aura_estimate_resp(const double *sig, int n, double fs,
                          const double *sos_resp, int ns_resp) {
    double mean = 0.0, *x = NULL, *bp = NULL, *f = NULL, *p = NULL;
    int nperseg, nf, k, best = -1;
    double bestv = -1e300, bg, snr, f_peak;
    /* 呼吸带下边界取 0.18Hz */
    const double f_lo = 0.18, f_hi = 0.5;
    if (!sig || !sos_resp || n < 256 || fs <= 0.0) return NAN;
    if (f_hi >= 0.5 * fs) return NAN;
    x = (double *)malloc((size_t)n * sizeof(double));
    bp = (double *)malloc((size_t)n * sizeof(double));
    if (!x || !bp) { free(x); free(bp); return NAN; }
    for (k = 0; k < n; ++k) mean += sig[k];
    mean /= (double)n;
    for (k = 0; k < n; ++k) x[k] = sig[k] - mean;
    if (aura_sosfiltfilt(sos_resp, ns_resp, x, n, bp, -1) != 0) {
        free(x); free(bp); return NAN;
    }
    nperseg = (fs >= 30.0) ? (n < 2048 ? n : 2048) : (n < 256 ? n : 256);
    nf = nperseg / 2 + 1;
    f = (double *)malloc((size_t)nf * sizeof(double));
    p = (double *)malloc((size_t)nf * sizeof(double));
    if (!f || !p) { free(x); free(bp); free(f); free(p); return NAN; }
    if (aura_welch(bp, n, fs, nperseg, f, p) < 0) {
        free(x); free(bp); free(f); free(p); return NAN;
    }
    {
        double *band = (double *)malloc((size_t)nf * sizeof(double));
        int nb = 0;
        if (!band) { free(x); free(bp); free(f); free(p); return NAN; }
        for (k = 0; k < nf; ++k) {
            if (f[k] >= f_lo && f[k] <= f_hi) {
                band[nb] = p[k];
                if (p[k] > bestv) { bestv = p[k]; best = k; }
                nb++;
            }
        }
        if (nb == 0 || best < 0 || bestv <= 0.0) {
            free(band); free(x); free(bp); free(f); free(p);
            return NAN;
        }
        f_peak = f[best];
        if (!(f_peak >= 7.0 / 60.0 && f_peak <= 28.0 / 60.0)) {
            free(band); free(x); free(bp); free(f); free(p);
            return NAN;
        }
        bg = median_copy(band, nb);
        if (bg <= 0.0) {
            free(band); free(x); free(bp); free(f); free(p);
            return NAN;
        }
        snr = bestv / bg;
        if (snr < 1.8) {
            free(band); free(x); free(bp); free(f); free(p);
            return NAN;
        }
        if (best > 0 && best < nf - 1) {
            double y0 = p[best - 1], y1 = p[best], y2 = p[best + 1];
            double den = y0 - 2.0 * y1 + y2;
            if (fabs(den) > 1e-12) {
                double delta = 0.5 * (y0 - y2) / den;
                if (fabs(delta) <= 0.5) {
                    double fc = f[best] + delta * (f[best + 1] - f[best]);
                    if (fc >= 7.0 / 60.0 && fc <= 28.0 / 60.0) f_peak = fc;
                }
            }
        }
        free(band);
    }
    free(x); free(bp); free(f); free(p);
    return f_peak;
}

double aura_estimate_resp_api(const double *sig, int n, double fs) {
    double mean = 0.0, var = 0.0, scale, total = 0.0, peak = -1.0, f_peak = NAN;
    double *xw = NULL;
    aura_complex *spec = NULL;
    int i, k, nh, need, best = -1;
    if (!sig || n <= 0 || fs <= 0.0) return NAN;
    need = (int)(8.0 * fs);
    if (need < 64) need = 64;
    if (n < need) return NAN;
    for (i = 0; i < n; ++i) mean += sig[i];
    mean /= (double)n;
    for (i = 0; i < n; ++i) {
        double d = sig[i] - mean;
        var += d * d;
    }
    scale = sqrt(var / (double)n);
    if (!(scale > 1e-9)) return NAN;
    xw = (double *)malloc((size_t)n * sizeof(double));
    spec = (aura_complex *)malloc((size_t)n * sizeof(aura_complex));
    if (!xw || !spec) { free(xw); free(spec); return NAN; }
    for (i = 0; i < n; ++i) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(n - 1));
        xw[i] = (sig[i] - mean) * w;
    }
    if (aura_fft(xw, NULL, n, spec) != 0) {
        free(xw); free(spec); return NAN;
    }
    nh = n / 2 + 1;
    {
        int band_first = -1;
        for (k = 0; k < nh; ++k) {
            double f = fs * (double)k / (double)n;
            if (f >= 0.12 && f <= 0.60) {
                double p = spec[k].re * spec[k].re + spec[k].im * spec[k].im;
                if (band_first < 0) band_first = k;
                total += p;
                if (p > peak) { peak = p; best = k; }
            }
        }
        /* 峰值在带首时视为基线漂移 */
        if (best > band_first && total > 0.0 && peak / total >= 0.20)
            f_peak = fs * (double)best / (double)n;
    }
    free(xw); free(spec);
    return f_peak;
}

typedef struct { int idx; double mag; } MagIdx;

static int magcmp(const void *a, const void *b) {
    const MagIdx *x = (const MagIdx *)a, *y = (const MagIdx *)b;
    return (x->mag > y->mag) ? -1 : (x->mag < y->mag ? 1 : 0);
}

void aura_rhc_notch(const double *sig, int n, double fs, double f_resp,
                    double *out) {
    int k, nh, nfreq = 0, ncand = 0;
    double f_lo = 40.0 / 60.0, f_hi = 180.0 / 60.0;
    double cand[16];
    int notch[16];
    aura_complex *half = NULL;
    if (!sig || !out) return;
    if (!isfinite(f_resp) || f_resp <= 0.0 || n < 64) {
        memcpy(out, sig, (size_t)n * sizeof(double));
        return;
    }
    nh = n / 2 + 1;
    half = (aura_complex *)malloc((size_t)nh * sizeof(aura_complex));
    if (!half) {
        memcpy(out, sig, (size_t)n * sizeof(double));
        return;
    }
    if (rfft_real(sig, n, half) != 0) {
        free(half);
        memcpy(out, sig, (size_t)n * sizeof(double));
        return;
    }
    {
        MagIdx *arr = (MagIdx *)malloc((size_t)nh * sizeof(MagIdx));
        int na = 0;
        if (arr) {
            for (k = 0; k < nh; ++k) {
                double f = fs * (double)k / (double)n;
                if (f >= f_lo && f <= f_hi) {
                    arr[na].idx = k;
                    arr[na].mag = half[k].re * half[k].re + half[k].im * half[k].im;
                    na++;
                }
            }
            qsort(arr, (size_t)na, sizeof(MagIdx), magcmp);
            {
                int i, picked = 0;
                for (i = 0; i < na && picked < 3; ++i) {
                    double fc = fs * (double)arr[i].idx / (double)n;
                    int ok = 1, j;
                    for (j = 0; j < ncand; ++j) {
                        if (fabs(fc - cand[j]) * 60.0 < 8.0) { ok = 0; break; }
                    }
                    if (ok) {
                        cand[ncand++] = fc;
                        picked++;
                    }
                }
            }
            free(arr);
        }
    }
    /* ACF 主峰候选 */
    {
        double *acf = (double *)malloc((size_t)n * sizeof(double));
        double mean = 0.0, var = 0.0;
        if (acf) {
            for (k = 0; k < n; ++k) mean += sig[k];
            mean /= (double)n;
            for (k = 0; k < n; ++k) {
                double d = sig[k] - mean;
                var += d * d;
            }
            if (var > 1e-18) {
                int lag_min = (int)(fs / f_hi);
                int lag_max = (int)(fs / f_lo);
                if (lag_max > n - 1) lag_max = n - 1;
                if (lag_min < 1) lag_min = 1;
                if (lag_min < lag_max) {
                    int best = -1;
                    double bestv = -1e300;
                    aura_acf(sig, n, acf);
                    for (k = lag_min; k <= lag_max; ++k) {
                        if (acf[k] > bestv) { bestv = acf[k]; best = k; }
                    }
                    if (best > 0) {
                        double fa = fs / (double)best;
                        int dup = 0, j;
                        for (j = 0; j < ncand; ++j) {
                            if (fabs(fa - cand[j]) * 60.0 < 3.0) dup = 1;
                        }
                        if (!dup && fa >= f_lo && fa <= f_hi && ncand < 8) {
                            cand[ncand++] = fa;
                        }
                    }
                }
            }
            free(acf);
        }
    }
    {
        int kmin = (int)ceil(f_lo / f_resp);
        int kmax = (int)floor(f_hi / f_resp);
        if (kmax > 8) kmax = 8;
        for (k = kmin; k <= kmax; ++k) {
            double fh = k * f_resp;
            double fh_bpm = fh * 60.0;
            int skip = 0, j;
            if (fh_bpm >= 40.0 && fh_bpm <= 60.0) skip = 1;
            if (!skip) {
                for (j = 0; j < ncand; ++j) {
                    if (fabs(fh - cand[j]) * 60.0 < 6.0) { skip = 1; break; }
                }
            }
            if (!skip) notch[nfreq++] = k;
        }
    }
    if (nfreq == 0) {
        memcpy(out, sig, (size_t)n * sizeof(double));
        free(half);
        return;
    }
    for (k = 0; k < nfreq; ++k) {
        double fh = notch[k] * f_resp;
        int j;
        for (j = 0; j < nh; ++j) {
            double f = fs * (double)j / (double)n;
            if (f >= fh - 0.06 && f <= fh + 0.06) {
                half[j].re = 0.0;
                half[j].im = 0.0;
            }
        }
    }
    if (irfft_half(half, n, out) != 0) {
        memcpy(out, sig, (size_t)n * sizeof(double));
    }
    free(half);
}

static double psd_at(const double *p, int nf, double fmax, double f) {
    if (nf <= 0) return 1e-12;
    if (f <= 0.0) return p[0] > 1e-12 ? p[0] : 1e-12;
    if (f >= fmax) return p[nf - 1] > 1e-12 ? p[nf - 1] : 1e-12;
    {
        double pos = f / fmax * (double)(nf - 1);
        int i0 = (int)pos;
        double w = pos - i0;
        double p0 = p[i0] > 1e-12 ? p[i0] : 1e-12;
        double p1 = p[i0 + 1] > 1e-12 ? p[i0 + 1] : 1e-12;
        return p0 + w * (p1 - p0);
    }
}

double aura_estimate_fft(const double *sig, int n, double fs,
                         double f_resp, const double *sos_resp, int ns_resp,
                         double *snr_out) {
    double *x = NULL, *f = NULL, *p = NULL, *band = NULL, *hps = NULL;
    int nperseg, nf, k, band_start = 0, nband = 0, best = -1;
    double bestp = -1e300, f_peak = NAN, bg, snr;
    double f_lo = 40.0 / 60.0, f_hi = 180.0 / 60.0;
    int max_h = 6;
    (void)sos_resp; (void)ns_resp;
    if (!sig || n < 64 || fs <= 0.0) return NAN;
    nperseg = (fs >= 30.0) ? (n < 2048 ? n : 2048) : (n < 256 ? n : 256);
    nf = nperseg / 2 + 1;
    x = (double *)malloc((size_t)n * sizeof(double));
    f = (double *)malloc((size_t)nf * sizeof(double));
    p = (double *)malloc((size_t)nf * sizeof(double));
    band = (double *)malloc((size_t)nf * sizeof(double));
    hps = (double *)malloc((size_t)nf * sizeof(double));
    if (!x || !f || !p || !band || !hps) goto fail;
    {
        double mean = 0.0;
        for (k = 0; k < n; ++k) mean += sig[k];
        mean /= (double)n;
        for (k = 0; k < n; ++k) x[k] = sig[k] - mean;
    }
    if (aura_welch(x, n, fs, nperseg, f, p) < 0) goto fail;
    for (k = 0; k < nf; ++k) {
        if (f[k] >= f_lo && f[k] <= f_hi) {
            if (nband == 0) band_start = k;
            band[nband] = p[k];
            k++;
            nband++;
            k--;
        }
    }
    if (nband == 0) goto fail;
    for (k = 0; k < nband; ++k) {
        double prod = band[k];
        int h;
        for (h = 2; h <= max_h; ++h) {
            double fh = f[band_start + k] * (double)h;
            if (fh > 0.5 * fs) continue;
            prod *= psd_at(p, nf, 0.5 * fs, fh);
        }
        hps[k] = prod;
    }
    {
        double mx = band[0];
        int strong[2048];
        int ns = 0;
        for (k = 1; k < nband; ++k) if (band[k] > mx) mx = band[k];
        for (k = 0; k < nband; ++k) {
            if (band[k] >= 0.15 * mx) strong[ns++] = k;
        }
        if (ns == 0) goto fail;
        /* 呼吸谐波碰撞剔除 */
        if (isfinite(f_resp) && f_resp > 0.0 && ns > 1) {
            int collision[2048], nc = 0, all = 1;
            for (k = 0; k < ns; ++k) {
                double fk = f[band_start + strong[k]];
                int col = 0, kk;
                for (kk = 2; kk <= 6; ++kk) {
                    if (fabs(fk - kk * f_resp) < 3.0 / 60.0) { col = 1; break; }
                }
                collision[k] = col;
                if (!col) all = 0;
                if (col) nc++;
            }
            if (nc > 0 && !all) {
                double best_no = -1e300;
                for (k = 0; k < ns; ++k) {
                    if (!collision[k] && hps[strong[k]] > best_no)
                        best_no = hps[strong[k]];
                }
                {
                    int keepn = 0;
                    for (k = 0; k < ns; ++k) {
                        if (!collision[k] || hps[strong[k]] > 10.0 * best_no)
                            strong[keepn++] = strong[k];
                    }
                    ns = keepn;
                }
            }
        }
        for (k = 0; k < ns; ++k) {
            int bi = strong[k];
            if (band[bi] > bestp) { bestp = band[bi]; best = bi; }
        }
        if (best < 0) goto fail;
        {
            int g = band_start + best;
            f_peak = f[g];
            if (g > 0 && g < nf - 1) {
                double bgi = median_copy(band, nband);
                double y0 = log(fmax(p[g - 1] - bgi, 1e-15));
                double y1 = log(fmax(p[g] - bgi, 1e-15));
                double y2 = log(fmax(p[g + 1] - bgi, 1e-15));
                double den = y0 - 2.0 * y1 + y2;
                if (fabs(den) > 1e-12) {
                    double delta = 0.5 * (y0 - y2) / den;
                    if (fabs(delta) <= 0.5)
                        f_peak = f[g] + delta * (f[g + 1] - f[g]);
                }
            }
            bg = median_copy(band, nband);
            snr = bestp / (bg + 1e-12);
            if (snr_out) *snr_out = snr;
        }
    }
    free(x); free(f); free(p); free(band); free(hps);
    if (f_peak <= 0.05) return NAN;
    return f_peak * 60.0;

fail:
    free(x); free(f); free(p); free(band); free(hps);
    return NAN;
}

/* ACF 边界峰做线性折扣。 */
#define ACF_EDGE_MARGIN     3
#define ACF_EDGE_MIN_FACTOR 0.55
/* 相关峰高低于该值不参与融合。 */
#define ACF_MIN_HEIGHT      0.25

double aura_estimate_acf(const double *sig, int n, double fs, double *height) {
    double *acf = NULL, bestv = -1e300;
    int lag_min, lag_max, k, best = -1;
    if (!sig || n < 64 || fs <= 0.0) return NAN;
    acf = (double *)malloc((size_t)n * sizeof(double));
    if (!acf) return NAN;
    aura_acf(sig, n, acf);
    lag_min = (int)(fs / 3.0);
    lag_max = (int)(fs / 0.6666666667);
    if (lag_min < 1) lag_min = 1;
    if (lag_max > n - 1) lag_max = n - 1;
    if (lag_max <= lag_min) { free(acf); return NAN; }
    for (k = lag_min; k <= lag_max; ++k) {
        double value = acf[k];
        int dmin = k - lag_min;
        int dmax = lag_max - k;
        int edge = dmin < dmax ? dmin : dmax;
        if (edge < ACF_EDGE_MARGIN) {
            value *= ACF_EDGE_MIN_FACTOR +
                (1.0 - ACF_EDGE_MIN_FACTOR) * (double)edge / ACF_EDGE_MARGIN;
        }
        if (value > bestv) { bestv = value; best = k; }
    }
    free(acf);
    if (best < 0 || bestv <= 0.0) return NAN;
    if (bestv < ACF_MIN_HEIGHT) return NAN;
    if (height) *height = bestv;
    return 60.0 * fs / (double)best;
}

int aura_estimate_hr(const double *sig, int n, double fs, double f_resp_in,
                     int enable_acf,
                     const double *sos_resp, int ns_resp,
                     double *out_f_resp, double *out_fft,
                     double *out_acf, double *out_snr_fft) {
    double f_resp, *notched = NULL, fft, acf = NAN, snr = 0.0;
    if (!sig || n <= 0) return -1;
    f_resp = f_resp_in;
    if (!isfinite(f_resp) || f_resp <= 0.0) {
        f_resp = aura_estimate_resp(sig, n, fs, sos_resp, ns_resp);
    }
    notched = (double *)malloc((size_t)n * sizeof(double));
    if (!notched) return -1;
    aura_rhc_notch(sig, n, fs, f_resp, notched);
    fft = aura_estimate_fft(notched, n, fs, f_resp, sos_resp, ns_resp, &snr);
    if (enable_acf) acf = aura_estimate_acf(notched, n, fs, NULL);
    if (out_f_resp) *out_f_resp = f_resp;
    if (out_fft) *out_fft = fft;
    if (out_acf) *out_acf = acf;
    if (out_snr_fft) *out_snr_fft = snr;
    free(notched);
    return 0;
}

/* PPI 三路估计 */

double aura_estimate_ppi_event(const double *sig, int n, double fs,
                               int *n_peaks) {
    aura_event_result ev;
    double hr = NAN;
    if (n_peaks) *n_peaks = 0;
    if (!sig || n < 64 || fs <= 0.0) return NAN;
    if (aura_event_detect(sig, n, fs, 0.0, &ev) != 0) return NAN;
    if (n_peaks) *n_peaks = ev.n_events;
    if (ev.n_ibis >= 2) {
        double med = median_copy(ev.ibis_ms, ev.n_ibis);
        if (med > 0.0) hr = 60000.0 / med;
    }
    aura_event_free(&ev);
    return hr;
}

double aura_estimate_ppi_template(const double *sig, int n, double fs,
                                  int *n_peaks) {
    double *x = NULL, *acf = NULL, *tpl = NULL, *corr = NULL;
    double mean = 0.0, var = 0.0, stdv, tpl_norm, cmax;
    int i, k, j, lag_min, lag_max, P = 0, n_full, corr_len, np;
    int *peaks = NULL, *beats = NULL;
    double hr = NAN;
    if (n_peaks) *n_peaks = 0;
    if (!sig || n < 64 || fs <= 0.0) return NAN;
    x = (double *)malloc((size_t)n * sizeof(double));
    acf = (double *)malloc((size_t)n * sizeof(double));
    if (!x || !acf) { free(x); free(acf); return NAN; }
    for (i = 0; i < n; ++i) mean += sig[i];
    mean /= (double)n;
    for (i = 0; i < n; ++i) { x[i] = sig[i] - mean; var += x[i] * x[i]; }
    stdv = sqrt(var / (double)n);
    if (!(stdv > 1e-12)) { free(x); free(acf); return NAN; }
    aura_acf(sig, n, acf);
    lag_min = (int)(0.333 * fs);
    if (lag_min < 1) lag_min = 1;
    lag_max = (int)(1.5 * fs);
    if (lag_max > n - 1) lag_max = n - 1;
    if (lag_max <= lag_min) { free(x); free(acf); return NAN; }
    P = lag_min;
    for (k = lag_min + 1; k <= lag_max; ++k)
        if (acf[k] > acf[P]) P = k;
    n_full = n / P;
    if (n_full < 2) { free(x); free(acf); return NAN; }
    tpl = (double *)calloc((size_t)P, sizeof(double));
    if (!tpl) { free(x); free(acf); return NAN; }
    for (k = 0; k < n_full; ++k)
        for (j = 0; j < P; ++j) tpl[j] += x[k * P + j];
    for (j = 0; j < P; ++j) tpl[j] /= (double)n_full;
    mean = 0.0;
    for (j = 0; j < P; ++j) mean += tpl[j];
    mean /= (double)P;
    tpl_norm = 0.0;
    for (j = 0; j < P; ++j) { tpl[j] -= mean; tpl_norm += tpl[j] * tpl[j]; }
    tpl_norm = sqrt(tpl_norm);
    if (!(tpl_norm > 1e-12)) {
        free(x); free(acf); free(tpl); return NAN;
    }
    for (j = 0; j < P; ++j) tpl[j] /= tpl_norm;
    corr_len = n - P + 1;
    corr = (double *)malloc((size_t)corr_len * sizeof(double));
    peaks = (int *)malloc((size_t)(corr_len / 2 + 2) * sizeof(int));
    if (!corr || !peaks) {
        free(x); free(acf); free(tpl); free(corr); free(peaks); return NAN;
    }
    for (k = 0; k < corr_len; ++k) {
        double s = 0.0;
        for (j = 0; j < P; ++j) s += x[k + j] * tpl[j];
        corr[k] = s / stdv;
    }
    cmax = corr[0];
    for (k = 1; k < corr_len; ++k) if (corr[k] > cmax) cmax = corr[k];
    {
        double min_dist = (double)((int)(0.333 * fs));
        double dist2 = 0.8 * (double)P;
        double d = (dist2 > min_dist) ? dist2 : min_dist;
        double h = 0.35 * cmax;
        if (h < 0.30) h = 0.30;
        np = aura_find_peaks(corr, corr_len, d, h, 0.12, peaks,
                             corr_len / 2 + 2, NULL, NULL);
        if (np < 2) {
            double h2 = 0.25 * cmax;
            if (h2 < 0.20) h2 = 0.20;
            np = aura_find_peaks(corr, corr_len, d, h2, -1e308, peaks,
                                 corr_len / 2 + 2, NULL, NULL);
        }
    }
    if (np >= 2) {
        int refine = (int)(0.05 * fs), nb = 0;
        beats = (int *)malloc((size_t)np * sizeof(int));
        if (!beats) { np = 0; }
        if (beats) {
            for (i = 0; i < np; ++i) {
                int cp = peaks[i];
                int lo = cp - refine, hi = cp + refine, bi = -1;
                double best = -1e300;
                if (lo < 0) lo = 0;
                if (hi > n - 1) hi = n - 1;
                for (k = lo; k <= hi; ++k)
                    if (x[k] > best) { best = x[k]; bi = k; }
                if (bi >= 0) beats[nb++] = bi;
            }
            /* 排序并去重 */
            for (i = 1; i < nb; ++i) {
                int v = beats[i], j2 = i - 1;
                while (j2 >= 0 && beats[j2] > v) { beats[j2 + 1] = beats[j2]; --j2; }
                beats[j2 + 1] = v;
            }
            {
                int nu = 0;
                for (i = 0; i < nb; ++i)
                    if (nu == 0 || beats[i] != beats[nu - 1]) beats[nu++] = beats[i];
                nb = nu;
            }
            if (nb >= 2) {
                double *ibi = (double *)malloc((size_t)(nb - 1) * sizeof(double));
                double *valid = (double *)malloc((size_t)(nb - 1) * sizeof(double));
                int nv = 0;
                if (ibi && valid) {
                    for (i = 1; i < nb; ++i)
                        ibi[i - 1] = (double)(beats[i] - beats[i - 1]) / fs * 1000.0;
                    for (i = 0; i < nb - 1; ++i)
                        if (ibi[i] >= 333.0 && ibi[i] <= 1500.0)
                            valid[nv++] = ibi[i];
                    if (nv >= 2 && n_peaks) *n_peaks = nb;
                    if (nv >= 2) hr = 60000.0 / median_copy(valid, nv);
                }
                free(ibi); free(valid);
            }
        }
    }
    free(x); free(acf); free(tpl); free(corr); free(peaks); free(beats);
    return hr;
}

double aura_estimate_ppi_findpeaks(const double *sig, int n, double fs,
                                   int *n_peaks) {
    double mean = 0.0, var = 0.0, stdv, hr = NAN;
    int i, np, *peaks = NULL;
    if (n_peaks) *n_peaks = 0;
    if (!sig || n < 64 || fs <= 0.0) return NAN;
    for (i = 0; i < n; ++i) mean += sig[i];
    mean /= (double)n;
    for (i = 0; i < n; ++i) {
        double d = sig[i] - mean;
        var += d * d;
    }
    stdv = sqrt(var / (double)n);
    peaks = (int *)malloc((size_t)(n / 2 + 2) * sizeof(int));
    if (!peaks) return NAN;
    {
        double min_dist = (double)((int)(0.333 * fs));
        if (min_dist < 1.0) min_dist = 1.0;
        np = aura_find_peaks(sig, n, min_dist, stdv * 0.5, -1e308,
                             peaks, n / 2 + 2, NULL, NULL);
    }
    if (np >= 2) {
        double *ibi = (double *)malloc((size_t)(np - 1) * sizeof(double));
        double *valid = (double *)malloc((size_t)(np - 1) * sizeof(double));
        int nv = 0;
        if (ibi && valid) {
            for (i = 1; i < np; ++i)
                ibi[i - 1] = (double)(peaks[i] - peaks[i - 1]) / fs * 1000.0;
            for (i = 0; i < np - 1; ++i)
                if (ibi[i] >= 333.0 && ibi[i] <= 1500.0) valid[nv++] = ibi[i];
            if (nv >= 2) {
                if (n_peaks) *n_peaks = np;
                hr = 60000.0 / median_copy(valid, nv);
            }
        }
        free(ibi); free(valid);
    }
    free(peaks);
    return hr;
}

double aura_estimate_ppi(const double *sig, int n, double fs,
                         int *n_peaks, int *source) {
    int nk = 0;
    double hr;
    if (n_peaks) *n_peaks = 0;
    if (source) *source = -1;
    hr = aura_estimate_ppi_event(sig, n, fs, &nk);
    if (isfinite(hr)) {
        if (n_peaks) *n_peaks = nk;
        if (source) *source = 0;
        return hr;
    }
    hr = aura_estimate_ppi_template(sig, n, fs, &nk);
    if (isfinite(hr)) {
        if (n_peaks) *n_peaks = nk;
        if (source) *source = 1;
        return hr;
    }
    hr = aura_estimate_ppi_findpeaks(sig, n, fs, &nk);
    if (isfinite(hr)) {
        if (n_peaks) *n_peaks = nk;
        if (source) *source = 2;
        return hr;
    }
    return NAN;
}

/* 三路融合 */

static double clipd(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

int aura_estimate_all(const double *sig, int n, double fs, double f_resp_in,
                      int enable_acf,
                      const double *sos_resp, int ns_resp,
                      aura_hr_result *out) {
    double *notched = NULL;
    double f_resp, fft, acf = NAN, ppi = NAN, acf_h = 0.0, snr = 0.0;
    double weights[3], hrs[3], max_diff = NAN, fused = NAN;
    int npeaks = 0, source = -1, nv = 0, i, consistent = 0;
    if (!sig || n <= 0 || fs <= 0.0 || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->f_resp = NAN;
    out->hr_fft = NAN;
    out->hr_acf = NAN;
    out->hr_ppi = NAN;
    out->hr_fused = NAN;
    out->max_diff_bpm = NAN;
    out->ppi_source = -1;
    f_resp = f_resp_in;
    if (!isfinite(f_resp) || f_resp <= 0.0)
        f_resp = aura_estimate_resp(sig, n, fs, sos_resp, ns_resp);
    notched = (double *)malloc((size_t)n * sizeof(double));
    if (!notched) return -1;
    aura_rhc_notch(sig, n, fs, f_resp, notched);
    fft = aura_estimate_fft(notched, n, fs, f_resp, sos_resp, ns_resp, &snr);
    if (enable_acf) acf = aura_estimate_acf(notched, n, fs, &acf_h);
    ppi = aura_estimate_ppi(notched, n, fs, &npeaks, &source);
    /* 生理范围过滤 */
    if (!(acf >= 40.0 && acf <= 180.0)) acf = NAN;
    if (!(ppi >= 40.0 && ppi <= 180.0)) ppi = NAN;
    if (isfinite(fft)) hrs[nv++] = fft;
    if (isfinite(acf)) hrs[nv++] = acf;
    if (isfinite(ppi)) hrs[nv++] = ppi;
    if (nv >= 2) {
        double mn = hrs[0], mx = hrs[0];
        for (i = 1; i < nv; ++i) {
            if (hrs[i] < mn) mn = hrs[i];
            if (hrs[i] > mx) mx = hrs[i];
        }
        max_diff = mx - mn;
        consistent = (max_diff < 8.0);
    }
    weights[0] = isfinite(fft) ? clipd(snr / 5.0, 0.1, 1.0) : 0.0;
    weights[1] = isfinite(acf) ? clipd(acf_h, 0.1, 1.0) : 0.0;
    /* 峰数少时压低 PPI 权重 */
    weights[2] = isfinite(ppi) ? clipd(((double)npeaks - 4.0) / 12.0, 0.05, 1.0) : 0.0;
    if ((weights[0] + weights[1] + weights[2]) > 0.0 && consistent) {
        double num = 0.0, den = 0.0;
        if (isfinite(fft) && weights[0] > 0.0) { num += weights[0] * fft; den += weights[0]; }
        if (isfinite(acf) && weights[1] > 0.0) { num += weights[1] * acf; den += weights[1]; }
        if (isfinite(ppi) && weights[2] > 0.0) { num += weights[2] * ppi; den += weights[2]; }
        if (den > 0.0) fused = num / den;
    } else if (fs < 30.0) {
        if (nv >= 2) {
            double spread = max_diff;
            double med = median_copy(hrs, nv);
            if (spread <= 40.0) fused = med;
        }
    } else if (nv >= 2) {
        /* 不一致时优先 FFT/ACF 均值 */
        double med = median_copy(hrs, nv);
        if (nv == 3 && isfinite(fft) && isfinite(acf)
                && fabs(fft - acf) <= 10.0
                && fabs(ppi - 0.5 * (fft + acf)) > 8.0) {
            fused = 0.5 * (fft + acf);
        } else {
            fused = med;
        }
    } else if (isfinite(ppi)) {
        fused = ppi;
    } else if (isfinite(acf)) {
        fused = acf;
    } else if (isfinite(fft)) {
        fused = fft;
    }
    out->f_resp = f_resp;
    out->hr_fft = fft;
    out->hr_acf = acf;
    out->hr_ppi = ppi;
    out->hr_fused = fused;
    out->snr_fft = snr;
    out->acf_height = acf_h;
    out->max_diff_bpm = max_diff;
    out->consistent = consistent;
    out->n_ppi_peaks = npeaks;
    out->ppi_source = source;
    out->w_fft = weights[0];
    out->w_acf = weights[1];
    out->w_ppi = weights[2];
    free(notched);
    return 0;
}
