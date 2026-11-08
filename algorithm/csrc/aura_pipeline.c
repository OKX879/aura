#include <stdio.h>
/* 端到端流水线实现。 */
#include "aura_pipeline.h"

/* 调试导出 CSV，路径取自环境变量。 */
static FILE *g_phase_dump = NULL;
static FILE *g_snap_dump = NULL;
static int g_dump_init = 0;
static long g_phase_count = 0;
static long g_snap_count = 0;
static const long kDumpLimit = 5400;   /* 导出上限 */

#include "aura_bin_scorer.h"
#include "aura_complex_fusion.h"
#include "aura_hr.h"
#include "aura_event.h"
#include "aura_fast_motion.h"
#include "aura_kernels.h"
#include "aura_rate.h"
#include "aura_spatial.h"
#include "aura_sos_constants.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Q_HIST_MAX 10
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct AuraPipeline {
    int C, R, fs, window_n, step_n;
    double *zr, *zi;
    double *clutter_r, *clutter_i;
    int clutter_init;
    int pos, count;
    double *qhist;
    int qcount;
    /* 动态跟踪器状态 */
    double *qbhist, *qvhist, *snrevi, *snrema;
    int qbcount, qvcount, snrcount, snr_init;
    double *qtrack, *qbody, *qvital, *qconst;
    aura_distance_lock lock;
    aura_rate_tracker rate;
    aura_fast_motion fm;
    /* 心率通路状态 */
    double *hrwin;
    double hr_zi[AURA_SOS_HR_NS * 2];
    int hr_zi_init;
    double *wide;
    int wblen, wbcap;
    double resp_hold;
    int resp_hold_age;
    /* 最近 120 秒 HR 信号 */
    double *hrhist;
    int hrhist_len, hrhist_cap;
    AuraSnapshot snap;
};

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

static void push_qrow(double *hist, int *count, int R, const double *row) {
    if (*count >= Q_HIST_MAX) {
        memmove(hist, &hist[R], (size_t)(Q_HIST_MAX - 1) * R * sizeof(double));
        *count = Q_HIST_MAX - 1;
    }
    memcpy(&hist[(size_t)(*count) * R], row, (size_t)R * sizeof(double));
    (*count)++;
}

static double clip01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

AuraPipeline *aura_pipeline_create(int channels, int bins, double fs,
                                   int window_n, int step_n) {
    AuraPipeline *p;
    if (channels <= 0 || bins <= 0 || window_n <= 0 || step_n <= 0) return NULL;
    p = (AuraPipeline *)calloc(1, sizeof(AuraPipeline));
    if (!p) return NULL;
    p->C = channels;
    p->R = bins;
    p->fs = (int)(fs + 0.5);
    p->window_n = window_n;
    p->step_n = step_n;
    p->zr = (double *)calloc((size_t)window_n * channels * bins, sizeof(double));
    p->zi = (double *)calloc((size_t)window_n * channels * bins, sizeof(double));
    p->clutter_r = (double *)calloc((size_t)channels * bins, sizeof(double));
    p->clutter_i = (double *)calloc((size_t)channels * bins, sizeof(double));
    p->qhist = (double *)calloc(Q_HIST_MAX * bins, sizeof(double));
    p->qbhist = (double *)calloc(Q_HIST_MAX * bins, sizeof(double));
    p->qvhist = (double *)calloc(Q_HIST_MAX * bins, sizeof(double));
    p->snrevi = (double *)calloc(Q_HIST_MAX * bins, sizeof(double));
    p->snrema = (double *)calloc((size_t)bins, sizeof(double));
    p->qtrack = (double *)calloc(Q_HIST_MAX * bins, sizeof(double));
    p->qbody = (double *)calloc((size_t)bins, sizeof(double));
    p->qvital = (double *)calloc((size_t)bins, sizeof(double));
    p->qconst = (double *)calloc((size_t)bins, sizeof(double));
    p->hrwin = (double *)calloc((size_t)window_n, sizeof(double));
    p->wbcap = (int)(50.0 * fs + 0.5);
    if (p->wbcap < window_n) p->wbcap = window_n;
    p->wide = (double *)calloc((size_t)p->wbcap, sizeof(double));
    p->hrhist_cap = (int)(120.0 * fs + 0.5);
    if (p->hrhist_cap < window_n) p->hrhist_cap = window_n;
    p->hrhist = (double *)calloc((size_t)p->hrhist_cap, sizeof(double));
    p->resp_hold_age = 999;
    if (!p->zr || !p->zi || !p->clutter_r || !p->clutter_i
            || !p->qhist || !p->qbhist || !p->qvhist || !p->snrevi
            || !p->snrema || !p->qtrack || !p->qbody || !p->qvital
            || !p->qconst || !p->hrwin || !p->wide || !p->hrhist) {
        aura_pipeline_destroy(p);
        return NULL;
    }
    aura_distance_lock_init(&p->lock);
    aura_rate_tracker_init(&p->rate);
    aura_fast_motion_init(&p->fm);
    return p;
}

void aura_pipeline_destroy(AuraPipeline *p) {
    if (!p) return;
    free(p->zr); free(p->zi); free(p->qhist); free(p->hrwin); free(p->wide);
    free(p->hrhist);
    free(p->qbhist); free(p->qvhist); free(p->snrevi); free(p->snrema);
    free(p->qtrack); free(p->qbody); free(p->qvital); free(p->qconst);
    free(p->clutter_r); free(p->clutter_i);
    free(p);
}

int aura_pipeline_push(AuraPipeline *p, const float *z_re, const float *z_im) {
    int r, i, n;
    double *zwin_r = NULL, *zwin_i = NULL, *q = NULL;
    double *zf_r = NULL, *zf_i = NULL, *ph = NULL, *am = NULL;
    double *qfus = NULL, *tmpq = NULL;
    unsigned char *rel = NULL;
    aura_fuse_summary fsum;
    aura_bin_quality *bq = NULL;
    int center, locked, trail[1];
    double mean_motion = 0.0;
    if (!p || !z_re || !z_im) return -1;
    {
        const double alpha = 0.003;
        size_t base = (size_t)p->pos * p->C * p->R;
        for (i = 0; i < p->C * p->R; ++i, ++base) {
            double re = (double)z_re[i];
            double im = (double)z_im[i];
            if (!p->clutter_init) {
                p->clutter_r[i] = re;
                p->clutter_i[i] = im;
            } else {
                p->clutter_r[i] = (1.0 - alpha) * p->clutter_r[i] + alpha * re;
                p->clutter_i[i] = (1.0 - alpha) * p->clutter_i[i] + alpha * im;
            }
            p->zr[base] = re - p->clutter_r[i];
            p->zi[base] = im - p->clutter_i[i];
        }
        p->clutter_init = 1;
        p->pos = (p->pos + 1) % p->window_n;
        p->count++;
    }
    if (p->count < p->window_n + 3 * p->fs) return 0;
    if ((p->count - p->window_n) % p->step_n != 0) return 0;

    n = p->window_n;
    zwin_r = (double *)malloc((size_t)n * p->C * p->R * sizeof(double));
    zwin_i = (double *)malloc((size_t)n * p->C * p->R * sizeof(double));
    q = (double *)malloc((size_t)p->R * sizeof(double));
    bq = (aura_bin_quality *)calloc((size_t)p->R, sizeof(aura_bin_quality));
    rel = (unsigned char *)malloc((size_t)n);
    ph = (double *)malloc((size_t)n * sizeof(double));
    am = (double *)malloc((size_t)n * sizeof(double));
    zf_r = (double *)malloc((size_t)n * sizeof(double));
    zf_i = (double *)malloc((size_t)n * sizeof(double));
    if (!zwin_r || !zwin_i || !q || !bq || !rel || !ph || !am || !zf_r || !zf_i)
        goto fail;

    for (i = 0; i < n; ++i) {
        int src = (p->pos + i) % p->window_n;
        size_t b = (size_t)src * p->C * p->R;
        size_t d = (size_t)i * p->C * p->R;
        memcpy(&zwin_r[d], &p->zr[b], (size_t)p->C * p->R * sizeof(double));
        memcpy(&zwin_i[d], &p->zi[b], (size_t)p->C * p->R * sizeof(double));
    }
    if (aura_score_window(zwin_r, zwin_i, n, p->C, p->R, (double)p->fs,
                          (const double *)AURA_SOS_CARDIAC, AURA_SOS_CARDIAC_NS,
                          (const double *)AURA_SOS_MOTION, AURA_SOS_MOTION_NS,
                          bq) != 0) goto fail;
    for (r = 0; r < p->R; ++r) {
        q[r] = bq[r].total_score;
        mean_motion += bq[r].motion_score;
    }
    mean_motion /= (double)p->R;

    push_qrow(p->qhist, &p->qcount, p->R, q);
    tmpq = (double *)malloc((size_t)3 * p->R * sizeof(double));
    if (!tmpq) goto fail;
    /* 谱 SNR 的指数滑动平均 */
    {
        double *svec = tmpq, *dev = tmpq + p->R, *zval = tmpq + 2 * p->R;
        double med, mad;
        for (r = 0; r < p->R; ++r) svec[r] = bq[r].spectral_snr;
        med = median_copy(svec, p->R);
        for (r = 0; r < p->R; ++r) dev[r] = fabs(svec[r] - med);
        mad = median_copy(dev, p->R) + 1e-6;
        for (r = 0; r < p->R; ++r) {
            double z = (svec[r] - med) / (1.4826 * mad);
            if (z < -3.0) z = -3.0;
            if (z > 3.0) z = 3.0;
            zval[r] = z / 3.0;
        }
        if (!p->snr_init) {
            memcpy(p->snrema, zval, (size_t)p->R * sizeof(double));
            p->snr_init = 1;
        } else {
            for (r = 0; r < p->R; ++r)
                p->snrema[r] = 0.5 * p->snrema[r] + 0.5 * zval[r];
        }
        push_qrow(p->snrevi, &p->snrcount, p->R, p->snrema);
        while (p->snrcount > p->qcount) {
            memmove(p->snrevi, &p->snrevi[p->R],
                    (size_t)(p->snrcount - 1) * p->R * sizeof(double));
            p->snrcount--;
        }
        memcpy(p->qtrack, p->qhist,
               (size_t)p->qcount * p->R * sizeof(double));
        for (i = 0; i < p->qcount; ++i)
            for (r = 0; r < p->R; ++r)
                p->qtrack[(size_t)i * p->R + r] += 0.10 * p->snrevi[(size_t)i * p->R + r];
    }
    {
        int *traj = (int *)malloc((size_t)p->qcount * sizeof(int));
        if (!traj) goto fail;
        if (aura_viterbi(p->qtrack, p->qcount, p->R, 0.3, 0.15, 2, traj) != 0) {
            free(traj);
            goto fail;
        }
        center = traj[p->qcount - 1];
        free(traj);
    }
    /* 双轨迹与距离约束 */
    {
        int candidate = center;
        int rvital = center, rbody = center;
        double *qbody = p->qbody, *qvital = p->qvital;
        qfus = q;
        for (r = 0; r < p->R; ++r) {
            qbody[r] = clip01(0.40 * bq[r].energy
                              + 0.20 * bq[r].amp_stability
                              + 0.20 * bq[r].coherence
                              - 0.10 * bq[r].motion_score);
            qvital[r] = clip01(0.25 * bq[r].phase_valid_ratio
                               + 0.25 * bq[r].periodicity
                               + 0.20 * bq[r].coherence
                               + 0.15 * bq[r].amp_stability
                               - 0.15 * bq[r].jump_ratio
                               - 0.10 * bq[r].motion_score);
            qvital[r] = clip01(qvital[r] + 0.10 * p->snrema[r]);
        }
        push_qrow(p->qbhist, &p->qbcount, p->R, qbody);
        push_qrow(p->qvhist, &p->qvcount, p->R, qvital);
        if (p->qbcount >= 2) {
            int *tb = (int *)malloc((size_t)p->qbcount * sizeof(int));
            int *tv = (int *)malloc((size_t)p->qvcount * sizeof(int));
            if (!tb || !tv) { free(tb); free(tv); goto fail; }
            if (aura_viterbi(p->qbhist, p->qbcount, p->R, 0.3, 0.15, 2, tb) != 0
                    || aura_viterbi(p->qvhist, p->qvcount, p->R, 0.3, 0.15, 2, tv) != 0) {
                free(tb); free(tv);
                goto fail;
            }
            rbody = tb[p->qbcount - 1];
            rvital = tv[p->qvcount - 1];
            free(tb); free(tv);
            if (abs(rvital - rbody) > 3) {
                if (qbody[rbody] >= 0.55) {
                    rvital = rbody;
                } else {
                    int lo = rbody - 3, hi = rbody + 3, best;
                    if (lo < 0) lo = 0;
                    if (hi >= p->R) hi = p->R - 1;
                    best = lo;
                    for (r = lo; r <= hi; ++r)
                        if (qvital[r] > qvital[best]) best = r;
                    rvital = best;
                }
            }
            if (abs(rvital - rbody) <= 3 && qvital[rvital] >= 0.12) {
                int lo = rbody - 3, hi = rbody + 3;
                memset(p->qconst, 0, (size_t)p->R * sizeof(double));
                if (lo < 0) lo = 0;
                if (hi >= p->R) hi = p->R - 1;
                for (r = lo; r <= hi; ++r) p->qconst[r] = qvital[r];
                candidate = rvital;
                qfus = p->qconst;
            }
        }
        {
            aura_lock_out lo;
            if (aura_distance_lock_update(&p->lock, candidate, qfus, p->R, &lo) != 0)
                goto fail;
            locked = lo.selected_bin;
        }
    }
    /* 检测到快速运动时清空空间状态 */
    {
        aura_fast_motion_snap fms;
        if (aura_fast_motion_update(&p->fm, zwin_r, zwin_i, n, p->C, p->R,
                                    (double)p->fs, &fms) == 0) {
            p->snap.motion = fms.active ? 1 : 0;
            p->snap.motion_level_rad = fms.level_rad;
            p->snap.motion_excursion_rad = fms.excursion_rad;
            if (fms.detected) {
                aura_distance_lock_reset(&p->lock);
                p->qcount = 0;
                p->qbcount = 0;
                p->qvcount = 0;
                p->snrcount = 0;
                p->snr_init = 0;
            }
        }
    }

    trail[0] = locked;
    if (aura_fuse_trajectory(zwin_r, zwin_i, n, p->C, p->R, trail, 1,
                             qfus ? qfus : q, n, n, (double)p->fs, 2, 1.0, 3.0,
                             0.5, 2.0, 2.0, 0.0, 0.3, 3.0, 30, 1.0, 0.7,
                             (const double *)AURA_SOS_CARDIAC,
                             AURA_SOS_CARDIAC_NS,
                             zf_r, zf_i, ph, am, rel, &fsum) != 0)
        goto fail;

    /* HR 通路：滤波后估计呼吸与心率 */
    {
        int add = p->step_n < n ? p->step_n : n;
        double *mm = (double *)malloc((size_t)n * sizeof(double));
        double mean = 0.0, f_resp = NAN;
        if (!mm) goto fail;
        for (i = 0; i < n; ++i) mm[i] = ph[i] * (5.0 / (4.0 * M_PI));
        for (i = 0; i < n; ++i) mean += mm[i];
        mean /= (double)n;
        for (i = 0; i < n; ++i) mm[i] -= mean;
        /* 只追加本窗新增段 */
        if (p->wblen + add > p->wbcap) {
            int drop = p->wblen + add - p->wbcap;
            memmove(p->wide, &p->wide[drop],
                    (size_t)(p->wblen - drop) * sizeof(double));
            p->wblen -= drop;
        }
        memcpy(&p->wide[p->wblen], &mm[n - add], (size_t)add * sizeof(double));
        p->wblen += add;
        if (!g_dump_init) {
            g_dump_init = 1;
            const char *phase_path = getenv("AURA_PHASE_DUMP");
            const char *snap_path = getenv("AURA_SNAP_DUMP");
            if (phase_path && *phase_path) {
                g_phase_dump = fopen(phase_path, "w");
                if (g_phase_dump) fprintf(g_phase_dump, "n,mm\n");
            }
            if (snap_path && *snap_path) {
                g_snap_dump = fopen(snap_path, "w");
                if (g_snap_dump) fprintf(g_snap_dump, "frame,hr,resp_hz,conf,motion\n");
            }
        }
        if (g_phase_dump && g_phase_count < kDumpLimit) {
            for (i = n - add; i < n && g_phase_count < kDumpLimit; ++i) {
                fprintf(g_phase_dump, "%ld,%.6f\n", g_phase_count++, mm[i]);
            }
            fflush(g_phase_dump);
            if (g_phase_count >= kDumpLimit) {
                fclose(g_phase_dump);
                g_phase_dump = NULL;
            }
        }
        if (p->wblen >= (int)(30.0 * p->fs)) {
            /* 呼吸估计需 30 秒窗 */
            f_resp = aura_estimate_resp(p->wide, p->wblen, (double)p->fs,
                                        (const double *)AURA_SOS_RESP,
                                        AURA_SOS_RESP_NS);
            if (isfinite(f_resp)) {
                p->resp_hold = f_resp;
                p->resp_hold_age = 0;
            } else if (p->resp_hold_age < 8 && isfinite(p->resp_hold)) {
                f_resp = p->resp_hold;
                p->resp_hold_age++;
            }
        }
        /* 1.0-2.5Hz 因果滤波，状态跨窗保持 */
        if (!p->hr_zi_init) {
            memset(p->hr_zi, 0, sizeof(p->hr_zi));
            p->hr_zi_init = 1;
        }
        aura_sosfilt_state((const double *)AURA_SOS_HR, AURA_SOS_HR_NS,
                           mm, n, p->hrwin, p->hr_zi);
        if (p->hrhist_len + add > p->hrhist_cap) {
            int drop = p->hrhist_len + add - p->hrhist_cap;
            memmove(p->hrhist, &p->hrhist[drop],
                    (size_t)(p->hrhist_len - drop) * sizeof(double));
            p->hrhist_len -= drop;
        }
        memcpy(&p->hrhist[p->hrhist_len], &p->hrwin[n - add],
               (size_t)add * sizeof(double));
        p->hrhist_len += add;
        free(mm);
        p->snap.resp_hz = f_resp;
    }

    p->snap.frame_id = p->count;
    if (g_snap_dump && g_snap_count < kDumpLimit) {
        fprintf(g_snap_dump, "%ld,%.2f,%.5f,%.4f,%d\n",
                (long)p->count, p->snap.hr_bpm, p->snap.resp_hz,
                p->snap.hr_conf, p->snap.motion);
        ++g_snap_count;
        fflush(g_snap_dump);
        if (g_snap_count >= kDumpLimit) {
            fclose(g_snap_dump);
            g_snap_dump = NULL;
        }
    }
    p->snap.selected_bin = locked;
    p->snap.candidate_bin = center;
    p->snap.mean_coherence = fsum.mean_coherence;
    p->snap.fused_amp = median_copy(am, n);
    if (mean_motion > 0.6) p->snap.motion = 1;

    {
        aura_hr_result hr;
        /* 心率分析窗取最近 30 秒。 */
        const double *hr_sig = p->hrwin;
        int hr_n = n, hr_warm = 0;
        {
            int span = (int)(30.0 * p->fs + 0.5);
            if (p->hrhist_len >= (int)(20.0 * p->fs)) {
                hr_n = p->hrhist_len < span ? p->hrhist_len : span;
                hr_sig = &p->hrhist[p->hrhist_len - hr_n];
                hr_warm = 1;
            }
        }
        if (aura_estimate_all(hr_sig, hr_n, (double)p->fs,
                              p->snap.resp_hz, 1,
                              (const double *)AURA_SOS_RESP, AURA_SOS_RESP_NS,
                              &hr) == 0) {
            double conf = 0.0;
            if (isfinite(hr.max_diff_bpm))
                conf = 1.0 - hr.max_diff_bpm / 24.0;
            if (conf < 0.0) conf = 0.0;
            if (conf > 1.0) conf = 1.0;
            p->snap.hr_fft = hr.hr_fft;
            p->snap.hr_acf = hr.hr_acf;
            p->snap.hr_ppi = hr.hr_ppi;
            p->snap.hr_fused = hr.hr_fused;
            p->snap.hr_conf = conf;
            p->snap.resp_hz = hr.f_resp;
            p->snap.hr_snr = hr.snr_fft;
            p->snap.hr_max_diff_bpm = hr.max_diff_bpm;
            p->snap.hr_consistent = hr.consistent;
            p->snap.ppi_source = hr.ppi_source;
            p->snap.w_fft = hr.w_fft;
            p->snap.w_acf = hr.w_acf;
            p->snap.w_ppi = hr.w_ppi;
            p->snap.n_beats = hr.n_ppi_peaks;
            p->snap.presence = isfinite(hr.hr_fused);
            /* 2:1 高心率救援 */
            {
                double primary = hr.hr_fused;
                double pconf = conf;
                int rescued = 0;
                if (!hr.consistent
                        && isfinite(hr.hr_fft) && isfinite(hr.hr_acf)
                        && isfinite(hr.hr_ppi)) {
                    double lo = 0.5 * (hr.hr_acf + hr.hr_ppi);
                    if (fabs(hr.hr_acf - hr.hr_ppi) <= 6.0
                            && hr.hr_fft >= 80.0
                            && fabs(hr.hr_fft - 2.0 * lo) <= 8.0) {
                        primary = hr.hr_fft;
                        pconf = 1.0 - fabs(hr.hr_acf - hr.hr_ppi) / 24.0;
                        if (pconf < 0.35) pconf = 0.35;
                        rescued = 1;
                    }
                }
                p->snap.hr_rescued = rescued;
                {
                    /* 预热期不更新平滑器 */
                    double trk = NAN;
                    int upd = hr_warm
                        ? aura_rate_tracker_update(&p->rate, primary, pconf, &trk)
                        : 0;
                    p->snap.hr_confident = upd ? 1 : 0;
                    p->snap.hr_bpm = (isfinite(trk) && trk > 0.0) ? trk : 0.0;
                }
            }
        }
    }

    free(zwin_r); free(zwin_i); free(q); free(bq); free(rel);
    free(ph); free(am); free(zf_r); free(zf_i); free(tmpq);
    return 0;

fail:
    free(zwin_r); free(zwin_i); free(q); free(bq); free(rel);
    free(ph); free(am); free(zf_r); free(zf_i); free(tmpq);
    return -1;
}

int aura_pipeline_snapshot(const AuraPipeline *p, AuraSnapshot *out) {
    if (!p || !out) return -1;
    *out = p->snap;
    return 0;
}

int aura_pipeline_get_events(const AuraPipeline *p, aura_event_result *out) {
    if (!p || !out) return -1;
    memset(out, 0, sizeof(*out));
    if (p->hrhist_len < 64) return 0;
    return aura_event_detect(p->hrhist, p->hrhist_len, (double)p->fs,
                             0.0, out);
}

int aura_pipeline_get_hrv(const AuraPipeline *p, int mode, aura_hrv_result *out) {
    aura_event_result ev;
    double *clean_ibi = NULL, *clean_hr = NULL;
    int *clean_seg = NULL;
    int n_clean = 0, i, start, rc;
    if (!p || !out) return -1;
    memset(out, 0, sizeof(*out));
    memset(&ev, 0, sizeof(ev));
    if (p->hrhist_len < 64) {
        snprintf(out->reason, sizeof(out->reason), "duration_too_short");
        return 0;
    }
    if (aura_event_detect(p->hrhist, p->hrhist_len, (double)p->fs, 0.0, &ev) != 0)
        return -1;
    if (ev.n_ibis > 0) {
        clean_ibi = (double *)malloc((size_t)ev.n_ibis * sizeof(double));
        clean_seg = (int *)malloc((size_t)ev.n_ibis * sizeof(int));
        clean_hr = (double *)malloc((size_t)ev.n_ibis * sizeof(double));
    }
    if (clean_ibi && clean_seg) {
        start = 0;
        for (i = 1; i <= ev.n_ibis; ++i) {
            int boundary = (i == ev.n_ibis)
                || (ev.segment_ids[i] != ev.segment_ids[i - 1]);
            if (!boundary) continue;
            {
                int len = i - start;
                if (len >= 5) {
                    double med = median_copy(ev.ibis_ms + start, len);
                    int j;
                    for (j = start; j < i; ++j) {
                        if (fabs(ev.ibis_ms[j] - med) <= 0.25 * med) {
                            clean_ibi[n_clean] = ev.ibis_ms[j];
                            clean_seg[n_clean] = ev.segment_ids[j];
                            if (clean_hr) clean_hr[n_clean] = 60000.0 / ev.ibis_ms[j];
                            n_clean++;
                        }
                    }
                }
            }
            start = i;
        }
    }
    if (n_clean == 0 && ev.n_ibis > 0) {
        /* 清洗后为空则保留原始 IBI */
        memcpy(clean_ibi, ev.ibis_ms, (size_t)ev.n_ibis * sizeof(double));
        memcpy(clean_seg, ev.segment_ids, (size_t)ev.n_ibis * sizeof(int));
        for (i = 0; i < ev.n_ibis; ++i) clean_hr[i] = 60000.0 / ev.ibis_ms[i];
        n_clean = ev.n_ibis;
    }
    rc = aura_hrv_compute(clean_ibi, clean_seg, n_clean,
                          (double)p->hrhist_len / (double)p->fs,
                          p->hrhist_len, p->hrhist_len,
                          clean_hr, n_clean, mode, out);
    aura_event_free(&ev);
    free(clean_ibi); free(clean_seg); free(clean_hr);
    return rc;
}
