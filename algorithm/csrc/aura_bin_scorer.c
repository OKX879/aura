/* Bin 质量评分实现。 */
#include "aura_bin_scorer.h"
#include "aura_kernels.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define W_ENERGY 0.10
#define W_AMP 0.05
#define W_PHASE 0.15
#define W_COH 0.20
#define W_PERIOD 0.10
#define W_SNR 0.00
#define W_JUMP 0.05
#define W_MOTION 0.05
#define W_DIP 0.25

static int dcmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* 排序后取中位数。 */
static double median_inplace(double *buf, int m) {
    if (m <= 0) return 0.0;
    qsort(buf, (size_t)m, sizeof(double), dcmp);
    if (m % 2) return buf[m / 2];
    return 0.5 * (buf[m / 2 - 1] + buf[m / 2]);
}

static double median_copy(const double *x, int m) {
    double *buf = (double *)malloc((size_t)(m > 0 ? m : 1) * sizeof(double));
    double v;
    if (!buf) return 0.0;
    memcpy(buf, x, (size_t)m * sizeof(double));
    v = median_inplace(buf, m);
    free(buf);
    return v;
}

static void unwrap_phase(const double *p, int n, double *out) {
    int i;
    if (n <= 0) return;
    out[0] = p[0];
    for (i = 1; i < n; ++i) {
        double d = p[i] - p[i - 1];
        double ddmod = fmod(d + M_PI, 2.0 * M_PI);
        double corr;
        if (ddmod < 0.0) ddmod += 2.0 * M_PI;
        ddmod -= M_PI;
        if (fabs(ddmod + M_PI) < 1e-15 && d > 0.0) ddmod = M_PI;
        corr = (fabs(d) >= M_PI) ? (ddmod - d) : 0.0;
        out[i] = out[i - 1] + d + corr;
    }
}

static void dilate_bad(unsigned char *bad, int n, int iterations) {
    unsigned char *tmp = (unsigned char *)malloc((size_t)n);
    int it, i;
    if (!tmp) return;
    for (it = 0; it < iterations; ++it) {
        for (i = 0; i < n; ++i) {
            int l = i > 0 ? bad[i - 1] : 0;
            int r = i + 1 < n ? bad[i + 1] : 0;
            tmp[i] = (unsigned char)(bad[i] || l || r);
        }
        memcpy(bad, tmp, (size_t)n);
    }
    free(tmp);
}

static double clip01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

int aura_score_bin(const double *zr, const double *zi, int n, int c,
                   int range_bin, double fs,
                   const double *sos_cardiac, int ns_cardiac,
                   const double *sos_motion, int ns_motion,
                   aura_bin_quality *out) {
    int i, ci, m, k, n_active, n_pairs;
    double *amp = NULL, *ampbuf = NULL, *medbuf = NULL;
    double *phase = NULL, *unw = NULL, *disp = NULL, *card = NULL;
    double *motion = NULL, *med_signal = NULL, *corr_vals = NULL;
    double *f = NULL, *p = NULL, *band = NULL;
    double abs_med, med_amp, med_amp_sq, cv, med_motion;
    double valid_count = 0, jump_count = 0, total = (double)n * c;
    double nyq = 0.5 * fs;
    int nf;

    if (!zr || !zi || !out || n <= 0 || c <= 0) return -1;
    memset(out, 0, sizeof(*out));
    out->range_bin = range_bin;
    out->n_valid_channels = c;

    m = n * c;
    amp = (double *)malloc((size_t)m * sizeof(double));
    ampbuf = (double *)malloc((size_t)m * sizeof(double));
    if (!amp || !ampbuf) goto fail;
    for (i = 0; i < m; ++i) {
        amp[i] = sqrt(zr[i] * zr[i] + zi[i] * zi[i]);
    }
    abs_med = median_copy(amp, m);
    out->abs_median = abs_med;
    if (abs_med < 1e-3) {
        free(amp); free(ampbuf);
        return 0;
    }

    /* Q_E */
    for (i = 0; i < m; ++i) ampbuf[i] = amp[i] * amp[i];
    med_amp_sq = median_copy(ampbuf, m);
    out->energy = clip01(0.5 + 0.1 * log10(med_amp_sq + 1e-6));

    /* Q_A */
    med_amp = median_copy(amp, m);
    for (i = 0; i < m; ++i) ampbuf[i] = fabs(amp[i] - med_amp);
    cv = median_copy(ampbuf, m) / (med_amp + 1e-6);
    out->amp_stability = exp(-cv);

    /* 相位展开 */
    phase = (double *)malloc((size_t)m * sizeof(double));
    unw = (double *)malloc((size_t)n * sizeof(double));
    if (!phase || !unw) goto fail;
    for (ci = 0; ci < c; ++ci) {
        for (i = 0; i < n; ++i) {
            int idx = i * c + ci;
            phase[i * c + ci] = atan2(zi[idx], zr[idx]);
        }
    }
    /* 逐通道展开 */
    for (ci = 0; ci < c; ++ci) {
        for (i = 0; i < n; ++i) unw[i] = phase[i * c + ci];
        unwrap_phase(unw, n, unw);
        for (i = 0; i < n; ++i) phase[i * c + ci] = unw[i];
    }

    /* Q_P / Q_J */
    for (ci = 0; ci < c; ++ci) {
        unsigned char *bad = (unsigned char *)calloc((size_t)n, 1);
        double med_c, floor_c;
        int vc = 0, jc = 0;
        if (!bad) goto fail;
        for (i = 0; i < n; ++i) ampbuf[i] = amp[i * c + ci];
        med_c = median_copy(ampbuf, n);
        floor_c = 0.3 * (med_c > 0.0 ? med_c : 1.0);
        for (i = 0; i < n; ++i) {
            if (ampbuf[i] < floor_c) bad[i] = 1;
        }
        for (i = 1; i < n; ++i) {
            double d1 = phase[i * c + ci] - phase[(i - 1) * c + ci];
            if (fabs(d1) > 2.0) {
                bad[i] = 1;
                jc++;
            }
        }
        for (i = 1; i < n - 1; ++i) {
            double d1 = phase[i * c + ci] - phase[(i - 1) * c + ci];
            double d2 = phase[(i + 1) * c + ci] - 2.0 * phase[i * c + ci]
                        + phase[(i - 1) * c + ci];
            (void)d1;
            if (fabs(d2) > 0.7) bad[i] = 1;
        }
        dilate_bad(bad, n, 2);
        for (i = 0; i < n; ++i) if (!bad[i]) vc++;
        valid_count += (double)vc;
        jump_count += (double)jc;
        free(bad);
    }
    out->phase_valid_ratio = valid_count / (total > 1.0 ? total : 1.0);
    out->jump_ratio = jump_count / (total > 1.0 ? total : 1.0);

    /* 心搏带滤波 */
    card = (double *)calloc((size_t)m, sizeof(double));
    disp = (double *)malloc((size_t)n * sizeof(double));
    if (!card || !disp) goto fail;
    if (nyq > 3.0 && n > 27) {
        double *tmp = (double *)malloc((size_t)n * sizeof(double));
        if (!tmp) goto fail;
        for (ci = 0; ci < c; ++ci) {
            for (i = 0; i < n; ++i) disp[i] = phase[i * c + ci];
            aura_detrend_linear(disp, n, disp);
            {
                double mean = 0.0;
                for (i = 0; i < n; ++i) {
                    disp[i] = disp[i] * 5.0 / (4.0 * M_PI);
                    mean += disp[i];
                }
                mean /= (double)n;
                for (i = 0; i < n; ++i) disp[i] -= mean;
            }
            if (aura_sosfiltfilt(sos_cardiac, ns_cardiac, disp, n, tmp, -1) == 0) {
                for (i = 0; i < n; ++i) card[i * c + ci] = tmp[i];
            }
        }
        free(tmp);
    }

    /* 通道中位信号 */
    med_signal = (double *)malloc((size_t)n * sizeof(double));
    medbuf = (double *)malloc((size_t)(c > 0 ? c : 1) * sizeof(double));
    if (!med_signal || !medbuf) goto fail;
    for (i = 0; i < n; ++i) {
        for (ci = 0; ci < c; ++ci) medbuf[ci] = card[i * c + ci];
        med_signal[i] = median_inplace(medbuf, c);
    }

    /* Q_C 相干 */
    {
        int *active = (int *)malloc((size_t)c * sizeof(int));
        double *means = (double *)malloc((size_t)c * sizeof(double));
        double *stds = (double *)malloc((size_t)c * sizeof(double));
        n_active = 0;
        if (!active || !means || !stds) {
            free(active); free(means); free(stds);
            goto fail;
        }
        for (ci = 0; ci < c; ++ci) {
            double mean = 0.0, var = 0.0;
            for (i = 0; i < n; ++i) mean += card[i * c + ci];
            mean /= (double)n;
            for (i = 0; i < n; ++i) {
                double d = card[i * c + ci] - mean;
                var += d * d;
            }
            var /= (double)n;
            means[ci] = mean;
            stds[ci] = sqrt(var);
            if (stds[ci] > 1e-9) active[n_active++] = ci;
        }
        if (n_active >= 2) {
            n_pairs = n_active * (n_active - 1) / 2;
            corr_vals = (double *)malloc((size_t)n_pairs * sizeof(double));
            if (corr_vals) {
                int a, b, q = 0;
                for (a = 0; a < n_active; ++a) {
                    for (b = a + 1; b < n_active; ++b) {
                        int ca = active[a], cb = active[b];
                        double dot = 0.0;
                        for (i = 0; i < n; ++i) {
                            dot += (card[i * c + ca] - means[ca])
                                   * (card[i * c + cb] - means[cb]);
                        }
                        dot /= (double)n;
                        corr_vals[q++] = dot / (stds[ca] * stds[cb] + 1e-30);
                    }
                }
                for (k = 0; k < n_pairs; ++k) {
                    if (corr_vals[k] < 0.0) corr_vals[k] = 0.0;
                }
                out->coherence = median_copy(corr_vals, n_pairs);
            }
        }
        free(active); free(means); free(stds);
    }

    /* Q_H 周期性 + ACF HR */
    {
        double mean = 0.0, std = 0.0;
        double *acf = (double *)malloc((size_t)n * sizeof(double));
        for (i = 0; i < n; ++i) mean += med_signal[i];
        mean /= (double)n;
        for (i = 0; i < n; ++i) {
            double d = med_signal[i] - mean;
            std += d * d;
        }
        std = sqrt(std / (double)n);
        if (std > 1e-6 && acf) {
            int lag_min = (int)(fs / 3.0);
            int lag_max = (int)(fs / 0.7);
            int best = -1;
            double bestv = -1e300;
            aura_acf(med_signal, n, acf);
            if (lag_min < 1) lag_min = 1;
            if (lag_max > n - 1) lag_max = n - 1;
            for (k = lag_min; k <= lag_max; ++k) {
                if (acf[k] > bestv) {
                    bestv = acf[k];
                    best = k;
                }
            }
            if (best >= 0) {
                out->periodicity = bestv > 0.0 ? bestv : 0.0;
                if (bestv > 0.0) out->hr_acf_bpm = 60.0 * fs / (double)best;
            }
        }
        free(acf);
    }

    /* Q_S 谱峰清晰度 */
    {
        double mean = 0.0, std = 0.0;
        for (i = 0; i < n; ++i) mean += med_signal[i];
        mean /= (double)n;
        for (i = 0; i < n; ++i) {
            double d = med_signal[i] - mean;
            std += d * d;
        }
        std = sqrt(std / (double)n);
        if (n >= 128 && std > 1e-6) {
            int nperseg = n < 2048 ? n : 2048;
            int cap = n / 2 + 2;
            double *sig = (double *)malloc((size_t)n * sizeof(double));
            double hi = (nyq < 3.0) ? nyq : 3.0;
            f = (double *)malloc((size_t)cap * sizeof(double));
            p = (double *)malloc((size_t)cap * sizeof(double));
            band = (double *)malloc((size_t)cap * sizeof(double));
            if (sig && f && p && band) {
                for (i = 0; i < n; ++i) sig[i] = med_signal[i] - mean;
                nf = aura_welch(sig, n, fs, nperseg, f, p);
                if (nf > 0) {
                    int nb = 0;
                    for (k = 0; k < nf; ++k) {
                        if (f[k] >= 0.7 && f[k] <= hi) band[nb++] = p[k];
                    }
                    if (nb > 0) {
                        double mx = band[0];
                        double medp;
                        for (k = 1; k < nb; ++k) if (band[k] > mx) mx = band[k];
                        medp = median_copy(band, nb);
                        if (mx > 0.0) {
                            double snr = mx / (medp + 1e-12);
                            out->spectral_snr = clip01(snr / (snr + 2.0));
                        }
                    }
                }
                free(sig);
            }
            free(f); free(p); free(band);
            f = p = band = NULL;
        }
    }

    /* Q_M 运动惩罚 */
    motion = (double *)calloc((size_t)m, sizeof(double));
    if (!motion) goto fail;
    if (nyq > 10.0 && n > 27) {
        double *tmp = (double *)malloc((size_t)n * sizeof(double));
        double *energies = (double *)malloc((size_t)c * sizeof(double));
        if (tmp && energies) {
            for (ci = 0; ci < c; ++ci) {
                for (i = 0; i < n; ++i) disp[i] = phase[i * c + ci];
                aura_detrend_linear(disp, n, disp);
                {
                    double mean = 0.0;
                    for (i = 0; i < n; ++i) {
                        disp[i] = disp[i] * 5.0 / (4.0 * M_PI);
                        mean += disp[i];
                    }
                    mean /= (double)n;
                    for (i = 0; i < n; ++i) disp[i] -= mean;
                }
                if (aura_sosfiltfilt(sos_motion, ns_motion, disp, n, tmp, -1) == 0) {
                    double e = 0.0;
                    for (i = 0; i < n; ++i) e += tmp[i] * tmp[i];
                    energies[ci] = sqrt(e / (double)n);
                } else {
                    energies[ci] = 0.0;
                }
            }
            med_motion = median_copy(energies, c);
            out->motion_score = clip01(med_motion / 0.5);
        }
        free(tmp); free(energies);
    }

    /* Q_D 零点质控 */
    {
        double *dips = (double *)malloc((size_t)c * sizeof(double));
        int nd = 0;
        if (dips) {
            for (ci = 0; ci < c; ++ci) {
                double med_c;
                int cnt = 0;
                for (i = 0; i < n; ++i) ampbuf[i] = amp[i * c + ci];
                med_c = median_copy(ampbuf, n);
                if (med_c <= 1e-3) continue;
                for (i = 0; i < n; ++i) {
                    if (amp[i * c + ci] < 0.3 * med_c) cnt++;
                }
                dips[nd++] = (double)cnt / (double)n;
            }
            out->dip_ratio = nd > 0 ? median_copy(dips, nd) : 0.0;
            free(dips);
        }
    }

    /* 总分 */
    {
        double dip_term = clip01(out->dip_ratio / 0.05);
        out->total_score = clip01(
            W_ENERGY * out->energy
            + W_AMP * out->amp_stability
            + W_PHASE * out->phase_valid_ratio
            + W_COH * out->coherence
            + W_PERIOD * out->periodicity
            + W_SNR * out->spectral_snr
            - W_JUMP * out->jump_ratio
            - W_MOTION * out->motion_score
            - W_DIP * dip_term);
    }

    free(amp); free(ampbuf); free(phase); free(unw); free(disp);
    free(card); free(motion); free(med_signal); free(medbuf);
    free(corr_vals);
    return 0;

fail:
    free(amp); free(ampbuf); free(phase); free(unw); free(disp);
    free(card); free(motion); free(med_signal); free(medbuf);
    free(corr_vals); free(f); free(p); free(band);
    return -1;
}

int aura_score_window(const double *zr, const double *zi, int n, int c, int r,
                      double fs,
                      const double *sos_cardiac, int ns_cardiac,
                      const double *sos_motion, int ns_motion,
                      aura_bin_quality *out) {
    int ri;
    double *br = (double *)malloc((size_t)n * c * sizeof(double));
    double *bi = (double *)malloc((size_t)n * c * sizeof(double));
    if (!br || !bi) {
        free(br); free(bi);
        return -1;
    }
    for (ri = 0; ri < r; ++ri) {
        int i, ci;
        for (i = 0; i < n; ++i) {
            for (ci = 0; ci < c; ++ci) {
                size_t src = ((size_t)i * c + ci) * r + ri;
                size_t dst = (size_t)i * c + ci;
                br[dst] = zr[src];
                bi[dst] = zi[src];
            }
        }
        if (aura_score_bin(br, bi, n, c, ri, fs, sos_cardiac, ns_cardiac,
                           sos_motion, ns_motion, &out[ri]) != 0) {
            free(br); free(bi);
            return -1;
        }
    }
    free(br); free(bi);
    return 0;
}
