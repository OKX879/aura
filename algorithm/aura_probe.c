/* 对照探针，读取 float64 信号输出 JSON。 */
#include "aura_event.h"
#include "aura_hr.h"
#include "aura_sos_constants.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double *read_f64(const char *path, int n) {
    FILE *f = fopen(path, "rb");
    double *x;
    size_t got;
    if (!f) return NULL;
    x = (double *)malloc((size_t)n * sizeof(double));
    if (!x) { fclose(f); return NULL; }
    got = fread(x, sizeof(double), (size_t)n, f);
    fclose(f);
    if ((int)got != n) { free(x); return NULL; }
    return x;
}

static void pnum(double v) {
    if (isnan(v)) printf("NaN");
    else if (isinf(v)) printf(v > 0 ? "Infinity" : "-Infinity");
    else printf("%.17g", v);
}

int main(int argc, char **argv) {
    const char *mode;
    int n, enable = 1;
    double fs, f_resp = NAN;
    double *sig;
    if (argc < 5) return 2;
    mode = argv[1];
    n = atoi(argv[3]);
    fs = atof(argv[4]);
    if (n <= 0 || fs <= 0.0) return 2;
    if (strcmp(mode, "event") != 0) {
        if (argc < 6) return 2;
        f_resp = atof(argv[5]);
    }
    if (strcmp(mode, "hr") == 0) {
        if (argc >= 7) enable = atoi(argv[6]);
    }
    sig = read_f64(argv[2], n);
    if (!sig) { fprintf(stderr, "read failed\n"); return 3; }
    if (strcmp(mode, "event") == 0) {
        aura_event_result r;
        int i;
        if (aura_event_detect(sig, n, fs, 0.0, &r) != 0) return 4;
        printf("{\"n_events\":%d,\"n_ibis\":%d,\"n_raw\":%d,\"n_segments\":%d,",
               r.n_events, r.n_ibis, r.n_raw, r.n_segments);
        printf("\"times\":[");
        for (i = 0; i < r.n_events; ++i) { if (i) printf(","); pnum(r.times_s[i]); }
        printf("],\"amps\":[");
        for (i = 0; i < r.n_events; ++i) { if (i) printf(","); pnum(r.amps[i]); }
        printf("],\"ibis\":[");
        for (i = 0; i < r.n_ibis; ++i) { if (i) printf(","); pnum(r.ibis_ms[i]); }
        printf("],\"segment_ids\":[");
        for (i = 0; i < r.n_ibis; ++i) { if (i) printf(","); printf("%d", r.segment_ids[i]); }
        printf("]}\n");
        aura_event_free(&r);
    } else if (strcmp(mode, "hr") == 0) {
        aura_hr_result r;
        if (aura_estimate_all(sig, n, fs, f_resp, enable,
                              (const double *)AURA_SOS_RESP, AURA_SOS_RESP_NS,
                              &r) != 0) return 4;
        printf("{\"f_resp\":"); pnum(r.f_resp);
        printf(",\"fft\":"); pnum(r.hr_fft);
        printf(",\"acf\":"); pnum(r.hr_acf);
        printf(",\"ppi\":"); pnum(r.hr_ppi);
        printf(",\"fused\":"); pnum(r.hr_fused);
        printf(",\"snr\":"); pnum(r.snr_fft);
        printf(",\"acf_h\":"); pnum(r.acf_height);
        printf(",\"max_diff\":"); pnum(r.max_diff_bpm);
        printf(",\"consistent\":%d,\"n_peaks\":%d,\"source\":%d,",
               r.consistent, r.n_ppi_peaks, r.ppi_source);
        printf("\"w_fft\":"); pnum(r.w_fft);
        printf(",\"w_acf\":"); pnum(r.w_acf);
        printf(",\"w_ppi\":"); pnum(r.w_ppi);
        printf("}\n");
    } else if (strcmp(mode, "ppi") == 0) {
        int nk = 0, src = -1;
        double hr;
        const char *m = (argc >= 6) ? argv[5] : "auto";
        if (strcmp(m, "event") == 0) hr = aura_estimate_ppi_event(sig, n, fs, &nk);
        else if (strcmp(m, "template") == 0) hr = aura_estimate_ppi_template(sig, n, fs, &nk);
        else if (strcmp(m, "findpeaks") == 0) hr = aura_estimate_ppi_findpeaks(sig, n, fs, &nk);
        else hr = aura_estimate_ppi(sig, n, fs, &nk, &src);
        printf("{\"hr\":"); pnum(hr);
        printf(",\"n_peaks\":%d,\"source\":%d}\n", nk, src);
    } else {
        free(sig);
        return 2;
    }
    free(sig);
    return 0;
}
