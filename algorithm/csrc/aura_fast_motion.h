/* 快速运动检测接口。 */
#ifndef AURA_FAST_MOTION_H
#define AURA_FAST_MOTION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double recent_s;
    double phase_threshold_rad;
    double excursion_threshold_rad;
    int quiet_windows_to_clear;
    int active;
    int quiet_streak;
} aura_fast_motion;

typedef struct {
    int active;
    int detected;
    double level_rad;
    double excursion_rad;
    int quiet_streak;
    int hold_remaining_windows;
    const char *reason;
} aura_fast_motion_snap;

void aura_fast_motion_init(aura_fast_motion *g);
void aura_fast_motion_reset(aura_fast_motion *g);

/* 输入窗口复数数据。 */
int aura_fast_motion_update(aura_fast_motion *g,
                            const double *zr, const double *zi,
                            int T, int C, int R, double fs,
                            aura_fast_motion_snap *out);

#ifdef __cplusplus
}
#endif

#endif /* AURA_FAST_MOTION_H */
