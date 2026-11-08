/* 内核向量测试运行器，读写二进制容器。 */
#include "aura_kernels.h"
#include "aura_bin_scorer.h"
#include "aura_spatial.h"
#include "aura_complex_fusion.h"
#include "aura_hr.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>

#define MAGIC "AURAK01\0"

typedef struct {
    char key[64];
    unsigned int kind;
    unsigned int rows;
    unsigned int cols;
    double *data;
} Vec;

typedef struct {
    Vec *v;
    int n;
    int cap;
} VecList;

static void vec_list_init(VecList *l) {
    l->v = NULL;
    l->n = 0;
    l->cap = 0;
}

static void vec_list_free(VecList *l) {
    int i;
    for (i = 0; i < l->n; ++i) free(l->v[i].data);
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

static int vec_list_add(VecList *l, const char *key, unsigned int kind,
                        unsigned int rows, unsigned int cols,
                        const double *data) {
    Vec *nv;
    if (l->n >= l->cap) {
        int nc = l->cap ? l->cap * 2 : 8;
        Vec *tmp = (Vec *)realloc(l->v, (size_t)nc * sizeof(Vec));
        if (!tmp) return -1;
        l->v = tmp;
        l->cap = nc;
    }
    nv = &l->v[l->n];
    memset(nv, 0, sizeof(*nv));
    snprintf(nv->key, sizeof(nv->key), "%s", key);
    nv->kind = kind;
    nv->rows = rows;
    nv->cols = cols;
    {
        size_t sz = (size_t)rows * cols;
        if (sz == 0) sz = 1;
        nv->data = (double *)malloc(sz * sizeof(double));
        if (!nv->data) return -1;
        if (data && rows && cols) {
            memcpy(nv->data, data, (size_t)rows * cols * sizeof(double));
        } else {
            memset(nv->data, 0, sz * sizeof(double));
        }
    }
    l->n++;
    return 0;
}

static Vec *find_vec(VecList *l, const char *key) {
    int i;
    for (i = 0; i < l->n; ++i) {
        if (strcmp(l->v[i].key, key) == 0) return &l->v[i];
    }
    return NULL;
}

static int read_container(const char *path, VecList *l) {
    FILE *f = fopen(path, "rb");
    unsigned int n = 0, i;
    char magic[8];
    if (!f) return -1;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, MAGIC, 8) != 0) {
        fclose(f);
        return -1;
    }
    if (fread(&n, sizeof(unsigned int), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    for (i = 0; i < n; ++i) {
        char key[64];
        unsigned int kind, rows, cols;
        double *data;
        if (fread(key, 1, 64, f) != 64 ||
            fread(&kind, sizeof(unsigned int), 1, f) != 1 ||
            fread(&rows, sizeof(unsigned int), 1, f) != 1 ||
            fread(&cols, sizeof(unsigned int), 1, f) != 1) {
            fclose(f);
            return -1;
        }
        data = (double *)malloc((size_t)rows * cols * sizeof(double));
        if (!data && rows && cols) {
            fclose(f);
            return -1;
        }
        if (fread(data, sizeof(double), (size_t)rows * cols, f) !=
            (size_t)rows * cols) {
            free(data);
            fclose(f);
            return -1;
        }
        if (vec_list_add(l, key, kind, rows, cols, data) != 0) {
            free(data);
            fclose(f);
            return -1;
        }
        free(data);
    }
    fclose(f);
    return 0;
}

static int write_container(const char *path, VecList *l) {
    FILE *f = fopen(path, "wb");
    unsigned int n, i;
    if (!f) return -1;
    fwrite(MAGIC, 1, 8, f);
    n = (unsigned int)l->n;
    fwrite(&n, sizeof(unsigned int), 1, f);
    for (i = 0; i < n; ++i) {
        Vec *v = &l->v[i];
        fwrite(v->key, 1, 64, f);
        fwrite(&v->kind, sizeof(unsigned int), 1, f);
        fwrite(&v->rows, sizeof(unsigned int), 1, f);
        fwrite(&v->cols, sizeof(unsigned int), 1, f);
        fwrite(v->data, sizeof(double),
               (size_t)v->rows * v->cols, f);
    }
    fclose(f);
    return 0;
}

static int op_fft(VecList *in, VecList *out, const char *id, const char *xk) {
    Vec *x = find_vec(in, xk);
    aura_complex *fft_out;
    double *data;
    int n, k;
    if (!x) return -1;
    n = (int)(x->rows * x->cols);
    fft_out = (aura_complex *)malloc((size_t)n * sizeof(aura_complex));
    data = (double *)malloc((size_t)2 * n * sizeof(double));
    if (!fft_out || !data) {
        free(fft_out); free(data);
        return -1;
    }
    if (aura_fft(x->data, NULL, n, fft_out) != 0) {
        free(fft_out); free(data);
        return -1;
    }
    for (k = 0; k < n; ++k) {
        data[k] = fft_out[k].re;
        data[n + k] = fft_out[k].im;
    }
    vec_list_add(out, id, 1, 2, (unsigned int)n, data);
    free(fft_out); free(data);
    return 0;
}

static int op_sosfiltfwd(VecList *in, VecList *out, const char *id,
                         const char *sosk, const char *xk) {
    Vec *sos = find_vec(in, sosk);
    Vec *x = find_vec(in, xk);
    double *y;
    int n, ns;
    if (!sos || !x) return -1;
    ns = (int)sos->rows;
    n = (int)(x->rows * x->cols);
    y = (double *)malloc((size_t)n * sizeof(double));
    if (!y) return -1;
    if (aura_sosfilt_fwd(sos->data, ns, x->data, n, y) != 0) {
        free(y);
        return -1;
    }
    vec_list_add(out, id, 0, 1, (unsigned int)n, y);
    free(y);
    return 0;
}

static int op_sosfiltzi(VecList *in, VecList *out, const char *id,
                        const char *sosk, const char *xk) {
    Vec *sos = find_vec(in, sosk);
    Vec *x = find_vec(in, xk);
    double *y;
    int n, ns;
    if (!sos || !x) return -1;
    ns = (int)sos->rows;
    n = (int)(x->rows * x->cols);
    y = (double *)malloc((size_t)n * sizeof(double));
    if (!y) return -1;
    if (aura_sosfilt_zi_fwd(sos->data, ns, x->data, n, y) != 0) {
        free(y);
        return -1;
    }
    vec_list_add(out, id, 0, 1, (unsigned int)n, y);
    free(y);
    return 0;
}

static int op_rtt(VecList *in, VecList *out, const char *id, const char *xk) {
    Vec *x = find_vec(in, xk);
    double *y;
    int n;
    if (!x) return -1;
    n = (int)(x->rows * x->cols);
    y = (double *)malloc((size_t)n * sizeof(double));
    if (!y) return -1;
    if (aura_rfft_roundtrip(x->data, n, y) != 0) { free(y); return -1; }
    vec_list_add(out, id, 0, 1, (unsigned int)n, y);
    free(y);
    return 0;
}

static int op_rhc(VecList *in, VecList *out, const char *id,
                  const char *sigk, double f_resp) {
    Vec *sig = find_vec(in, sigk);
    double *y;
    int n;
    if (!sig) return -1;
    n = (int)(sig->rows * sig->cols);
    y = (double *)malloc((size_t)n * sizeof(double));
    if (!y) return -1;
    aura_rhc_notch(sig->data, n, 100.0, f_resp, y);
    vec_list_add(out, id, 0, 1, (unsigned int)n, y);
    free(y);
    return 0;
}

static int op_hr(VecList *in, VecList *out, const char *id,
                 const char *sigk, const char *sosk, double f_resp,
                 int enable_acf) {
    Vec *sig = find_vec(in, sigk);
    Vec *sos = find_vec(in, sosk);
    double outv[4], *data;
    int n;
    if (!sig || !sos) return -1;
    n = (int)(sig->rows * sig->cols);
    if (aura_estimate_hr(sig->data, n, 100.0, f_resp, enable_acf,
                         sos->data, (int)sos->rows,
                         &outv[0], &outv[1], &outv[2], &outv[3]) != 0)
        return -1;
    data = (double *)malloc(4 * sizeof(double));
    if (!data) return -1;
    data[0] = outv[0]; data[1] = outv[1]; data[2] = outv[2]; data[3] = outv[3];
    vec_list_add(out, id, 1, 1, 4, data);
    free(data);
    return 0;
}

static int op_fusetraj(VecList *in, VecList *out, const char *id,
                       const char *zrk, const char *zik, const char *qk,
                       const char *tk, int n, int c, int r,
                       int win_n, int step_n, double fs, const char *sosk) {
    Vec *zr = find_vec(in, zrk);
    Vec *zi = find_vec(in, zik);
    Vec *q = find_vec(in, qk);
    Vec *tv = find_vec(in, tk);
    Vec *sos = find_vec(in, sosk);
    double *or_ = NULL, *oi_ = NULL, *ph = NULL, *am = NULL, *sum = NULL;
    unsigned char *rel = NULL;
    int *traj = NULL;
    aura_fuse_summary sm;
    int k, i;
    if (!zr || !zi || !q || !tv || !sos) return -1;
    k = (int)tv->rows * (int)tv->cols;
    traj = (int *)malloc((size_t)k * sizeof(int));
    or_ = (double *)malloc((size_t)n * sizeof(double));
    oi_ = (double *)malloc((size_t)n * sizeof(double));
    ph = (double *)malloc((size_t)n * sizeof(double));
    am = (double *)malloc((size_t)n * sizeof(double));
    rel = (unsigned char *)malloc((size_t)n);
    sum = (double *)malloc(10 * sizeof(double));
    if (!traj || !or_ || !oi_ || !ph || !am || !rel || !sum) goto tfail;
    for (i = 0; i < k; ++i) traj[i] = (int)tv->data[i];
    if (aura_fuse_trajectory(zr->data, zi->data, n, c, r, traj, k,
                             q->data, win_n, step_n, fs, 2, 1.0, 3.0,
                             0.5, 2.0, 2.0, 0.0, 0.3, 3.0, 30,
                             1.0, 0.7, sos->data, (int)sos->rows,
                             or_, oi_, ph, am, rel, &sm) != 0) goto tfail;
    {
        char key[80];
        snprintf(key, sizeof(key), "%s_r", id);
        vec_list_add(out, key, 0, 1, (unsigned int)n, or_);
        snprintf(key, sizeof(key), "%s_i", id);
        vec_list_add(out, key, 0, 1, (unsigned int)n, oi_);
        snprintf(key, sizeof(key), "%s_ph", id);
        vec_list_add(out, key, 0, 1, (unsigned int)n, ph);
        snprintf(key, sizeof(key), "%s_amp", id);
        vec_list_add(out, key, 0, 1, (unsigned int)n, am);
        sum[0] = (double)sm.n_total;
        sum[1] = sm.mean_coherence;
        sum[2] = sm.mean_active_channels;
        sum[3] = sm.mean_active_bins;
        sum[4] = sm.amp_median;
        sum[5] = sm.phase_range_rad;
        sum[6] = sm.amp_reliable_ratio;
        sum[7] = sm.coherence_matrix_mean;
        sum[8] = sm.validity_matrix_mean;
        sum[9] = (double)(strcmp(sm.fill_mode, "all_reliable") == 0 ? 0 :
                   strcmp(sm.fill_mode, "freeze") == 0 ? 1 :
                   strcmp(sm.fill_mode, "interpolation") == 0 ? 2 : 3);
        snprintf(key, sizeof(key), "%s_sum", id);
        vec_list_add(out, key, 1, 1, 10, sum);
    }
    free(traj); free(or_); free(oi_); free(ph); free(am); free(rel); free(sum);
    return 0;
tfail:
    free(traj); free(or_); free(oi_); free(ph); free(am); free(rel); free(sum);
    return -1;
}

static int op_fuse(VecList *in, VecList *out, const char *id,
                   const char *zrk, const char *zik, const char *bwk,
                   int n, int c, int r, int center, double fs,
                   const char *sosk) {
    Vec *zr = find_vec(in, zrk);
    Vec *zi = find_vec(in, zik);
    Vec *bw = find_vec(in, bwk);
    Vec *sos = find_vec(in, sosk);
    double *or_ = NULL, *oi_ = NULL, *coh = NULL, *stats = NULL;
    unsigned char *fv = NULL;
    aura_fuse_stats st;
    int i, sumv = 0;
    if (!zr || !zi || !bw || !sos) return -1;
    or_ = (double *)malloc((size_t)n * sizeof(double));
    oi_ = (double *)malloc((size_t)n * sizeof(double));
    coh = (double *)malloc((size_t)c * r * sizeof(double));
    fv = (unsigned char *)malloc((size_t)n * c * r);
    stats = (double *)malloc(3 * sizeof(double));
    if (!or_ || !oi_ || !coh || !fv || !stats) {
        free(or_); free(oi_); free(coh); free(fv); free(stats);
        return -1;
    }
    if (aura_fuse_window(zr->data, zi->data, n, c, r, bw->data, center,
                         fs, 0.0, 2, 1.0, 3.0, 0.5, 2.0, 2.0, 0.0, 0.3,
                         sos->data, (int)sos->rows,
                         or_, oi_, coh, fv, &st) != 0) {
        free(or_); free(oi_); free(coh); free(fv); free(stats);
        return -1;
    }
    for (i = 0; i < n * c * r; ++i) sumv += fv[i] ? 1 : 0;
    stats[0] = (double)st.n_active_channels;
    stats[1] = (double)st.n_active_bins;
    stats[2] = st.mean_coherence;
    {
        char key[80];
        snprintf(key, sizeof(key), "%s_r", id);
        vec_list_add(out, key, 0, 1, (unsigned int)n, or_);
        snprintf(key, sizeof(key), "%s_i", id);
        vec_list_add(out, key, 0, 1, (unsigned int)n, oi_);
        snprintf(key, sizeof(key), "%s_coh", id);
        vec_list_add(out, key, 1, (unsigned int)c, (unsigned int)r, coh);
        snprintf(key, sizeof(key), "%s_stats", id);
        vec_list_add(out, key, 1, 1, 3, stats);
        snprintf(key, sizeof(key), "%s_valid", id);
        {
            double v = (double)sumv;
            vec_list_add(out, key, 1, 1, 1, &v);
        }
    }
    free(or_); free(oi_); free(coh); free(fv); free(stats);
    return 0;
}

static int op_viterbi(VecList *in, VecList *out, const char *id,
                      const char *qk, double l1, double l2, int max_jump) {
    Vec *q = find_vec(in, qk);
    int *traj;
    double *data;
    int k, r, i;
    if (!q) return -1;
    k = (int)q->rows;
    r = (int)q->cols;
    traj = (int *)malloc((size_t)k * sizeof(int));
    data = (double *)malloc((size_t)k * sizeof(double));
    if (!traj || !data) {
        free(traj); free(data);
        return -1;
    }
    if (aura_viterbi(q->data, k, r, l1, l2, max_jump, traj) != 0) {
        free(traj); free(data);
        return -1;
    }
    for (i = 0; i < k; ++i) data[i] = (double)traj[i];
    vec_list_add(out, id, 0, 1, (unsigned int)k, data);
    free(traj); free(data);
    return 0;
}

static int op_lock(VecList *in, VecList *out, const char *id,
                   const char *qk, const char *ck) {
    Vec *q = find_vec(in, qk);
    Vec *c = find_vec(in, ck);
    aura_distance_lock lock;
    aura_lock_out lo;
    double *data;
    int k, r, i;
    if (!q || !c) return -1;
    k = (int)q->rows;
    r = (int)q->cols;
    data = (double *)malloc((size_t)k * 3 * sizeof(double));
    if (!data) return -1;
    aura_distance_lock_init(&lock);
    for (i = 0; i < k; ++i) {
        int cand = (int)c->data[i];
        if (aura_distance_lock_update(&lock, cand, &q->data[i * r], r,
                                      &lo) != 0) {
            free(data);
            return -1;
        }
        data[i * 3 + 0] = (double)lo.selected_bin;
        data[i * 3 + 1] = lo.confidence;
        data[i * 3 + 2] = (double)lo.status;
    }
    vec_list_add(out, id, 1, (unsigned int)k, 3, data);
    free(data);
    return 0;
}

static int op_scorebin(VecList *in, VecList *out, const char *id,
                       const char *zrk, const char *zik,
                       int n, int c, int r, double fs,
                       const char *sosk_c, const char *sosk_m) {
    Vec *zr = find_vec(in, zrk);
    Vec *zi = find_vec(in, zik);
    Vec *sosc = find_vec(in, sosk_c);
    Vec *sosm = find_vec(in, sosk_m);
    aura_bin_quality *q;
    double *data;
    int ri;
    if (!zr || !zi || !sosc || !sosm) return -1;
    if ((int)(zr->rows * zr->cols) != n * c * r) return -1;
    q = (aura_bin_quality *)calloc((size_t)r, sizeof(aura_bin_quality));
    data = (double *)malloc((size_t)r * 14 * sizeof(double));
    if (!q || !data) {
        free(q); free(data);
        return -1;
    }
    if (aura_score_window(zr->data, zi->data, n, c, r, fs,
                          sosc->data, (int)sosc->rows,
                          sosm->data, (int)sosm->rows, q) != 0) {
        free(q); free(data);
        return -1;
    }
    for (ri = 0; ri < r; ++ri) {
        data[ri * 14 + 0] = (double)q[ri].range_bin;
        data[ri * 14 + 1] = q[ri].energy;
        data[ri * 14 + 2] = q[ri].amp_stability;
        data[ri * 14 + 3] = q[ri].phase_valid_ratio;
        data[ri * 14 + 4] = q[ri].coherence;
        data[ri * 14 + 5] = q[ri].periodicity;
        data[ri * 14 + 6] = q[ri].jump_ratio;
        data[ri * 14 + 7] = q[ri].motion_score;
        data[ri * 14 + 8] = q[ri].dip_ratio;
        data[ri * 14 + 9] = q[ri].spectral_snr;
        data[ri * 14 + 10] = q[ri].total_score;
        data[ri * 14 + 11] = q[ri].abs_median;
        data[ri * 14 + 12] = (double)q[ri].n_valid_channels;
        data[ri * 14 + 13] = q[ri].hr_acf_bpm;
    }
    vec_list_add(out, id, 1, (unsigned int)r, 14, data);
    free(q); free(data);
    return 0;
}

static int op_odd_ext(VecList *in, VecList *out, const char *id,
                      const char *xk, int edge) {
    Vec *x = find_vec(in, xk);
    double *y;
    int n;
    if (!x) return -1;
    n = (int)(x->rows * x->cols);
    y = (double *)malloc((size_t)(n + 2 * edge) * sizeof(double));
    if (!y) return -1;
    aura_odd_ext(x->data, n, edge, y);
    vec_list_add(out, id, 0, 1, (unsigned int)(n + 2 * edge), y);
    free(y);
    return 0;
}

static int op_sosfiltfilt_full(VecList *in, VecList *out, const char *id,
                               const char *sosk, const char *xk,
                               int padlen) {
    Vec *sos = find_vec(in, sosk);
    Vec *x = find_vec(in, xk);
    double *y;
    int n, ns, edge = 0, ext_n;
    if (!sos || !x) return -1;
    ns = (int)sos->rows;
    n = (int)(x->rows * x->cols);
    if (padlen < 0) {
        int ntaps = 2 * ns + 1, nz_b = 0, nz_a = 0, s;
        for (s = 0; s < ns; ++s) {
            if (sos->data[6 * s + 2] == 0.0) nz_b++;
            if (sos->data[6 * s + 5] == 0.0) nz_a++;
        }
        ntaps -= (nz_b < nz_a ? nz_b : nz_a);
        edge = 3 * ntaps;
    } else {
        edge = padlen;
    }
    ext_n = n + 2 * edge;
    y = (double *)malloc((size_t)ext_n * sizeof(double));
    if (!y) return -1;
    if (aura_sosfiltfilt_full(sos->data, ns, x->data, n, y,
                              padlen, &edge) != 0) {
        free(y);
        return -1;
    }
    vec_list_add(out, id, 0, 1, (unsigned int)(n + 2 * edge), y);
    free(y);
    return 0;
}

static int op_sosfiltfilt(VecList *in, VecList *out, const char *id,
                          const char *sosk, const char *xk, int padlen) {
    Vec *sos = find_vec(in, sosk);
    Vec *x = find_vec(in, xk);
    double *y;
    int n, ns;
    if (!sos || !x) return -1;
    ns = (int)sos->rows;
    n = (int)(x->rows * x->cols);
    y = (double *)malloc((size_t)n * sizeof(double));
    if (!y) return -1;
    if (aura_sosfiltfilt(sos->data, ns, x->data, n, y, padlen) != 0) {
        free(y);
        return -1;
    }
    vec_list_add(out, id, 0, 1, (unsigned int)n, y);
    free(y);
    return 0;
}

static int op_welch(VecList *in, VecList *out, const char *id,
                    const char *xk, double fs, int nperseg) {
    Vec *x = find_vec(in, xk);
    double *f, *p, *data;
    int n, nf;
    if (!x) return -1;
    n = (int)(x->rows * x->cols);
    f = (double *)malloc((size_t)(n / 2 + 2) * sizeof(double));
    p = (double *)malloc((size_t)(n / 2 + 2) * sizeof(double));
    if (!f || !p) {
        free(f); free(p);
        return -1;
    }
    nf = aura_welch(x->data, n, fs, nperseg, f, p);
    if (nf < 0) {
        free(f); free(p);
        return -1;
    }
    data = (double *)malloc((size_t)2 * nf * sizeof(double));
    if (!data) {
        free(f); free(p);
        return -1;
    }
    memcpy(data, f, (size_t)nf * sizeof(double));
    memcpy(data + nf, p, (size_t)nf * sizeof(double));
    vec_list_add(out, id, 1, 2, (unsigned int)nf, data);
    free(f); free(p); free(data);
    return 0;
}

static int op_detrend(VecList *in, VecList *out, const char *id,
                      const char *xk) {
    Vec *x = find_vec(in, xk);
    double *y;
    int n;
    if (!x) return -1;
    n = (int)(x->rows * x->cols);
    y = (double *)malloc((size_t)n * sizeof(double));
    if (!y) return -1;
    aura_detrend_linear(x->data, n, y);
    vec_list_add(out, id, 0, 1, (unsigned int)n, y);
    free(y);
    return 0;
}

static int op_trapezoid(VecList *in, VecList *out, const char *id,
                        const char *yk, const char *xk) {
    Vec *y = find_vec(in, yk);
    Vec *x = xk[0] ? find_vec(in, xk) : NULL;
    double val;
    int n;
    if (!y) return -1;
    n = (int)(y->rows * y->cols);
    val = aura_trapezoid(y->data, x ? x->data : NULL, n);
    vec_list_add(out, id, 1, 1, 1, &val);
    return 0;
}

static int op_acf(VecList *in, VecList *out, const char *id, const char *xk) {
    Vec *x = find_vec(in, xk);
    double *acf;
    int n;
    if (!x) return -1;
    n = (int)(x->rows * x->cols);
    acf = (double *)malloc((size_t)n * sizeof(double));
    if (!acf) return -1;
    aura_acf(x->data, n, acf);
    vec_list_add(out, id, 0, 1, (unsigned int)n, acf);
    free(acf);
    return 0;
}

static int op_peaks(VecList *in, VecList *out, const char *id,
                    const char *xk, double distance, double height,
                    double prom) {
    Vec *x = find_vec(in, xk);
    int n, np, *peaks;
    double *heights, *proms, *data;
    if (!x) return -1;
    n = (int)(x->rows * x->cols);
    peaks = (int *)malloc((size_t)(n / 2 + 2) * sizeof(int));
    heights = (double *)malloc((size_t)(n / 2 + 2) * sizeof(double));
    proms = (double *)malloc((size_t)(n / 2 + 2) * sizeof(double));
    if (!peaks || !heights || !proms) {
        free(peaks); free(heights); free(proms);
        return -1;
    }
    np = aura_find_peaks(x->data, n, distance, height, prom,
                         peaks, n / 2 + 2, heights, proms);
    if (np < 0) {
        free(peaks); free(heights); free(proms);
        return -1;
    }
    data = (double *)malloc((size_t)3 * np * sizeof(double));
    if (!data && np) {
        free(peaks); free(heights); free(proms);
        return -1;
    }
    if (np) {
        int k;
        for (k = 0; k < np; ++k) {
            data[k] = (double)peaks[k];
            data[np + k] = heights[k];
            data[2 * np + k] = proms[k];
        }
    }
    vec_list_add(out, id, 1, 3, (unsigned int)np, data);
    free(peaks); free(heights); free(proms); free(data);
    return 0;
}

static int run_jobs(const char *jobs_path, VecList *in, VecList *out) {
    FILE *f = fopen(jobs_path, "r");
    char line[1024];
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        char op[64], id[64], a[64], b[64];
        int padlen, nperseg, n;
        double fs, distance, height, prom;
        double l1v = 0.3, l2v = 0.15;
        int maxj = 2;
        a[0] = b[0] = 0;
        n = sscanf(line, "%63s %63s %63s %63s %d", op, id, a, b, &padlen);
        if (n < 2 || op[0] == '#') continue;
        if (strcmp(op, "fft") == 0) {
            if (op_fft(in, out, id, a) != 0)
                fprintf(stderr, "[phase1] fft failed: %s\n", line);
        } else if (strcmp(op, "sosfiltfilt") == 0) {
            if (n < 5) padlen = -1;
            if (op_sosfiltfilt(in, out, id, a, b, padlen) != 0)
                fprintf(stderr, "[phase1] sosfiltfilt failed: %s\n", line);
        } else if (strcmp(op, "odd_ext") == 0) {
            if (sscanf(line, "%63s %63s %63s %d", op, id, a, &padlen) != 4) {
                fprintf(stderr, "[phase1] odd_ext parse failed: %s\n", line);
                continue;
            }
            if (op_odd_ext(in, out, id, a, padlen) != 0)
                fprintf(stderr, "[phase1] odd_ext failed: %s\n", line);
        } else if (strcmp(op, "rtt") == 0) {
            if (op_rtt(in, out, id, a) != 0)
                fprintf(stderr, "[phase1] rtt failed: %s\n", line);
        } else if (strcmp(op, "rhc") == 0) {
            double fr;
            if (sscanf(line, "%63s %63s %63s %lf", op, id, a, &fr) != 4) {
                fprintf(stderr, "[phase1] rhc parse failed: %s\n", line);
                continue;
            }
            if (op_rhc(in, out, id, a, fr) != 0)
                fprintf(stderr, "[phase1] rhc failed: %s\n", line);
        } else if (strcmp(op, "hr") == 0) {
            double fr;
            int ea;
            if (sscanf(line, "%63s %63s %63s %63s %lf %d", op, id, a, b,
                       &fr, &ea) != 6) {
                fprintf(stderr, "[phase1] hr parse failed: %s\n", line);
                continue;
            }
            if (op_hr(in, out, id, a, b, fr, ea) != 0)
                fprintf(stderr, "[phase1] hr failed: %s\n", line);
        } else if (strcmp(op, "fusetraj") == 0) {
            int nn, cc, rr, wn, sn;
            double ff;
            char ssk[64];
            if (sscanf(line, "%63s %63s %63s %63s %63s %63s %d %d %d %d %d %lf %63s",
                       op, id, a, b, ssk, ssk, &nn, &cc, &rr, &wn, &sn, &ff,
                       ssk) != 13) {
                fprintf(stderr, "[phase1] fusetraj parse failed: %s\n", line);
                continue;
            }
            {
                char zrk[64], zik[64], qk[64], tk[64], sosk[64];
                if (sscanf(line, "%63s %63s %63s %63s %63s %63s %d %d %d %d %d %lf %63s",
                           op, id, zrk, zik, qk, tk, &nn, &cc, &rr, &wn, &sn,
                           &ff, sosk) != 13) {
                    fprintf(stderr, "[phase1] fusetraj parse2 failed: %s\n", line);
                    continue;
                }
                if (op_fusetraj(in, out, id, zrk, zik, qk, tk, nn, cc, rr,
                                wn, sn, ff, sosk) != 0)
                    fprintf(stderr, "[phase1] fusetraj failed: %s\n", line);
            }
        } else if (strcmp(op, "fuse") == 0) {
            int nf, cf, rf, ctr;
            double fsf;
            char sosk[64];
            if (sscanf(line, "%63s %63s %63s %63s %63s %d %d %d %d %lf %63s",
                       op, id, a, b, sosk, &nf, &cf, &rf, &ctr, &fsf,
                       sosk) != 11) {
                fprintf(stderr, "[phase1] fuse parse failed: %s\n", line);
                continue;
            }
            /* 第 5 个 token 是权重键，需重解析 */
            {
                char zrk[64], zik[64], bwk[64], ssk[64];
                int nn, cc, rr, ce;
                double ff;
                if (sscanf(line, "%63s %63s %63s %63s %63s %d %d %d %d %lf %63s",
                           op, id, zrk, zik, bwk, &nn, &cc, &rr, &ce, &ff,
                           ssk) != 11) {
                    fprintf(stderr, "[phase1] fuse parse2 failed: %s\n", line);
                    continue;
                }
                if (op_fuse(in, out, id, zrk, zik, bwk, nn, cc, rr, ce, ff,
                            ssk) != 0)
                    fprintf(stderr, "[phase1] fuse failed: %s\n", line);
            }
        } else if (strcmp(op, "viterbi") == 0) {
            if (sscanf(line, "%63s %63s %63s %lf %lf %d", op, id, a,
                       &l1v, &l2v, &maxj) != 6) {
                fprintf(stderr, "[phase1] viterbi parse failed: %s\n", line);
                continue;
            }
            if (op_viterbi(in, out, id, a, l1v, l2v, maxj) != 0)
                fprintf(stderr, "[phase1] viterbi failed: %s\n", line);
        } else if (strcmp(op, "lock") == 0) {
            if (op_lock(in, out, id, a, b) != 0)
                fprintf(stderr, "[phase1] lock failed: %s\n", line);
        } else if (strcmp(op, "scorebin") == 0) {
            int nbin, cbin, rbin;
            double fsbin;
            char sosc[64], sosm[64];
            if (sscanf(line, "%63s %63s %63s %63s %d %d %d %lf %63s %63s",
                       op, id, a, b, &nbin, &cbin, &rbin, &fsbin,
                       sosc, sosm) != 10) {
                fprintf(stderr, "[phase1] scorebin parse failed: %s\n", line);
                continue;
            }
            if (op_scorebin(in, out, id, a, b, nbin, cbin, rbin, fsbin,
                            sosc, sosm) != 0)
                fprintf(stderr, "[phase1] scorebin failed: %s\n", line);
        } else if (strcmp(op, "sosfiltfiltfull") == 0) {
            if (n < 5) padlen = -1;
            if (op_sosfiltfilt_full(in, out, id, a, b, padlen) != 0)
                fprintf(stderr, "[phase1] sosfiltfiltfull failed: %s\n", line);
        } else if (strcmp(op, "sosfilt") == 0) {
            if (op_sosfiltfwd(in, out, id, a, b) != 0)
                fprintf(stderr, "[phase1] sosfilt failed: %s\n", line);
        } else if (strcmp(op, "sosfiltzi") == 0) {
            if (op_sosfiltzi(in, out, id, a, b) != 0)
                fprintf(stderr, "[phase1] sosfiltzi failed: %s\n", line);
        } else if (strcmp(op, "welch") == 0) {
            if (sscanf(line, "%63s %63s %63s %lf %d", op, id, a, &fs,
                       &nperseg) != 5) {
                fprintf(stderr, "[phase1] welch parse failed: %s\n", line);
                continue;
            }
            if (op_welch(in, out, id, a, fs, nperseg) != 0)
                fprintf(stderr, "[phase1] welch failed: %s\n", line);
        } else if (strcmp(op, "detrend") == 0) {
            if (op_detrend(in, out, id, a) != 0)
                fprintf(stderr, "[phase1] detrend failed: %s\n", line);
        } else if (strcmp(op, "trapezoid") == 0) {
            if (op_trapezoid(in, out, id, a, b) != 0)
                fprintf(stderr, "[phase1] trapezoid failed: %s\n", line);
        } else if (strcmp(op, "acf") == 0) {
            if (op_acf(in, out, id, a) != 0)
                fprintf(stderr, "[phase1] acf failed: %s\n", line);
        } else if (strcmp(op, "peaks") == 0) {
            if (sscanf(line, "%63s %63s %63s %lf %lf %lf", op, id, a,
                       &distance, &height, &prom) != 6) {
                fprintf(stderr, "[phase1] peaks parse failed: %s\n", line);
                continue;
            }
            if (op_peaks(in, out, id, a, distance, height, prom) != 0)
                fprintf(stderr, "[phase1] peaks failed: %s\n", line);
        } else {
            fprintf(stderr, "[phase1] unknown op: %s\n", op);
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    VecList in, out;
    int rc;
    if (argc < 4) {
        fprintf(stderr, "usage: %s vectors.bin jobs.txt out.bin\n", argv[0]);
        return 2;
    }
    vec_list_init(&in);
    vec_list_init(&out);
    rc = read_container(argv[1], &in);
    if (rc != 0) {
        fprintf(stderr, "[phase1] cannot read %s\n", argv[1]);
        return 2;
    }
    rc = run_jobs(argv[2], &in, &out);
    if (rc != 0) {
        fprintf(stderr, "[phase1] cannot read jobs %s\n", argv[2]);
        return 2;
    }
    rc = write_container(argv[3], &out);
    if (rc != 0) {
        fprintf(stderr, "[phase1] cannot write %s\n", argv[3]);
        return 2;
    }
    printf("[phase1] in=%d out=%d -> %s\n", in.n, out.n, argv[3]);
    vec_list_free(&in);
    vec_list_free(&out);
    return 0;
}
