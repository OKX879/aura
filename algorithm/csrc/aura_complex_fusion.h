/* 单窗复数融合接口。 */
#ifndef AURA_COMPLEX_FUSION_H
#define AURA_COMPLEX_FUSION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int n_active_channels;
    int n_active_bins;
    double mean_coherence;
} aura_fuse_stats;

typedef struct {
    int n_total;
    double mean_coherence;
    double mean_active_channels;
    double mean_active_bins;
    double amp_median;
    double phase_range_rad;
    double amp_reliable_ratio;
    double coherence_matrix_mean;
    double validity_matrix_mean;
    char fill_mode[32];
} aura_fuse_summary;

/* 复数输入、bin 权重与滤波系数。 */
int aura_fuse_window(const double *zr, const double *zi, int n, int c, int r,
                     const double *bin_weights, int center_bin, double fs,
                     double ref_channel, int neighborhood,
                     double sigma_r, double beta,
                     double coherence_threshold,
                     double channel_coh_power, double bin_coh_power,
                     double bin_coh_min, double amp_floor_frac,
                     const double *sos_cardiac, int ns_cardiac,
                     double *out_r, double *out_i, double *coh_mat,
                     unsigned char *frame_valid, aura_fuse_stats *stats);

/* 全程融合，时变轨迹用滑窗。 */
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
                         aura_fuse_summary *summary);

#ifdef __cplusplus
}
#endif

#endif /* AURA_COMPLEX_FUSION_H */
