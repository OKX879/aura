/* 端到端流水线接口。 */
#ifndef AURA_PIPELINE_H
#define AURA_PIPELINE_H

#include "aura_event.h"
#include "aura_hrv.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int frame_id;
    double hr_bpm;
    double hr_fft;
    double hr_acf;
    double hr_ppi;
    double hr_fused;
    double hr_conf;
    double resp_hz;
    double hr_snr;
    double hr_max_diff_bpm;
    int hr_consistent;
    int hr_confident;        /* 跟踪器已有稳定锚点 */
    int ppi_source;          /* PPI 来源编号 */
    int hr_rescued;          /* 高心率救援是否生效 */
    double w_fft, w_acf, w_ppi;
    int selected_bin;
    int candidate_bin;
    int presence;
    int motion;
    double motion_level_rad;
    double motion_excursion_rad;
    int n_beats;
    int n_segments;
    double fused_amp;
    double mean_coherence;
} AuraSnapshot;

typedef struct AuraPipeline AuraPipeline;

AuraPipeline *aura_pipeline_create(int channels, int bins, double fs,
                                   int window_n, int step_n);
void aura_pipeline_destroy(AuraPipeline *p);

/* 推入一帧复数数据。 */
int aura_pipeline_push(AuraPipeline *p, const float *z_re, const float *z_im);

/* 读取最近一次结果。 */
int aura_pipeline_snapshot(const AuraPipeline *p, AuraSnapshot *out);

/* 逐拍与 HRV 结果，开销较大。 */
int aura_pipeline_get_events(const AuraPipeline *p, aura_event_result *out);
int aura_pipeline_get_hrv(const AuraPipeline *p, int mode, aura_hrv_result *out);

#ifdef __cplusplus
}
#endif

#endif /* AURA_PIPELINE_H */
