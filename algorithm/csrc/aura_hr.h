/* 心率估计与融合接口。 */
#ifndef AURA_HR_H
#define AURA_HR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double f_resp;      /* 呼吸基频，不可用为 NAN */
    double hr_fft;      /* FFT 路心率 */
    double hr_acf;      /* ACF 路心率 */
    double hr_ppi;      /* PPI 路心率 */
    double hr_fused;    /* 融合输出心率 */
    double snr_fft;     /* FFT 峰值信噪比 */
    double acf_height;  /* ACF 归一化峰高 */
    double max_diff_bpm;/* 三路最大差 */
    int consistent;     /* 三路是否一致 */
    int n_ppi_peaks;    /* PPI 路峰数 */
    int ppi_source;     /* PPI 来源编号 */
    double w_fft, w_acf, w_ppi;
} aura_hr_result;

/* 呼吸基频估计，失败返回 NAN。 */
double aura_estimate_resp(const double *sig, int n, double fs,
                          const double *sos_resp, int ns_resp);

/* 宽带呼吸基频估计，失败返回 NAN。 */
double aura_estimate_resp_api(const double *sig, int n, double fs);

/* RHC 陷波，频率无效时原样复制。 */
void aura_rhc_notch(const double *sig, int n, double fs, double f_resp,
                    double *out);

/* FFT/HPS 心率估计，失败返回 NAN。 */
double aura_estimate_fft(const double *sig, int n, double fs,
                         double f_resp, const double *sos_resp, int ns_resp,
                         double *snr);

/* ACF 心率估计，失败返回 NAN。 */
double aura_estimate_acf(const double *sig, int n, double fs, double *height);

/* rfft/irfft 往返调试。 */
int aura_rfft_roundtrip(const double *x, int n, double *out);
int aura_estimate_hr(const double *sig, int n, double fs, double f_resp_in,
                     int enable_acf,
                     const double *sos_resp, int ns_resp,
                     double *out_f_resp, double *out_fft,
                     double *out_acf, double *out_snr_fft);

/* 单路 PPI 估计。 */
double aura_estimate_ppi_event(const double *sig, int n, double fs,
                               int *n_peaks);
double aura_estimate_ppi_template(const double *sig, int n, double fs,
                                  int *n_peaks);
double aura_estimate_ppi_findpeaks(const double *sig, int n, double fs,
                                   int *n_peaks);
/* 三级回退 PPI，全失败返回 NAN。 */
double aura_estimate_ppi(const double *sig, int n, double fs,
                         int *n_peaks, int *source);

/* 完整 HR 估计链路。 */
int aura_estimate_all(const double *sig, int n, double fs, double f_resp_in,
                      int enable_acf,
                      const double *sos_resp, int ns_resp,
                      aura_hr_result *out);

#ifdef __cplusplus
}
#endif

#endif /* AURA_HR_H */
