/* Bin 质量评分接口。 */
#ifndef AURA_BIN_SCORER_H
#define AURA_BIN_SCORER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int range_bin;
    double energy;
    double amp_stability;
    double phase_valid_ratio;
    double coherence;
    double periodicity;
    double jump_ratio;
    double motion_score;
    double dip_ratio;
    double spectral_snr;
    double total_score;
    double abs_median;
    int n_valid_channels;
    double hr_acf_bpm;
} aura_bin_quality;

/* 复数输入与滤波系数，返回 0 成功。 */
int aura_score_bin(const double *zr, const double *zi, int n, int c,
                   int range_bin, double fs,
                   const double *sos_cardiac, int ns_cardiac,
                   const double *sos_motion, int ns_motion,
                   aura_bin_quality *out);

/* 批量评分，输出每个 bin 的质量。 */
int aura_score_window(const double *zr, const double *zi, int n, int c, int r,
                      double fs,
                      const double *sos_cardiac, int ns_cardiac,
                      const double *sos_motion, int ns_motion,
                      aura_bin_quality *out);

#ifdef __cplusplus
}
#endif

#endif /* AURA_BIN_SCORER_H */
