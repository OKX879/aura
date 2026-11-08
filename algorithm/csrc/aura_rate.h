/* 心率跟踪器接口。 */
#ifndef AURA_RATE_H
#define AURA_RATE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double lo, hi, max_delta, min_confidence;
    double relock_gap, seed_range;
    int history_size, relock_streak, seed_streak, seed_max_tries;
    double history[8];
    int hlen;
    double value;
    int has_value;
    int relock_dir, relock_count;
    double relock_bank[8];
    int rb_len;
    double seed_bank[8];
    int sb_len, seed_tries;
} aura_rate_tracker;

void aura_rate_tracker_init(aura_rate_tracker *t);
void aura_rate_tracker_reset(aura_rate_tracker *t);
void aura_rate_tracker_force_relock(aura_rate_tracker *t, double value);

/* 更新跟踪器，返回是否接受。 */
int aura_rate_tracker_update(aura_rate_tracker *t, double value,
                             double confidence, double *out);

#ifdef __cplusplus
}
#endif

#endif /* AURA_RATE_H */
