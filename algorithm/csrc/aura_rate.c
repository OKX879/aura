/* 心率跟踪器实现。 */
#include "aura_rate.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void dsort(double *x, int n) {
    int i, j;
    for (i = 1; i < n; ++i) {
        double v = x[i];
        j = i - 1;
        while (j >= 0 && x[j] > v) { x[j + 1] = x[j]; --j; }
        x[j + 1] = v;
    }
}

static double dmedian(const double *src, int n) {
    double b[16];
    if (n <= 0) return 0.0;
    if (n > 16) n = 16;
    memcpy(b, src, (size_t)n * sizeof(double));
    dsort(b, n);
    if (n % 2) return b[n / 2];
    return 0.5 * (b[n / 2 - 1] + b[n / 2]);
}

/* 定长队列，满时丢最旧 */
static void push_capped(double *buf, int *len, int cap, double v) {
    if (cap <= 0) return;
    if (*len >= cap) {
        memmove(buf, buf + 1, (size_t)(cap - 1) * sizeof(double));
        *len = cap - 1;
    }
    if (*len >= cap) return;
    buf[(*len)++] = v;
}

static double round1(double v) {
    return round(v * 10.0) / 10.0;
}

/* 取跨度不超过 range 的最大簇，返回簇样本数，簇中位数写入 *med。
 * 首锚兜底用：双峰输入（一半谐波倍频、一半真值）取全体中位会落到错峰，
 * 最大簇始终贴着主群体。与 Python 侧 _largest_cluster 同一策略。 */
static int largest_cluster(const double *bank, int n, double range, double *med) {
    double b[8];
    int i, j, start = 0, best_len = 1, best_start = 0;
    if (!bank || n <= 0) { if (med) *med = 0.0; return 0; }
    if (n > 8) n = 8;
    memcpy(b, bank, (size_t)n * sizeof(double));
    for (i = 1; i < n; ++i) {
        double v = b[i];
        j = i - 1;
        while (j >= 0 && b[j] > v) { b[j + 1] = b[j]; --j; }
        b[j + 1] = v;
    }
    for (i = 0; i < n; ++i) {
        while (b[i] - b[start] > range) ++start;
        if (i - start + 1 > best_len) {
            best_len = i - start + 1;
            best_start = start;
        }
    }
    if (med) {
        int m = best_len;
        if (m % 2) *med = b[best_start + m / 2];
        else *med = 0.5 * (b[best_start + m / 2 - 1] + b[best_start + m / 2]);
    }
    return best_len;
}

void aura_rate_tracker_init(aura_rate_tracker *t) {
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->lo = 40.0;
    t->hi = 180.0;
    t->max_delta = 1.5;
    t->min_confidence = 0.30;
    t->history_size = 5;
    t->relock_gap = 15.0;
    t->relock_streak = 3;
    t->seed_streak = 3;
    t->seed_range = 8.0;
    t->seed_max_tries = 6;
    t->has_value = 0;
}

void aura_rate_tracker_reset(aura_rate_tracker *t) {
    if (!t) return;
    t->hlen = 0;
    t->value = 0.0;
    t->has_value = 0;
    t->relock_dir = 0;
    t->relock_count = 0;
    t->rb_len = 0;
    t->sb_len = 0;
    t->seed_tries = 0;
}

void aura_rate_tracker_force_relock(aura_rate_tracker *t, double value) {
    if (!t) return;
    if (!(isfinite(value) && value >= t->lo && value <= t->hi)) return;
    t->value = value;
    t->has_value = 1;
    t->hlen = 0;
    push_capped(t->history, &t->hlen, t->history_size, value);
    t->relock_dir = 0;
    t->relock_count = 0;
    t->rb_len = 0;
    t->sb_len = 0;
    t->seed_tries = 0;
}

int aura_rate_tracker_update(aura_rate_tracker *t, double value,
                             double confidence, double *out) {
    double v, conf, target, gap, delta;
    if (!t) return 0;
    if (!(isfinite(value) && value >= t->lo && value <= t->hi)) {
        if (out) *out = t->has_value ? round1(t->value) : NAN;
        return 0;
    }
    conf = isfinite(confidence) ? confidence : 0.0;
    if (conf < t->min_confidence) {
        if (out) *out = t->has_value ? round1(t->value) : NAN;
        return 0;
    }
    v = value;
    if (!t->has_value || t->hlen == 0) {
        int ready = 0, cn;
        double cmed = 0.0;
        /* bank 保留 seed_max_tries 个样本：双峰输入必须留够样本才能分出簇 */
        push_capped(t->seed_bank, &t->sb_len, t->seed_max_tries, v);
        t->seed_tries++;
        cn = largest_cluster(t->seed_bank, t->sb_len, t->seed_range, &cmed);
        ready = (cn >= t->seed_streak);
        if (!ready && t->seed_tries >= t->seed_max_tries) ready = 1;
        if (!ready) {
            if (out) *out = t->has_value ? round1(t->value) : NAN;
            return 0;
        }
        t->value = cmed;
        t->has_value = 1;
        t->hlen = 0;
        push_capped(t->history, &t->hlen, t->history_size, t->value);
        t->sb_len = 0;
        t->seed_tries = 0;
        if (out) *out = round1(t->value);
        return 1;
    }
    target = dmedian(t->history, t->hlen);
    gap = target - t->value;
    if (fabs(gap) > t->relock_gap) {
        int dir = (gap > 0.0) ? 1 : -1;
        int i;
        double mn, mx;
        if (dir == t->relock_dir) t->relock_count++;
        else { t->relock_dir = dir; t->relock_count = 1; }
        push_capped(t->relock_bank, &t->rb_len, t->relock_streak, v);
        mn = mx = t->relock_bank[0];
        for (i = 1; i < t->rb_len; ++i) {
            if (t->relock_bank[i] < mn) mn = t->relock_bank[i];
            if (t->relock_bank[i] > mx) mx = t->relock_bank[i];
        }
        if (t->relock_count >= t->relock_streak
                && t->rb_len >= t->relock_streak
                && (mx - mn) <= 10.0) {
            t->value = dmedian(t->relock_bank, t->rb_len);
            t->hlen = 0;
            push_capped(t->history, &t->hlen, t->history_size, t->value);
            t->relock_dir = 0;
            t->relock_count = 0;
            t->rb_len = 0;
            if (out) *out = round1(t->value);
            return 1;
        }
        if (out) *out = round1(t->value);
        return 0;
    }
    t->relock_dir = 0;
    t->relock_count = 0;
    t->rb_len = 0;
    push_capped(t->history, &t->hlen, t->history_size, v);
    target = dmedian(t->history, t->hlen);
    delta = target - t->value;
    if (delta > t->max_delta) delta = t->max_delta;
    if (delta < -t->max_delta) delta = -t->max_delta;
    t->value += delta;
    if (out) *out = round1(t->value);
    return 1;
}
