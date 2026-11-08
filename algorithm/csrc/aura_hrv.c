/* HRV 指标计算实现。 */
#include "aura_hrv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dsort(double *x, int n) {
    int i, j;
    for (i = 1; i < n; ++i) {
        double v = x[i];
        j = i - 1;
        while (j >= 0 && x[j] > v) { x[j + 1] = x[j]; --j; }
        x[j + 1] = v;
    }
}

static double dmedian(const double *x, int n) {
    double *b;
    double v;
    if (n <= 0) return NAN;
    b = (double *)malloc((size_t)n * sizeof(double));
    if (!b) return NAN;
    memcpy(b, x, (size_t)n * sizeof(double));
    dsort(b, n);
    if (n % 2) v = b[n / 2];
    else v = 0.5 * (b[n / 2 - 1] + b[n / 2]);
    free(b);
    return v;
}

/* 最近 window 个有限值的中位数。 */
static double hr_display(const double *hr, int n, int window) {
    double *tail;
    int i, m = 0;
    if (!hr || n <= 0) return NAN;
    tail = (double *)malloc((size_t)n * sizeof(double));
    if (!tail) return NAN;
    for (i = 0; i < n; ++i)
        if (isfinite(hr[i])) tail[m++] = hr[i];
    if (m == 0) { free(tail); return NAN; }
    if (m > window) {
        memmove(tail, tail + (m - window), (size_t)window * sizeof(double));
        m = window;
    }
    {
        double v = dmedian(tail, m);
        free(tail);
        return v;
    }
}

int aura_hrv_compute(const double *ibis_ms, const int *segment_ids, int n_ibis,
                     double total_duration_s, int total_time_slots,
                     int valid_time_slots,
                     const double *hr_series, int n_hr,
                     int mode, aura_hrv_result *out) {
    double sum = 0.0, ss = 0.0;
    double *ibis_copy = NULL;
    int i, start, max_seg = 0;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->rmssd_ms = NAN;
    out->sdrr_ms = NAN;
    out->pnn50_percent = NAN;
    out->hr_display_bpm = NAN;
    out->mean_ibi_ms = NAN;
    out->median_ibi_ms = NAN;
    out->min_ibi_ms = NAN;
    out->max_ibi_ms = NAN;
    out->coverage_ratio = (total_time_slots > 0)
        ? (double)valid_time_slots / (double)total_time_slots : 0.0;
    if (!ibis_ms || n_ibis <= 0) {
        snprintf(out->reason, sizeof(out->reason), "no_valid_ibis");
        return 0;
    }
    out->n_ibis_total = n_ibis;
    /* 辅助统计 */
    ibis_copy = (double *)malloc((size_t)n_ibis * sizeof(double));
    if (ibis_copy) {
        memcpy(ibis_copy, ibis_ms, (size_t)n_ibis * sizeof(double));
        out->median_ibi_ms = dmedian(ibis_copy, n_ibis);
    }
    out->min_ibi_ms = ibis_ms[0];
    out->max_ibi_ms = ibis_ms[0];
    for (i = 0; i < n_ibis; ++i) {
        sum += ibis_ms[i];
        if (ibis_ms[i] < out->min_ibi_ms) out->min_ibi_ms = ibis_ms[i];
        if (ibis_ms[i] > out->max_ibi_ms) out->max_ibi_ms = ibis_ms[i];
    }
    out->mean_ibi_ms = sum / (double)n_ibis;
    /* 门槛 */
    if (mode == 0) {
        if (total_duration_s < 30.0) {
            snprintf(out->reason, sizeof(out->reason), "duration_too_short");
            out->sufficient_data = 0;
            free(ibis_copy);
            return 0;
        }
        if (n_ibis < 30) {
            snprintf(out->reason, sizeof(out->reason), "total_ibis_too_few");
            out->sufficient_data = 0;
            free(ibis_copy);
            return 0;
        }
        if (total_time_slots > 0 && out->coverage_ratio < 0.80) {
            snprintf(out->reason, sizeof(out->reason), "coverage_too_low");
            out->sufficient_data = 0;
            free(ibis_copy);
            return 0;
        }
    } else {
        int max_run = 1, run = 1;
        for (i = 1; i < n_ibis; ++i) {
            if (segment_ids && segment_ids[i] == segment_ids[i - 1]) {
                run++;
                if (run > max_run) max_run = run;
            } else {
                run = 1;
            }
        }
        max_seg = max_run;
        if (max_seg < 240) {
            snprintf(out->reason, sizeof(out->reason), "max_segment_too_short");
            out->sufficient_data = 0;
            free(ibis_copy);
            return 0;
        }
    }
    /* RMSSD / pNN50 仅在同片段内差分 */
    {
        double sum_sq = 0.0;
        int n_diffs = 0, n50 = 0;
        start = 0;
        for (i = 1; i <= n_ibis; ++i) {
            int boundary = (i == n_ibis)
                || (segment_ids && segment_ids[i] != segment_ids[i - 1])
                || (!segment_ids && 0);
            if (!boundary) continue;
            {
                int j;
                for (j = start + 1; j < i; ++j) {
                    double d = ibis_ms[j] - ibis_ms[j - 1];
                    sum_sq += d * d;
                    if (fabs(d) > 50.0) n50++;
                    n_diffs++;
                }
            }
            start = i;
        }
        if (n_diffs > 0) {
            out->rmssd_ms = sqrt(sum_sq / (double)n_diffs);
            out->pnn50_percent = 100.0 * (double)n50 / (double)n_diffs;
            out->n_diffs_valid = n_diffs;
            out->n_diffs_50ms = n50;
        }
    }
    /* SDRR 全局样本标准差 */
    if (n_ibis >= 2) {
        double mean = out->mean_ibi_ms;
        for (i = 0; i < n_ibis; ++i) {
            double d = ibis_ms[i] - mean;
            ss += d * d;
        }
        out->sdrr_ms = sqrt(ss / (double)(n_ibis - 1));
    }
    /* HR_display */
    if (hr_series && n_hr > 0) {
        out->hr_display_bpm = hr_display(hr_series, n_hr, 5);
    } else {
        double *hr = (double *)malloc((size_t)n_ibis * sizeof(double));
        if (hr) {
            for (i = 0; i < n_ibis; ++i) hr[i] = 60000.0 / ibis_ms[i];
            out->hr_display_bpm = hr_display(hr, n_ibis, 5);
            free(hr);
        }
    }
    out->sufficient_data = 1;
    snprintf(out->reason, sizeof(out->reason), "ok");
    free(ibis_copy);
    return 0;
}
