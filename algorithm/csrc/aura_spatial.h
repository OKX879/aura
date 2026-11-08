/* 轨迹与距离锁定接口。 */
#ifndef AURA_SPATIAL_H
#define AURA_SPATIAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Viterbi 求最优 bin 轨迹。 */
int aura_viterbi(const double *q, int k, int r,
                 double lambda1, double lambda2, int max_jump,
                 int *trajectory);

typedef struct {
    double min_quality;
    double switch_margin;
    int adjacent_confirm_windows;
    int jump_confirm_windows;
    int lost_hold_windows;
    double smooth_alpha;
    int locked_bin;
    double continuous_bin;
    int pending_bin;
    int switch_streak;
    int lost_streak;
    int initialized;
} aura_distance_lock;

typedef struct {
    int selected_bin;
    double continuous_bin;
    double confidence;
    int status;          /* 状态：跟踪/保持/丢失/捕获/重获 */
    int candidate_bin;
    int instantaneous_bin;
    int switch_streak;
    int lost_streak;
    double selected_score;
    double margin;
} aura_lock_out;

void aura_distance_lock_init(aura_distance_lock *l);
void aura_distance_lock_reset(aura_distance_lock *l);
int aura_distance_lock_update(aura_distance_lock *l, int candidate,
                              const double *q, int r, aura_lock_out *out);

#ifdef __cplusplus
}
#endif

#endif /* AURA_SPATIAL_H */
