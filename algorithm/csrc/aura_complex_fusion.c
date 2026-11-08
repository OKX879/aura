/* 复数通道融合实现。 */
#include "aura_complex_fusion.h"
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

static double clip01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

int aura_fuse_window(const double *zr, const double *zi, int n, int c, int r,
                     const double *bin_weights, int center_bin, double fs,
                     double ref_channel, int neighborhood,
                     double sigma_r, double beta,
                     double coherence_threshold,
                     double channel_coh_power, double bin_coh_power,
                     double bin_coh_min, double amp_floor_frac,
                     const double *sos_cardiac, int ns_cardiac,
                     double *out_r, double *out_i, double *coh_mat,
                     unsigned char *frame_valid, aura_fuse_stats *stats) {
    int m = n * c * r;
    int i, ci, ri, k;
    int *bin_indices = NULL;
    int n_bins = 0, r0 = center_bin, ref_c = (int)ref_channel;
    double *ar = NULL, *ai = NULL;       /* 对齐后的复数 */
    double *disp = NULL;                 /* 心搏带位移 */
    double *disp_ref = NULL;
    double *bin_scales = NULL, *bin_coh = NULL, *effective_w = NULL;
    double *tmp = NULL, *ampbuf = NULL, *medbuf = NULL;
    double *zf_r = NULL, *zf_i = NULL;
    int *active_count = NULL;
    double std_ref = 0.0, scale_r0 = 1.0, total_weight = 0.0;
    int use_cardiac;
    if (!zr || !zi || !bin_weights || !out_r || !out_i || !coh_mat
            || n <= 0 || c <= 0 || r <= 0 || r0 < 0 || r0 >= r
            || ref_c < 0 || ref_c >= c) return -1;

    bin_indices = (int *)malloc((size_t)r * sizeof(int));
    ar = (double *)malloc((size_t)m * sizeof(double));
    ai = (double *)malloc((size_t)m * sizeof(double));
    bin_scales = (double *)malloc((size_t)r * sizeof(double));
    bin_coh = (double *)calloc((size_t)r, sizeof(double));
    effective_w = (double *)calloc((size_t)r, sizeof(double));
    zf_r = (double *)calloc((size_t)n, sizeof(double));
    zf_i = (double *)calloc((size_t)n, sizeof(double));
    active_count = (int *)calloc((size_t)c, sizeof(int));
    tmp = (double *)malloc((size_t)n * sizeof(double));
    ampbuf = (double *)malloc((size_t)m * sizeof(double));
    medbuf = (double *)malloc((size_t)(c > 0 ? c : 1) * sizeof(double));
    if (!bin_indices || !ar || !ai || !bin_scales || !bin_coh
            || !effective_w || !zf_r || !zf_i || !active_count || !tmp
            || !ampbuf || !medbuf) goto fail;

    for (ri = 0; ri < r; ++ri) {
        if (abs(ri - r0) <= neighborhood && bin_weights[ri] > 0.0) {
            bin_indices[n_bins++] = ri;
        }
    }
    if (n_bins == 0) {
        bin_indices[n_bins++] = r0;
    }

    /* ---- 心搏带位移 ---- */
    use_cardiac = (fs > 0.0) && (n > 27);
    disp = (double *)malloc((size_t)m * sizeof(double));
    if (!disp) goto fail;
    if (use_cardiac) {
        for (ri = 0; ri < r; ++ri) {
            for (ci = 0; ci < c; ++ci) {
                size_t base = ((size_t)0 * c + ci) * r + ri;
                for (i = 0; i < n; ++i) tmp[i] = atan2(zi[base + (size_t)i * c * r],
                                                       zr[base + (size_t)i * c * r]);
                aura_unwrap_phase(tmp, n, tmp);
                aura_detrend_linear(tmp, n, tmp);
                {
                    double mean = 0.0;
                    for (i = 0; i < n; ++i) {
                        tmp[i] = tmp[i] * 5.0 / (4.0 * M_PI);
                        mean += tmp[i];
                    }
                    mean /= (double)n;
                    for (i = 0; i < n; ++i) tmp[i] -= mean;
                }
                {
                    double *flt = (double *)malloc((size_t)n * sizeof(double));
                    if (flt && aura_sosfiltfilt(sos_cardiac, ns_cardiac, tmp, n,
                                                flt, -1) == 0) {
                        for (i = 0; i < n; ++i) {
                            disp[((size_t)i * c + ci) * r + ri] = flt[i];
                        }
                    } else {
                        for (i = 0; i < n; ++i) {
                            disp[((size_t)i * c + ci) * r + ri] = 0.0;
                        }
                    }
                    free(flt);
                }
            }
        }
        disp_ref = (double *)malloc((size_t)n * sizeof(double));
        if (!disp_ref) goto fail;
        for (i = 0; i < n; ++i) {
            disp_ref[i] = disp[((size_t)i * c + ref_c) * r + r0];
        }
        {
            double mean = 0.0, var = 0.0;
            for (i = 0; i < n; ++i) mean += disp_ref[i];
            mean /= (double)n;
            for (i = 0; i < n; ++i) {
                double d = disp_ref[i] - mean;
                var += d * d;
            }
            std_ref = sqrt(var / (double)n);
        }
    }

    /* ---- 跨 bin 相位对齐 ---- */
    {
        double *zrr = (double *)malloc((size_t)n * sizeof(double));
        double *zri = (double *)malloc((size_t)n * sizeof(double));
        if (!zrr || !zri) {
            free(zrr); free(zri);
            goto fail;
        }
        for (i = 0; i < n; ++i) {
            size_t idx = ((size_t)i * c + ref_c) * r + r0;
            zrr[i] = zr[idx];
            zri[i] = zi[idx];
        }
        for (ri = 0; ri < r; ++ri) {
            double delta = 0.0;
            if (ri != r0) {
                double cr = 0.0, ci_ = 0.0;
                for (i = 0; i < n; ++i) {
                    size_t idx = ((size_t)i * c + ref_c) * r + ri;
                    double xr = zr[idx], xi = zi[idx];
                    /* 乘参考共轭 */
                    cr += xr * zrr[i] + xi * zri[i];
                    ci_ += xi * zrr[i] - xr * zri[i];
                }
                delta = atan2(ci_, cr);
            }
            for (i = 0; i < n; ++i) {
                for (ci = 0; ci < c; ++ci) {
                    size_t idx = ((size_t)i * c + ci) * r + ri;
                    double xr = zr[idx], xi = zi[idx];
                    double cd = cos(delta), sd = sin(delta);
                    ar[idx] = xr * cd + xi * sd;
                    ai[idx] = -xr * sd + xi * cd;
                }
            }
        }
        free(zrr); free(zri);
    }

    /* ---- Bin 级幅度归一化 ---- */
    for (ri = 0; ri < r; ++ri) bin_scales[ri] = 1.0;
    for (k = 0; k < n_bins; ++k) {
        double robust;
        ri = bin_indices[k];
        for (ci = 0; ci < c; ++ci) {
            for (i = 0; i < n; ++i) {
                size_t idx = ((size_t)i * c + ci) * r + ri;
                ampbuf[i] = sqrt(ar[idx] * ar[idx] + ai[idx] * ai[idx]);
            }
            medbuf[ci] = median_copy(ampbuf, n);
        }
        robust = median_copy(medbuf, c);
        if (robust < 1e-9) {
            bin_scales[ri] = 0.0;
            for (i = 0; i < n; ++i) for (ci = 0; ci < c; ++ci) {
                size_t idx = ((size_t)i * c + ci) * r + ri;
                ar[idx] = ai[idx] = 0.0;
            }
        } else {
            double sc = 1.0 / robust;
            bin_scales[ri] = sc;
            for (i = 0; i < n; ++i) for (ci = 0; ci < c; ++ci) {
                size_t idx = ((size_t)i * c + ci) * r + ri;
                ar[idx] *= sc;
                ai[idx] *= sc;
            }
        }
    }
    scale_r0 = (bin_scales[r0] > 0.0) ? bin_scales[r0] : 1.0;

    /* 归一化后的参考 */
    {
        double *zrr = (double *)malloc((size_t)n * sizeof(double));
        double *zri = (double *)malloc((size_t)n * sizeof(double));
        double ref_norm_sq = 1e-9;
        if (!zrr || !zri) {
            free(zrr); free(zri);
            goto fail;
        }
        for (i = 0; i < n; ++i) {
            size_t idx = ((size_t)i * c + ref_c) * r + r0;
            zrr[i] = ar[idx];
            zri[i] = ai[idx];
            ref_norm_sq += ar[idx] * ar[idx] + ai[idx] * ai[idx];
        }
        for (k = 0; k < n_bins; ++k) {
            double active_vals[512];
            int n_act = 0;
            ri = bin_indices[k];
            for (ci = 0; ci < c; ++ci) {
                double cr = 0.0, ci_ = 0.0, ci_norm = 0.0, coh;
                for (i = 0; i < n; ++i) {
                    size_t idx = ((size_t)i * c + ci) * r + ri;
                    double xr = ar[idx], xi = ai[idx];
                    cr += xr * zrr[i] + xi * zri[i];
                    ci_ += xi * zrr[i] - xr * zri[i];
                    ci_norm += xr * xr + xi * xi;
                }
                if (ci_norm < 1e-12) coh = 0.0;
                else coh = sqrt(cr * cr + ci_ * ci_)
                           / (sqrt(ci_norm * ref_norm_sq) + 1e-9);
                coh_mat[ci * r + ri] = coh;
            }
            if (use_cardiac && disp_ref && std_ref > 1e-6) {
                for (ci = 0; ci < c; ++ci) {
                    double mean = 0.0, var = 0.0, cov = 0.0, rho;
                    for (i = 0; i < n; ++i) mean += disp[((size_t)i * c + ci) * r + ri];
                    mean /= (double)n;
                    for (i = 0; i < n; ++i) {
                        double d = disp[((size_t)i * c + ci) * r + ri] - mean;
                        var += d * d;
                    }
                    var = sqrt(var / (double)n);
                    if (var < 1e-9) {
                        rho = 0.0;
                    } else {
                        double rmean = 0.0;
                        for (i = 0; i < n; ++i) rmean += disp_ref[i];
                        rmean /= (double)n;
                        for (i = 0; i < n; ++i) {
                            cov += (disp[((size_t)i * c + ci) * r + ri] - mean)
                                   * (disp_ref[i] - rmean);
                        }
                        cov /= (double)n;
                        rho = cov / (var * std_ref + 1e-9);
                        rho = clip01(rho);
                    }
                    if (rho > 1e-6 && n_act < 512) active_vals[n_act++] = rho;
                }
                if (n_act >= 3) bin_coh[ri] = median_copy(active_vals, n_act);
                else if (n_act > 0) {
                    double mn = active_vals[0];
                    for (i = 1; i < n_act; ++i) if (active_vals[i] < mn) mn = active_vals[i];
                    bin_coh[ri] = mn;
                } else bin_coh[ri] = 0.0;
            } else {
                for (ci = 0; ci < c; ++ci) {
                    double v = coh_mat[ci * r + ri];
                    if (v > 1e-6 && n_act < 512) active_vals[n_act++] = v;
                }
                if (n_act >= 3) bin_coh[ri] = median_copy(active_vals, n_act);
                else if (n_act > 0) {
                    double mn = active_vals[0];
                    for (i = 1; i < n_act; ++i) if (active_vals[i] < mn) mn = active_vals[i];
                    bin_coh[ri] = mn;
                } else bin_coh[ri] = 0.0;
            }
        }
        free(zrr); free(zri);
    }

    /* 相干抑幅 */
    {
        double s_ew = 0.0;
        for (k = 0; k < n_bins; ++k) {
            double base;
            ri = bin_indices[k];
            base = (ri == r0) ? (bin_coh[ri] > 0.20 ? bin_coh[ri] : 0.20)
                              : (bin_coh[ri] > bin_coh_min ? bin_coh[ri]
                                                           : bin_coh_min);
            effective_w[ri] = bin_weights[ri] * pow(base, bin_coh_power);
            s_ew += effective_w[ri];
        }
        if (s_ew <= 0.0) effective_w[r0] = 1.0;
    }

    /* 融合 */
    {
        double ref_norm_sq = 1e-9;
        double s_ew = 0.0, scale = 1.0;
        double *zrr = (double *)malloc((size_t)n * sizeof(double));
        double *zri = (double *)malloc((size_t)n * sizeof(double));
        if (!zrr || !zri) {
            free(zrr); free(zri);
            goto fail;
        }
        for (i = 0; i < n; ++i) {
            size_t idx = ((size_t)i * c + ref_c) * r + r0;
            zrr[i] = ar[idx]; zri[i] = ai[idx];
            ref_norm_sq += ar[idx] * ar[idx] + ai[idx] * ai[idx];
        }
        for (k = 0; k < n_bins; ++k) s_ew += effective_w[bin_indices[k]];
        scale = (s_ew > 0.0) ? (1.0 / s_ew) : 1.0;
        for (k = 0; k < n_bins; ++k) {
            double w_r_base = effective_w[bin_indices[k]] * scale;
            ri = bin_indices[k];
            if (w_r_base <= 0.0) continue;
            for (ci = 0; ci < c; ++ci) {
                double cr = 0.0, ci_ = 0.0, ci_norm = 0.0, coh, delta, w_ci;
                for (i = 0; i < n; ++i) {
                    size_t idx = ((size_t)i * c + ci) * r + ri;
                    double xr = ar[idx], xi = ai[idx];
                    cr += xr * zrr[i] + xi * zri[i];
                    ci_ += xi * zrr[i] - xr * zri[i];
                    ci_norm += xr * xr + xi * xi;
                }
                if (ci_norm < 1e-12) coh = 0.0;
                else coh = sqrt(cr * cr + ci_ * ci_)
                           / (sqrt(ci_norm * ref_norm_sq) + 1e-9);
                if (coh < coherence_threshold) continue;
                w_ci = pow(coh, channel_coh_power);
                delta = atan2(ci_, cr);
                for (i = 0; i < n; ++i) {
                    size_t idx = ((size_t)i * c + ci) * r + ri;
                    double xr = ar[idx], xi = ai[idx];
                    double cd = cos(delta), sd = sin(delta);
                    double rr = xr * cd + xi * sd;
                    double ii = -xr * sd + xi * cd;
                    zf_r[i] += w_r_base * w_ci * rr;
                    zf_i[i] += w_r_base * w_ci * ii;
                }
                total_weight += w_r_base * w_ci;
                active_count[ci]++;
            }
        }
        if (total_weight > 0.0) {
            for (i = 0; i < n; ++i) {
                zf_r[i] /= total_weight;
                zf_i[i] /= total_weight;
            }
        } else {
            for (i = 0; i < n; ++i) {
                zf_r[i] = zrr[i];
                zf_i[i] = zri[i];
            }
        }
        if (scale_r0 > 1e-9) {
            for (i = 0; i < n; ++i) { zf_r[i] /= scale_r0; zf_i[i] /= scale_r0; }
        }
        for (i = 0; i < n; ++i) { out_r[i] = zf_r[i]; out_i[i] = zf_i[i]; }
        free(zrr); free(zri);
    }

    /* 统计 + frame_valid */
    {
        int n_active_channels = 0;
        double mean_coh = 0.0;
        double amp_med, amp_floor;
        for (ci = 0; ci < c; ++ci) if (active_count[ci] > 0) n_active_channels++;
        for (k = 0; k < n_bins; ++k) {
            for (ci = 0; ci < c; ++ci) {
                mean_coh += coh_mat[ci * r + bin_indices[k]];
            }
        }
        mean_coh /= (double)(c * n_bins);
        stats->n_active_channels = n_active_channels;
        stats->n_active_bins = n_bins;
        stats->mean_coherence = mean_coh;
        for (i = 0; i < m; ++i) {
            ampbuf[i] = sqrt(zr[i] * zr[i] + zi[i] * zi[i]);
        }
        amp_med = median_copy(ampbuf, m);
        amp_floor = (amp_med > 0.0) ? amp_floor_frac * amp_med : 0.0;
        if (frame_valid) {
            for (i = 0; i < n; ++i) {
                for (ci = 0; ci < c; ++ci) {
                    for (ri = 0; ri < r; ++ri) {
                        size_t idx = ((size_t)i * c + ci) * r + ri;
                        int in_bin = 0;
                        for (k = 0; k < n_bins; ++k) {
                            if (bin_indices[k] == ri) { in_bin = 1; break; }
                        }
                        frame_valid[idx] = (unsigned char)(
                            in_bin
                            && (ampbuf[idx] > amp_floor)
                            && (coh_mat[ci * r + ri] >= coherence_threshold));
                    }
                }
            }
        }
    }

    free(bin_indices); free(ar); free(ai); free(disp); free(disp_ref);
    free(bin_scales); free(bin_coh); free(effective_w);
    free(zf_r); free(zf_i); free(active_count); free(tmp);
    free(ampbuf); free(medbuf);
    return 0;

fail:
    free(bin_indices); free(ar); free(ai); free(disp); free(disp_ref);
    free(bin_scales); free(bin_coh); free(effective_w);
    free(zf_r); free(zf_i); free(active_count); free(tmp);
    free(ampbuf); free(medbuf);
    return -1;
}

/* 全程融合 */
static void compute_bin_weights(const double *q, int r, int center,
                                int neighborhood, double sigma_r, double beta,
                                double *w) {
    int i;
    double s = 0.0;
    for (i = 0; i < r; ++i) w[i] = 0.0;
    for (i = 0; i < r; ++i) {
        if (abs(i - center) > neighborhood) continue;
        {
            double spatial = exp(-((double)(i - center) * (i - center))
                                 / (2.0 * sigma_r * sigma_r));
            double qc = q[i] < 0.0 ? 0.0 : (q[i] > 1.0 ? 1.0 : q[i]);
            w[i] = exp(beta * qc) * spatial;
            s += w[i];
        }
    }
    if (s > 0.0) {
        for (i = 0; i < r; ++i) w[i] /= s;
    } else if (center >= 0 && center < r) {
        w[center] = 1.0;
    }
}

static const char *phase_fill(double *phase, int n,
                              const unsigned char *rel, int max_gap) {
    int *bad = NULL, nb = 0, i, n_freeze = 0, n_interp = 0;
    for (i = 0; i < n; ++i) if (!rel[i]) break;
    if (i == n) return "all_reliable";
    bad = (int *)malloc((size_t)n * sizeof(int));
    if (!bad) return "interpolation";
    for (i = 0; i < n; ++i) if (!rel[i]) bad[nb++] = i;
    i = 0;
    while (i < nb) {
        int s = bad[i], e = s;
        while (i + 1 < nb && bad[i + 1] == e + 1) { ++i; e = bad[i]; }
        {
            int gap = e - s + 1;
            if (gap <= max_gap) {
                double fill_val;
                if (s > 0) fill_val = phase[s - 1];
                else {
                    int kk, next_rel = s;
                    for (kk = e + 1; kk < n; ++kk) {
                        if (rel[kk]) { next_rel = kk; break; }
                    }
                    fill_val = (next_rel > s) ? phase[next_rel] : 0.0;
                }
                {
                    int j;
                    for (j = s; j <= e; ++j) phase[j] = fill_val;
                }
                n_freeze += gap;
            } else {
                int left = (s > 0) ? s - 1 : 0;
                int right = (e + 1 < n) ? e + 1 : n - 1;
                while (left > 0 && !rel[left]) --left;
                while (right < n - 1 && !rel[right]) ++right;
                if (rel[left] && rel[right] && right > left) {
                    int j;
                    for (j = s; j <= e; ++j) {
                        double t = (double)(j - left) / (double)(right - left);
                        phase[j] = phase[left] + t * (phase[right] - phase[left]);
                    }
                } else {
                    double sum = 0.0;
                    int cnt = 0, j;
                    for (j = 0; j < n; ++j) if (rel[j]) { sum += phase[j]; cnt++; }
                    {
                        double fill_val = cnt > 0 ? sum / cnt : 0.0;
                        for (j = s; j <= e; ++j) phase[j] = fill_val;
                    }
                }
                n_interp += gap;
            }
        }
        ++i;
    }
    free(bad);
    if (n_freeze > 0 && n_interp > 0) return "mixed";
    if (n_freeze > 0) return "freeze";
    if (n_interp > 0) return "interpolation";
    return "all_reliable";
}

static void stitch_overlap(double *fr, double *fi, int N,
                           const double *nr, const double *ni, int M,
                           int start, int end, int *filled_until) {
    int overlap_start = start;
    int overlap_end = end < N ? end : N;
    int overlap_len, n_fade, i;
    if (*filled_until < overlap_end) overlap_end = *filled_until;
    overlap_len = overlap_end - overlap_start;
    if (overlap_len <= 0) {
        for (i = 0; i < end - start; ++i) {
            fr[start + i] = nr[i];
            fi[start + i] = ni[i];
        }
        return;
    }
    {
        int new_start = overlap_start - start;
        int new_end = overlap_end - start;
        double cr = 0.0, ci_ = 0.0, e_old = 0.0, e_new = 0.0;
        double phase, amp_scale, *alr, *ali;
        if (new_start < 0 || new_end > M) {
            for (i = 0; i < end - start; ++i) {
                fr[start + i] = nr[i];
                fi[start + i] = ni[i];
            }
            return;
        }
        for (i = 0; i < overlap_len; ++i) {
            double or_ = fr[overlap_start + i], oi_ = fi[overlap_start + i];
            double xr = nr[new_start + i], xi = ni[new_start + i];
            cr += xr * or_ + xi * oi_;
            ci_ += xi * or_ - xr * oi_;
            e_old += or_ * or_ + oi_ * oi_;
            e_new += xr * xr + xi * xi;
        }
        phase = atan2(ci_, cr);
        amp_scale = (e_new > 1e-12) ? sqrt(e_old / e_new) : 1.0;
        if (amp_scale < 0.5) amp_scale = 0.5;
        if (amp_scale > 2.0) amp_scale = 2.0;
        alr = (double *)malloc((size_t)M * sizeof(double));
        ali = (double *)malloc((size_t)M * sizeof(double));
        if (!alr || !ali) { free(alr); free(ali); return; }
        for (i = 0; i < M; ++i) {
            double cd = cos(phase), sd = sin(phase);
            alr[i] = nr[i] * amp_scale * cd + ni[i] * amp_scale * sd;
            ali[i] = -nr[i] * amp_scale * sd + ni[i] * amp_scale * cd;
        }
        n_fade = 20 < overlap_len ? 20 : overlap_len;
        for (i = 0; i < n_fade; ++i) {
            double gamma = (n_fade == 1) ? 0.0 : (double)i / (double)(n_fade - 1);
            int idx = overlap_start + i;
            fr[idx] = (1.0 - gamma) * fr[idx] + gamma * alr[new_start + i];
            fi[idx] = (1.0 - gamma) * fi[idx] + gamma * ali[new_start + i];
        }
        {
            int replace_start = overlap_start + n_fade;
            if (replace_start < end) {
                int src = replace_start - start;
                for (i = 0; i < end - replace_start; ++i) {
                    fr[replace_start + i] = alr[src + i];
                    fi[replace_start + i] = ali[src + i];
                }
            }
        }
        free(alr); free(ali);
        if (end > *filled_until) *filled_until = end;
    }
}

int aura_fuse_trajectory(const double *zr, const double *zi, int N,
                         int C, int R, const int *traj, int K,
                         const double *Q, int win_n, int step_n, double fs,
                         int neighborhood, double sigma_r, double beta,
                         double coherence_threshold,
                         double channel_coh_power, double bin_coh_power,
                         double bin_coh_min, double amp_floor_frac,
                         double warmup_s, int phase_freeze_max_gap,
                         double tau_phi_jump, double tau_phi_acc,
                         const double *sos_cardiac, int ns_cardiac,
                         double *out_r, double *out_i, double *phase,
                         double *amp, unsigned char *reliable,
                         aura_fuse_summary *summary) {
    int m = N * C * R;
    double *zfr = NULL, *zfi = NULL, *coh_all = NULL, *fvv = NULL;
    int *nch = NULL, *nbin = NULL;
    double *mean_coh = NULL;
    int constant = 1, k, i, c, r;
    if (!zr || !zi || !traj || !Q || !out_r || !out_i || !phase || !amp
            || N <= 0 || C <= 0 || R <= 0 || K <= 0) return -1;
    for (k = 1; k < K; ++k) if (traj[k] != traj[k - 1]) constant = 0;
    zfr = (double *)calloc((size_t)N, sizeof(double));
    zfi = (double *)calloc((size_t)N, sizeof(double));
    coh_all = (double *)calloc((size_t)K * C * R, sizeof(double));
    nch = (int *)calloc((size_t)K, sizeof(int));
    nbin = (int *)calloc((size_t)K, sizeof(int));
    mean_coh = (double *)calloc((size_t)K, sizeof(double));
    fvv = (double *)calloc((size_t)m, sizeof(double));
    if (!zfr || !zfi || !coh_all || !nch || !nbin || !mean_coh || !fvv)
        goto fail;

    if (constant) {
        int center = traj[0];
        double *qavg = (double *)calloc((size_t)R, sizeof(double));
        double *bw = (double *)malloc((size_t)R * sizeof(double));
        unsigned char *fv_full = (unsigned char *)calloc((size_t)m, 1);
        aura_fuse_stats st;
        if (!qavg || !bw || !fv_full) { free(qavg); free(bw); free(fv_full); goto fail; }
        for (k = 0; k < K; ++k) for (r = 0; r < R; ++r) qavg[r] += Q[k * R + r];
        for (r = 0; r < R; ++r) qavg[r] /= (double)K;
        compute_bin_weights(qavg, R, center, neighborhood, sigma_r, beta, bw);
        if (aura_fuse_window(zr, zi, N, C, R, bw, center, fs, 0.0,
                             neighborhood, sigma_r, beta, coherence_threshold,
                             channel_coh_power, bin_coh_power, bin_coh_min,
                             amp_floor_frac, sos_cardiac, ns_cardiac,
                             zfr, zfi, &coh_all[0], fv_full, &st) != 0) {
            free(qavg); free(bw); free(fv_full);
            goto fail;
        }
        for (k = 0; k < K; ++k) {
            nch[k] = st.n_active_channels;
            nbin[k] = st.n_active_bins;
            mean_coh[k] = st.mean_coherence;
            if (k > 0) memcpy(&coh_all[(size_t)k * C * R], &coh_all[0],
                              (size_t)C * R * sizeof(double));
        }
        for (i = 0; i < m; ++i) if (fv_full[i]) fvv[i] = 1.0;
        free(qavg); free(bw); free(fv_full);
    } else {
        unsigned char *fv_win = (unsigned char *)malloc((size_t)win_n * C * R);
        int filled_until = 0;
        if (!fv_win) goto fail;
        for (k = 0; k < K; ++k) {
            int start = k * step_n;
            int end = start + win_n;
            double *nr, *ni, *bw;
            aura_fuse_stats fsst;
            int center = traj[k], j;
            if (end > N) end = N;
            if (end - start < 4) continue;
            nr = (double *)malloc((size_t)(end - start) * sizeof(double));
            ni = (double *)malloc((size_t)(end - start) * sizeof(double));
            bw = (double *)malloc((size_t)R * sizeof(double));
            if (!nr || !ni || !bw) { free(nr); free(ni); free(bw); break; }
            compute_bin_weights(&Q[k * R], R, center, neighborhood,
                                sigma_r, beta, bw);
            if (aura_fuse_window(&zr[(size_t)start * C * R],
                                 &zi[(size_t)start * C * R], end - start,
                                 C, R, bw, center, fs, 0.0, neighborhood,
                                 sigma_r, beta, coherence_threshold,
                                 channel_coh_power, bin_coh_power,
                                 bin_coh_min, amp_floor_frac,
                                 sos_cardiac, ns_cardiac,
                                 nr, ni, &coh_all[(size_t)k * C * R],
                                 fv_win, &fsst) == 0) {
                nch[k] = fsst.n_active_channels;
                nbin[k] = fsst.n_active_bins;
                mean_coh[k] = fsst.mean_coherence;
                if (k == 0) {
                    for (j = 0; j < end - start; ++j) {
                        zfr[start + j] = nr[j]; zfi[start + j] = ni[j];
                    }
                    filled_until = end;
                } else {
                    stitch_overlap(zfr, zfi, N, nr, ni, end - start,
                                   start, end, &filled_until);
                }
                for (j = 0; j < (end - start) * C * R; ++j) {
                    if (fv_win[j]) {
                        size_t idx = (size_t)start * C * R + j;
                        fvv[idx] = 1.0;
                    }
                }
            }
            free(nr); free(ni); free(bw);
        }
        free(fv_win);
    }

    /* 相位/幅度 */
    for (i = 0; i < N; ++i) {
        out_r[i] = zfr[i];
        out_i[i] = zfi[i];
        amp[i] = sqrt(zfr[i] * zfr[i] + zfi[i] * zfi[i]);
        if (i == 0) {
            phase[i] = atan2(zfi[i], zfr[i]);
        } else {
            double prev = atan2(zfi[i - 1], zfr[i - 1]);
            double ph = atan2(zfi[i], zfr[i]);
            double d = ph - prev;
            double ddmod = fmod(d + M_PI, 2.0 * M_PI);
            if (ddmod < 0.0) ddmod += 2.0 * M_PI;
            ddmod -= M_PI;
            if (fabs(ddmod + M_PI) < 1e-15 && d > 0.0) ddmod = M_PI;
            phase[i] = phase[i - 1] + d
                       + ((fabs(d) >= M_PI) ? (ddmod - d) : 0.0);
        }
    }

    /* 可靠掩码 + 相位填充 */
    {
        int warmup_n = (int)(warmup_s * fs);
        double amp_floor = 0.0;
        unsigned char *bad;
        double *medbuf;
        if (warmup_n > N - 1) warmup_n = N - 1;
        if (warmup_n < 0) warmup_n = 0;
        medbuf = (double *)malloc((size_t)(N - warmup_n > 0 ? N - warmup_n : 1)
                                  * sizeof(double));
        bad = (unsigned char *)calloc((size_t)N, 1);
        if (!bad || !medbuf) { free(bad); free(medbuf); goto fail; }
        for (i = warmup_n; i < N; ++i) medbuf[i - warmup_n] = amp[i];
        {
            double med = median_copy(medbuf, N - warmup_n > 0 ? N - warmup_n : 1);
            if (med > 0.0) amp_floor = amp_floor_frac * med;
        }
        for (i = 0; i < N; ++i) {
            double d1 = (i > 0) ? phase[i] - phase[i - 1] : 0.0;
            double d2 = (i > 1) ? phase[i] - 2.0 * phase[i - 1] + phase[i - 2] : 0.0;
            if (i < warmup_n || amp[i] < amp_floor
                    || fabs(d1) > tau_phi_jump
                    || fabs(d2) > tau_phi_acc) bad[i] = 1;
        }
        for (i = 0; i < N; ++i) reliable[i] = (unsigned char)(!bad[i]);
        {
            const char *mode = phase_fill(phase, N, reliable,
                                          phase_freeze_max_gap);
            snprintf(summary->fill_mode, sizeof(summary->fill_mode), "%s", mode);
        }
        free(bad); free(medbuf);
    }

    /* 统计 */
    {
        double mc = 0.0, mch = 0.0, mbn = 0.0, pmin = 1e300, pmax = -1e300;
        int nrel = 0;
        for (k = 0; k < K; ++k) {
            mc += mean_coh[k]; mch += nch[k]; mbn += nbin[k];
        }
        summary->n_total = N;
        summary->mean_coherence = K ? mc / K : 0.0;
        summary->mean_active_channels = K ? mch / K : 0.0;
        summary->mean_active_bins = K ? mbn / K : 0.0;
        summary->amp_median = median_copy(amp, N);
        for (i = 0; i < N; ++i) {
            if (phase[i] < pmin) pmin = phase[i];
            if (phase[i] > pmax) pmax = phase[i];
            if (reliable[i]) nrel++;
        }
        summary->phase_range_rad = (N > 0) ? (pmax - pmin) : 0.0;
        summary->amp_reliable_ratio = N ? (double)nrel / N : 0.0;
        {
            double cm = 0.0;
            for (c = 0; c < C; ++c) {
                for (r = 0; r < R; ++r) {
                    double *vals = (double *)malloc((size_t)K * sizeof(double));
                    int nv = 0;
                    if (!vals) continue;
                    for (k = 0; k < K; ++k) {
                        double v = coh_all[((size_t)k * C + c) * R + r];
                        if (v > 0.0) vals[nv++] = v;
                    }
                    cm += nv ? median_copy(vals, nv) : 0.0;
                    free(vals);
                }
            }
            summary->coherence_matrix_mean = cm / (double)(C * R);
        }
        {
            int warmup_n = (int)(warmup_s * fs);
            double sum = 0.0;
            int npost;
            if (warmup_n > N - 1) warmup_n = N - 1;
            if (warmup_n < 0) warmup_n = 0;
            npost = N - warmup_n > 0 ? N - warmup_n : 1;
            for (i = warmup_n; i < N; ++i) {
                for (c = 0; c < C; ++c) {
                    for (r = 0; r < R; ++r) {
                        if (fvv[((size_t)i * C + c) * R + r] > 0.5) sum += 1.0;
                    }
                }
            }
            summary->validity_matrix_mean = sum / ((double)npost * C * R);
        }
    }

    free(zfr); free(zfi); free(coh_all); free(nch); free(nbin);
    free(mean_coh); free(fvv);
    return 0;

fail:
    free(zfr); free(zfi); free(coh_all); free(nch); free(nbin);
    free(mean_coh); free(fvv);
    return -1;
}
