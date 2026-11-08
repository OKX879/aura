/* 数值内核实现。 */
#include "aura_kernels.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NONE_THRESHOLD (-1e308)

/* FFT */
static int next_pow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

static void fft_radix2(aura_complex *a, int n, int inverse) {
    int i, j, len;
    for (i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            aura_complex t = a[i];
            a[i] = a[j];
            a[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double ang = (inverse ? 2.0 : -2.0) * M_PI / (double)len;
        aura_complex wl = {cos(ang), sin(ang)};
        for (i = 0; i < n; i += len) {
            aura_complex w = {1.0, 0.0};
            int half = len >> 1;
            for (j = 0; j < half; ++j) {
                aura_complex u = a[i + j];
                aura_complex v = a[i + j + half];
                double vr = v.re * w.re - v.im * w.im;
                double vi = v.re * w.im + v.im * w.re;
                a[i + j].re = u.re + vr;
                a[i + j].im = u.im + vi;
                a[i + j + half].re = u.re - vr;
                a[i + j + half].im = u.im - vi;
                double nwr = w.re * wl.re - w.im * wl.im;
                w.im = w.re * wl.im + w.im * wl.re;
                w.re = nwr;
            }
        }
    }
    if (inverse) {
        for (i = 0; i < n; ++i) {
            a[i].re /= (double)n;
            a[i].im /= (double)n;
        }
    }
}

static int fft_bluestein(const double *xr, const double *xi, int n,
                         aura_complex *out) {
    int k, m = next_pow2(2 * n - 1);
    aura_complex *a = (aura_complex *)calloc((size_t)m, sizeof(aura_complex));
    aura_complex *b = (aura_complex *)calloc((size_t)m, sizeof(aura_complex));
    aura_complex *w = (aura_complex *)malloc((size_t)n * sizeof(aura_complex));
    if (!a || !b || !w) {
        free(a); free(b); free(w);
        return -1;
    }
    for (k = 0; k < n; ++k) {
        double xik = xi ? xi[k] : 0.0;
        double ang = -M_PI * ((double)k * (double)k) / (double)n;
        w[k].re = cos(ang);
        w[k].im = sin(ang);
        a[k].re = xr[k] * w[k].re - xik * w[k].im;
        a[k].im = xr[k] * w[k].im + xik * w[k].re;
        b[k].re = w[k].re;
        b[k].im = -w[k].im;
        if (k > 0) {
            b[m - k].re = w[k].re;
            b[m - k].im = -w[k].im;
        }
    }
    fft_radix2(a, m, 0);
    fft_radix2(b, m, 0);
    for (k = 0; k < m; ++k) {
        double re = a[k].re * b[k].re - a[k].im * b[k].im;
        double im = a[k].re * b[k].im + a[k].im * b[k].re;
        a[k].re = re;
        a[k].im = im;
    }
    fft_radix2(a, m, 1);
    for (k = 0; k < n; ++k) {
        out[k].re = a[k].re * w[k].re - a[k].im * w[k].im;
        out[k].im = a[k].re * w[k].im + a[k].im * w[k].re;
    }
    free(a); free(b); free(w);
    return 0;
}

int aura_fft(const double *xr, const double *xi, int n, aura_complex *out) {
    int i;
    if (n <= 0 || !xr || !out) return -1;
    if ((n & (n - 1)) == 0) {
        for (i = 0; i < n; ++i) {
            out[i].re = xr[i];
            out[i].im = xi ? xi[i] : 0.0;
        }
        fft_radix2(out, n, 0);
        return 0;
    }
    return fft_bluestein(xr, xi, n, out);
}

int aura_ifft(const double *xr, const double *xi, int n, aura_complex *out) {
    int i;
    double *cr = NULL, *ci = NULL;
    if (n <= 0 || !xr) return -1;
    cr = (double *)malloc((size_t)n * sizeof(double));
    ci = (double *)malloc((size_t)n * sizeof(double));
    if (!cr || !ci) {
        free(cr); free(ci);
        return -1;
    }
    for (i = 0; i < n; ++i) {
        cr[i] = xr[i];
        ci[i] = -(xi ? xi[i] : 0.0);
    }
    if (aura_fft(cr, ci, n, out) != 0) {
        free(cr); free(ci);
        return -1;
    }
    for (i = 0; i < n; ++i) {
        out[i].re /= (double)n;
        out[i].im = -out[i].im / (double)n;
    }
    free(cr); free(ci);
    return 0;
}

/* 线性去趋势 */
void aura_detrend_linear(const double *x, int n, double *y) {
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int i;
    if (!x || !y || n <= 0) return;
    if (n == 1) {
        y[0] = 0.0;
        return;
    }
    for (i = 0; i < n; ++i) {
        sx += (double)i;
        sy += x[i];
        sxx += (double)i * (double)i;
        sxy += (double)i * x[i];
    }
    {
        double denom = (double)n * sxx - sx * sx;
        double slope = (denom != 0.0)
            ? ((double)n * sxy - sx * sy) / denom : 0.0;
        double intercept = (sy - slope * sx) / (double)n;
        for (i = 0; i < n; ++i) y[i] = x[i] - (intercept + slope * (double)i);
    }
}

/* 梯形积分 */
double aura_trapezoid(const double *y, const double *x, int n) {
    double acc = 0.0;
    int i;
    if (!y || n <= 1) return 0.0;
    for (i = 1; i < n; ++i) {
        double dx = x ? (x[i] - x[i - 1]) : 1.0;
        acc += 0.5 * dx * (y[i] + y[i - 1]);
    }
    return acc;
}

/* 自相关 */
void aura_acf(const double *x, int n, double *acf) {
    int k, i;
    double a0, mean = 0.0;
    if (!x || !acf || n <= 0) return;
    for (i = 0; i < n; ++i) mean += x[i];
    mean /= (double)n;
    for (k = 0; k < n; ++k) {
        double s = 0.0;
        for (i = 0; i + k < n; ++i) {
            s += (x[i] - mean) * (x[i + k] - mean);
        }
        acf[k] = s;
    }
    a0 = acf[0];
    if (a0 > 0.0) {
        for (k = 0; k < n; ++k) acf[k] /= a0;
    }
}

/* SOS 滤波 */
static void sos_section_zi(double b0, double b1, double b2,
                           double a0, double a1, double a2,
                           double *z1, double *z2) {
    double y_inf, d1, d2;
    if (a0 != 1.0) {
        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 /= a0; a2 /= a0; a0 = 1.0;
    }
    y_inf = (b0 + b1 + b2) / (a0 + a1 + a2);
    d1 = b1 - y_inf * a1;
    d2 = b2 - y_inf * a2;
    *z1 = d1 + d2;
    *z2 = d2;
}

static void sosfilt(const double *sos, int ns, const double *x, int n,
                    double *y, double *zi) {
    int i, s;
    for (i = 0; i < n; ++i) {
        double v = x[i];
        for (s = 0; s < ns; ++s) {
            const double *c = sos + 6 * s;
            double b0 = c[0], b1 = c[1], b2 = c[2];
            double a0 = c[3], a1 = c[4], a2 = c[5];
            double out;
            if (a0 != 1.0) {
                b0 /= a0; b1 /= a0; b2 /= a0;
                a1 /= a0; a2 /= a0;
            }
            out = b0 * v + zi[2 * s];
            zi[2 * s] = b1 * v - a1 * out + zi[2 * s + 1];
            zi[2 * s + 1] = b2 * v - a2 * out;
            v = out;
        }
        y[i] = v;
    }
}

int aura_sosfilt_fwd(const double *sos, int ns, const double *x, int n,
                     double *y) {
    double *zi;
    if (!sos || !x || !y || ns <= 0 || n <= 0) return -1;
    zi = (double *)calloc((size_t)ns * 2, sizeof(double));
    if (!zi) return -1;
    sosfilt(sos, ns, x, n, y, zi);
    free(zi);
    return 0;
}

int aura_sosfilt_state(const double *sos, int ns, const double *x, int n,
                       double *y, double *zi) {
    if (!sos || !x || !y || !zi || ns <= 0 || n <= 0) return -1;
    sosfilt(sos, ns, x, n, y, zi);
    return 0;
}

int aura_sosfilt_zi_fwd(const double *sos, int ns, const double *x, int n,
                        double *y) {
    double *zi;
    int s;
    double scale = 1.0;
    if (!sos || !x || !y || ns <= 0 || n <= 0) return -1;
    zi = (double *)malloc((size_t)ns * 2 * sizeof(double));
    if (!zi) return -1;
    for (s = 0; s < ns; ++s) {
        const double *c = sos + 6 * s;
        double z1, z2;
        sos_section_zi(c[0], c[1], c[2], c[3], c[4], c[5], &z1, &z2);
        zi[2 * s] = scale * z1 * x[0];
        zi[2 * s + 1] = scale * z2 * x[0];
        scale *= (c[0] + c[1] + c[2]) / (c[3] + c[4] + c[5]);
    }
    sosfilt(sos, ns, x, n, y, zi);
    free(zi);
    return 0;
}

void aura_unwrap_phase(const double *p, int n, double *out) {
    int i;
    if (!p || !out || n <= 0) return;
    out[0] = p[0];
    for (i = 1; i < n; ++i) {
        double d = p[i] - p[i - 1];
        double ddmod = fmod(d + M_PI, 2.0 * M_PI);
        double corr;
        if (ddmod < 0.0) ddmod += 2.0 * M_PI;
        ddmod -= M_PI;
        if (fabs(ddmod + M_PI) < 1e-15 && d > 0.0) ddmod = M_PI;
        corr = (fabs(d) >= M_PI) ? (ddmod - d) : 0.0;
        out[i] = out[i - 1] + d + corr;
    }
}

void aura_odd_ext(const double *x, int n, int edge, double *out) {
    int i;
    for (i = 0; i < edge; ++i) {
        out[i] = 2.0 * x[0] - x[edge - i];
        out[n + edge + i] = 2.0 * x[n - 1] - x[n - 2 - i];
    }
    memcpy(out + edge, x, (size_t)n * sizeof(double));
}

int aura_sosfiltfilt_full(const double *sos, int ns, const double *x, int n,
                          double *y_full, int padlen, int *edge_out) {
    int i, s, edge, ntaps, nz_b, nz_a, ext_n;
    double *zi = NULL, *zi0 = NULL, *ext = NULL, *tmp = NULL;
    if (!sos || !x || !y_full || ns <= 0 || n <= 0) return -1;
    ntaps = 2 * ns + 1;
    nz_b = nz_a = 0;
    for (s = 0; s < ns; ++s) {
        if (sos[6 * s + 2] == 0.0) nz_b++;
        if (sos[6 * s + 5] == 0.0) nz_a++;
    }
    ntaps -= (nz_b < nz_a ? nz_b : nz_a);
    edge = (padlen < 0) ? 3 * ntaps : padlen;
    if (edge > 0 && n <= edge) return -1;
    if (edge < 0) edge = 0;
    if (edge_out) *edge_out = edge;
    ext_n = n + 2 * edge;
    ext = (double *)malloc((size_t)ext_n * sizeof(double));
    tmp = (double *)malloc((size_t)ext_n * sizeof(double));
    zi = (double *)malloc((size_t)ns * 2 * sizeof(double));
    zi0 = (double *)malloc((size_t)ns * 2 * sizeof(double));
    if (!ext || !tmp || !zi || !zi0) {
        free(ext); free(tmp); free(zi); free(zi0);
        return -1;
    }
    for (i = 0; i < edge; ++i) {
        ext[i] = 2.0 * x[0] - x[edge - i];
        ext[n + edge + i] = 2.0 * x[n - 1] - x[n - 2 - i];
    }
    memcpy(ext + edge, x, (size_t)n * sizeof(double));

    {
        double scale = 1.0;
        for (s = 0; s < ns; ++s) {
            const double *c = sos + 6 * s;
            double z1, z2;
            sos_section_zi(c[0], c[1], c[2], c[3], c[4], c[5], &z1, &z2);
            zi0[2 * s] = scale * z1;
            zi0[2 * s + 1] = scale * z2;
            scale *= (c[0] + c[1] + c[2]) / (c[3] + c[4] + c[5]);
        }
    }
    {
        double x0 = ext[0];
        for (s = 0; s < 2 * ns; ++s) zi[s] = zi0[s] * x0;
    }
    sosfilt(sos, ns, ext, ext_n, tmp, zi);
    for (i = 0; i < ext_n; ++i) ext[i] = tmp[ext_n - 1 - i];
    {
        double y0 = ext[0];
        for (s = 0; s < 2 * ns; ++s) zi[s] = zi0[s] * y0;
    }
    sosfilt(sos, ns, ext, ext_n, tmp, zi);
    for (i = 0; i < ext_n; ++i) y_full[i] = tmp[ext_n - 1 - i];
    free(ext); free(tmp); free(zi); free(zi0);
    return 0;
}

int aura_sosfiltfilt(const double *sos, int ns, const double *x, int n,
                     double *y, int padlen) {
    int edge = 0, i, ext_n;
    double *full;
    if (!sos || !x || !y || ns <= 0 || n <= 0) return -1;
    /* 先估 edge 以分配完整输出 */
    {
        int ntaps = 2 * ns + 1, nz_b = 0, nz_a = 0, s;
        for (s = 0; s < ns; ++s) {
            if (sos[6 * s + 2] == 0.0) nz_b++;
            if (sos[6 * s + 5] == 0.0) nz_a++;
        }
        ntaps -= (nz_b < nz_a ? nz_b : nz_a);
        edge = (padlen < 0) ? 3 * ntaps : padlen;
        if (edge < 0) edge = 0;
        if (edge > 0 && n <= edge) return -1;
    }
    ext_n = n + 2 * edge;
    full = (double *)malloc((size_t)ext_n * sizeof(double));
    if (!full) return -1;
    if (aura_sosfiltfilt_full(sos, ns, x, n, full, padlen, &edge) != 0) {
        free(full);
        return -1;
    }
    for (i = 0; i < n; ++i) y[i] = full[edge + i];
    free(full);
    return 0;
}

/* Welch 功率谱 */
int aura_welch(const double *x, int n, double fs, int nperseg,
               double *f, double *p) {
    int noverlap, step, nseg, nf, k, seg, half_even;
    double *win = NULL, *seg_buf = NULL, *acc = NULL, wsum = 0.0;
    aura_complex *fft_out = NULL;
    double scale;
    if (!x || !f || !p || n <= 0 || fs <= 0.0) return -1;
    if (nperseg < 1) return -1;
    if (nperseg > n) nperseg = n;
    noverlap = nperseg / 2;
    step = nperseg - noverlap;
    nseg = 1 + (n - nperseg) / step;
    nf = nperseg / 2 + 1;
    win = (double *)malloc((size_t)nperseg * sizeof(double));
    seg_buf = (double *)malloc((size_t)nperseg * sizeof(double));
    acc = (double *)calloc((size_t)nf, sizeof(double));
    fft_out = (aura_complex *)malloc((size_t)nperseg * sizeof(aura_complex));
    if (!win || !seg_buf || !acc || !fft_out) {
        free(win); free(seg_buf); free(acc); free(fft_out);
        return -1;
    }
    for (k = 0; k < nperseg; ++k) {
        win[k] = 0.5 - 0.5 * cos(2.0 * M_PI * (double)k / (double)nperseg);
        wsum += win[k] * win[k];
    }
    scale = 1.0 / (fs * wsum);
    for (seg = 0; seg < nseg; ++seg) {
        int start = seg * step;
        double mean = 0.0;
        if (nperseg < 2) {
            mean = 0.0;
        } else {
            for (k = 0; k < nperseg; ++k) mean += x[start + k];
            mean /= (double)nperseg;
        }
        for (k = 0; k < nperseg; ++k) {
            seg_buf[k] = (x[start + k] - mean) * win[k];
        }
        if (aura_fft(seg_buf, NULL, nperseg, fft_out) != 0) {
            free(win); free(seg_buf); free(acc); free(fft_out);
            return -1;
        }
        for (k = 0; k < nf; ++k) {
            acc[k] += (fft_out[k].re * fft_out[k].re +
                       fft_out[k].im * fft_out[k].im) * scale;
        }
    }
    for (k = 0; k < nf; ++k) acc[k] /= (double)nseg;
    half_even = (nperseg % 2 == 0);
    for (k = 1; k < nf; ++k) {
        if (half_even && k == nf - 1) continue;  /* 奈奎斯特点 */
        acc[k] *= 2.0;
    }
    for (k = 0; k < nf; ++k) {
        f[k] = fs * (double)k / (double)nperseg;
        p[k] = acc[k];
    }
    free(win); free(seg_buf); free(acc); free(fft_out);
    return nf;
}

/* 峰值检测 */
typedef struct {
    int idx;
    double h;
} peak_cand;

static int cand_cmp(const void *pa, const void *pb) {
    const peak_cand *a = (const peak_cand *)pa;
    const peak_cand *b = (const peak_cand *)pb;
    if (a->h > b->h) return -1;
    if (a->h < b->h) return 1;
    return (a->idx < b->idx) ? -1 : (a->idx > b->idx ? 1 : 0);
}

static double peak_prominence_w(const double *x, int n, int p, int half) {
    double lmin = x[p], rmin = x[p];
    int i, lo = 0, hi = n - 1;
    if (half > 0) {
        lo = p - half;
        hi = p + half;
        if (lo < 0) lo = 0;
        if (hi > n - 1) hi = n - 1;
    }
    for (i = p - 1; i >= lo; --i) {
        if (x[i] > x[p]) break;
        if (x[i] < lmin) lmin = x[i];
    }
    for (i = p + 1; i <= hi; ++i) {
        if (x[i] > x[p]) break;
        if (x[i] < rmin) rmin = x[i];
    }
    return x[p] - (lmin > rmin ? lmin : rmin);
}

static int find_peaks_impl(const double *x, int n, double distance,
                           double height, double prominence, int half,
                           int *peaks, int max_peaks,
                           double *peak_heights, double *prominences) {
    int *raw = NULL, nraw = 0, i, nkeep = 0;
    peak_cand *cand = NULL;
    int *keep = NULL;
    if (!x || !peaks || n <= 0 || max_peaks <= 0) return -1;
    raw = (int *)malloc((size_t)(n / 2 + 2) * sizeof(int));
    keep = (int *)malloc((size_t)(n / 2 + 2) * sizeof(int));
    if (!raw || !keep) {
        free(raw); free(keep);
        return -1;
    }
    i = 1;
    while (i < n - 1) {
        if (x[i - 1] < x[i] && x[i] > x[i + 1]) {
            raw[nraw++] = i;
            ++i;
        } else if (x[i - 1] < x[i] && x[i] == x[i + 1]) {
            int ia = i, ib = i + 1;
            while (ib + 1 < n && x[ib] == x[ib + 1]) ++ib;
            if (x[ia - 1] < x[ib] && ib + 1 < n && x[ib] > x[ib + 1]) {
                raw[nraw++] = (ia + ib) / 2;
            }
            i = ib + 1;
        } else {
            ++i;
        }
    }
    /* height 过滤 */
    for (i = 0; i < nraw; ++i) {
        if (height != NONE_THRESHOLD && x[raw[i]] < height) continue;
        raw[nkeep++] = raw[i];
    }
    nraw = nkeep;
    nkeep = 0;
    /* distance 过滤，高者优先 */
    if (distance > 0.0 && nraw > 0) {
        cand = (peak_cand *)malloc((size_t)nraw * sizeof(peak_cand));
        if (!cand) {
            free(raw); free(keep);
            return -1;
        }
        for (i = 0; i < nraw; ++i) {
            cand[i].idx = raw[i];
            cand[i].h = x[raw[i]];
        }
        qsort(cand, (size_t)nraw, sizeof(peak_cand), cand_cmp);
        for (i = 0; i < nraw; ++i) {
            int j, ok = 1;
            for (j = 0; j < nkeep; ++j) {
                if (fabs((double)(cand[i].idx - keep[j])) < distance) {
                    ok = 0;
                    break;
                }
            }
            if (ok && nkeep < n / 2 + 2) keep[nkeep++] = cand[i].idx;
        }
        free(cand);
    } else {
        for (i = 0; i < nraw; ++i) keep[nkeep++] = raw[i];
    }
    /* 升序 */
    for (i = 1; i < nkeep; ++i) {
        int v = keep[i], j = i - 1;
        while (j >= 0 && keep[j] > v) {
            keep[j + 1] = keep[j];
            --j;
        }
        keep[j + 1] = v;
    }
    /* prominence 过滤并输出 */
    {
        int outn = 0;
        for (i = 0; i < nkeep; ++i) {
            int p = keep[i];
            double prom = peak_prominence_w(x, n, p, half);
            if (prominence != NONE_THRESHOLD && prom < prominence) continue;
            if (outn >= max_peaks) break;
            peaks[outn] = p;
            if (peak_heights) peak_heights[outn] = x[p];
            if (prominences) prominences[outn] = prom;
            ++outn;
        }
        free(raw); free(keep);
        return outn;
    }
}

int aura_find_peaks(const double *x, int n, double distance,
                    double height, double prominence,
                    int *peaks, int max_peaks,
                    double *peak_heights, double *prominences) {
    return find_peaks_impl(x, n, distance, height, prominence, 0,
                           peaks, max_peaks, peak_heights, prominences);
}

int aura_find_peaks_w(const double *x, int n, double distance,
                      double height, double prominence, double wlen,
                      int *peaks, int max_peaks,
                      double *peak_heights, double *prominences) {
    int half = 0;
    if (wlen > 1.0) half = (int)(wlen / 2.0);
    if (half > n / 2) half = n / 2;
    return find_peaks_impl(x, n, distance, height, prominence, half,
                           peaks, max_peaks, peak_heights, prominences);
}
