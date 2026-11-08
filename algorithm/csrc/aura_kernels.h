/* 数值内核接口。 */
#ifndef AURA_KERNELS_H
#define AURA_KERNELS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double re;
    double im;
} aura_complex;

/* FFT 正变换，任意长度。 */
int aura_fft(const double *xr, const double *xi, int n, aura_complex *out);

/* IFFT 逆变换。 */
int aura_ifft(const double *xr, const double *xi, int n, aura_complex *out);

/* 线性去趋势。 */
void aura_detrend_linear(const double *x, int n, double *y);

/* 梯形积分，x 为空时按 dx=1。 */
double aura_trapezoid(const double *y, const double *x, int n);

/* 自相关，未归一化。 */
void aura_acf(const double *x, int n, double *acf);

/* 零初始状态 SOS 前向滤波。 */
int aura_sosfilt_fwd(const double *sos, int ns, const double *x, int n,
                     double *y);

/* 稳态初始条件 SOS 前向滤波。 */
int aura_sosfilt_zi_fwd(const double *sos, int ns, const double *x, int n,
                        double *y);

/* 流式 SOS 滤波，跨调用保持状态。 */
int aura_sosfilt_state(const double *sos, int ns, const double *x, int n,
                       double *y, double *zi);

/* 相位展开。 */
void aura_unwrap_phase(const double *p, int n, double *out);

/* 奇数延拓，调试用。 */
void aura_odd_ext(const double *x, int n, int edge, double *out);

/* 反向滤波完整输出，调试用。 */
int aura_sosfiltfilt_full(const double *sos, int ns, const double *x, int n,
                          double *y_full, int padlen, int *edge_out);

/* SOS 零相位滤波。 */
int aura_sosfiltfilt(const double *sos, int ns, const double *x, int n,
                     double *y, int padlen);

/* Welch 功率谱估计。 */
int aura_welch(const double *x, int n, double fs, int nperseg,
               double *f, double *p);

/* 峰值检测，支持高度、间距与突出度。 */
int aura_find_peaks(const double *x, int n, double distance,
                    double height, double prominence,
                    int *peaks, int max_peaks,
                    double *peak_heights, double *prominences);

/* 带窗口长度的峰值检测。 */
int aura_find_peaks_w(const double *x, int n, double distance,
                      double height, double prominence, double wlen,
                      int *peaks, int max_peaks,
                      double *peak_heights, double *prominences);

#ifdef __cplusplus
}
#endif

#endif /* AURA_KERNELS_H */
