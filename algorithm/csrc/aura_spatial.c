/* 轨迹与距离锁定实现。 */
#include "aura_spatial.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define INF 1e9

int aura_viterbi(const double *q, int k, int r,
                 double lambda1, double lambda2, int max_jump,
                 int *trajectory) {
    double *dp;
    int *parent;
    int kk, rr, pp;
    if (!q || !trajectory || k <= 0 || r <= 0) return -1;
    dp = (double *)malloc((size_t)k * r * sizeof(double));
    parent = (int *)malloc((size_t)k * r * sizeof(int));
    if (!dp || !parent) {
        free(dp); free(parent);
        return -1;
    }
    for (rr = 0; rr < r; ++rr) {
        dp[rr] = -q[rr];
        parent[rr] = -1;
    }
    for (kk = 1; kk < k; ++kk) {
        for (rr = 0; rr < r; ++rr) {
            double best = INF;
            int best_prev = 0;
            for (pp = 0; pp < r; ++pp) {
                int jump = abs(rr - pp);
                double trans, total;
                if (jump > max_jump) continue;
                trans = lambda1 * (double)jump;
                if (kk >= 2 && parent[(kk - 1) * r + pp] >= 0) {
                    int prev2 = parent[(kk - 1) * r + pp];
                    trans += lambda2 * (double)abs(rr - 2 * pp + prev2);
                }
                total = dp[(kk - 1) * r + pp] + trans - q[kk * r + rr];
                if (total < best) {
                    best = total;
                    best_prev = pp;
                }
            }
            dp[kk * r + rr] = best;
            parent[kk * r + rr] = best_prev;
        }
    }
    {
        int best = 0;
        double bestv = dp[(k - 1) * r];
        for (rr = 1; rr < r; ++rr) {
            if (dp[(k - 1) * r + rr] < bestv) {
                bestv = dp[(k - 1) * r + rr];
                best = rr;
            }
        }
        trajectory[k - 1] = best;
    }
    for (kk = k - 2; kk >= 0; --kk) {
        trajectory[kk] = parent[(kk + 1) * r + trajectory[kk + 1]];
    }
    free(dp); free(parent);
    return 0;
}

void aura_distance_lock_init(aura_distance_lock *l) {
    if (!l) return;
    memset(l, 0, sizeof(*l));
    l->min_quality = 0.12;
    l->switch_margin = 0.08;
    l->adjacent_confirm_windows = 2;
    l->jump_confirm_windows = 3;
    l->lost_hold_windows = 3;
    l->smooth_alpha = 0.38;
    aura_distance_lock_reset(l);
}

void aura_distance_lock_reset(aura_distance_lock *l) {
    if (!l) return;
    l->locked_bin = -1;
    l->continuous_bin = NAN;
    l->pending_bin = -1;
    l->switch_streak = 0;
    l->lost_streak = 0;
    l->initialized = 0;
}

static double sub_bin(int bin_index, const double *q, int r) {
    double left, center, right, denom, offset;
    if (bin_index <= 0 || bin_index >= r - 1) return (double)bin_index;
    left = q[bin_index - 1];
    center = q[bin_index];
    right = q[bin_index + 1];
    denom = left - 2.0 * center + right;
    if (fabs(denom) < 1e-9) return (double)bin_index;
    offset = 0.5 * (left - right) / denom;
    if (offset < -0.5) offset = -0.5;
    if (offset > 0.5) offset = 0.5;
    return (double)bin_index + offset;
}

static double clip01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

int aura_distance_lock_update(aura_distance_lock *l, int candidate,
                              const double *q_in, int r, aura_lock_out *out) {
    int top_i, status = 0;
    double top_score, viterbi_score, selected_score, margin, second = 0.0;
    int vit, jump, required, rr;
    double qbuf[512];
    const double *q = qbuf;
    if (!l || !q_in || !out || r <= 0 || r > 512) return -1;
    for (rr = 0; rr < r; ++rr) {
        qbuf[rr] = isfinite(q_in[rr]) ? q_in[rr] : 0.0;
    }
    top_i = 0;
    for (rr = 1; rr < r; ++rr) if (q[rr] > q[top_i]) top_i = rr;
    top_score = q[top_i];
    vit = candidate;
    if (vit < 0) vit = 0;
    if (vit >= r) vit = r - 1;
    viterbi_score = q[vit];
    candidate = (top_score >= viterbi_score + l->switch_margin) ? top_i : vit;

    if (!l->initialized) {
        l->locked_bin = candidate;
        l->continuous_bin = sub_bin(candidate, q, r);
        l->initialized = 1;
    }
    selected_score = q[l->locked_bin];

    {
        double best = -1e300, secondv = -1e300;
        for (rr = 0; rr < r; ++rr) {
            if (q[rr] > best) {
                secondv = best;
                best = q[rr];
            } else if (q[rr] > secondv) {
                secondv = q[rr];
            }
        }
        second = (r > 1) ? secondv : 0.0;
    }
    margin = top_score - second;
    if (margin < 0.0) margin = 0.0;

    if (top_score < l->min_quality) {
        l->lost_streak++;
        l->pending_bin = -1;
        l->switch_streak = 0;
        status = (l->lost_streak <= l->lost_hold_windows) ? 1 : 2;
    } else {
        l->lost_streak = 0;
        if (candidate == l->locked_bin) {
            l->pending_bin = -1;
            l->switch_streak = 0;
            status = 0;
        } else {
            if (candidate == l->pending_bin) {
                l->switch_streak++;
            } else {
                l->pending_bin = candidate;
                l->switch_streak = 1;
            }
            jump = abs(candidate - l->locked_bin);
            required = (jump <= 1) ? l->adjacent_confirm_windows
                                   : l->jump_confirm_windows;
            if (selected_score < l->min_quality
                    && top_score > l->min_quality + 0.18) {
                if (required > 2) required = 2;
            }
            if (l->switch_streak >= required) {
                l->locked_bin = candidate;
                l->pending_bin = -1;
                l->switch_streak = 0;
                selected_score = q[l->locked_bin];
                status = (jump > 1) ? 4 : 0;
            } else {
                status = 3;
            }
        }
    }

    {
        double sb = sub_bin(l->locked_bin, q, r);
        if (isnan(l->continuous_bin)) {
            l->continuous_bin = sb;
        } else {
            l->continuous_bin = (1.0 - l->smooth_alpha) * l->continuous_bin
                                + l->smooth_alpha * sb;
        }
    }
    out->selected_bin = l->locked_bin;
    out->continuous_bin = l->continuous_bin;
    out->confidence = clip01(0.72 * selected_score
                             + 0.28 * (margin / 0.20 < 1.0
                                       ? margin / 0.20 : 1.0));
    if (status == 1 || status == 2) {
        double f = 1.0 - (double)l->lost_streak
                   / ((double)l->lost_hold_windows + 2.0);
        out->confidence *= (f > 0.0 ? f : 0.0);
    }
    out->status = status;
    out->candidate_bin = candidate;
    out->instantaneous_bin = top_i;
    out->switch_streak = l->switch_streak;
    out->lost_streak = l->lost_streak;
    out->selected_score = selected_score;
    out->margin = margin;
    return 0;
}
