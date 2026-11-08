/* HRV 指标接口。 */
#ifndef AURA_HRV_H
#define AURA_HRV_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double rmssd_ms;
    double sdrr_ms;
    double pnn50_percent;
    double hr_display_bpm;
    int n_ibis_total;
    int n_diffs_valid;
    int n_diffs_50ms;
    double coverage_ratio;
    int sufficient_data;
    char reason[96];
    double mean_ibi_ms;
    double median_ibi_ms;
    double min_ibi_ms;
    double max_ibi_ms;
} aura_hrv_result;

/* 计算 HRV 指标，mode 选择统计口径。 */
int aura_hrv_compute(const double *ibis_ms, const int *segment_ids, int n_ibis,
                     double total_duration_s, int total_time_slots,
                     int valid_time_slots,
                     const double *hr_series, int n_hr,
                     int mode, aura_hrv_result *out);

#ifdef __cplusplus
}
#endif

#endif /* AURA_HRV_H */
