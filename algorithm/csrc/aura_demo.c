/* 读取复数 cube 并输出 JSON 快照。 */
#include "aura_pipeline.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* 输出 JSON 数字，非有限值为 null。 */
static void jnum(double v, int prec) {
    if (!isfinite(v)) printf("null");
    else printf("%.*f", prec, v);
}

int main(int argc, char **argv) {
    FILE *f;
    int C, R, fs = 100, win = 1024, step = 100;
    int frame = 0, last_snap = -1;
    float *buf;
    AuraPipeline *p;
    AuraSnapshot snap;
    if (argc < 4) {
        fprintf(stderr, "usage: %s cube.bin C R [fs] [window] [step]\n", argv[0]);
        return 2;
    }
    C = atoi(argv[2]);
    R = atoi(argv[3]);
    if (argc > 4) fs = atoi(argv[4]);
    if (argc > 5) win = atoi(argv[5]);
    if (argc > 6) step = atoi(argv[6]);
    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    buf = (float *)malloc((size_t)C * R * 2 * sizeof(float));
    p = aura_pipeline_create(C, R, (double)fs, win, step);
    if (!buf || !p) {
        fprintf(stderr, "alloc failed\n");
        fclose(f);
        return 2;
    }
    while (fread(buf, sizeof(float), (size_t)C * R * 2, f) ==
           (size_t)C * R * 2) {
        int i;
        float *re = (float *)malloc((size_t)C * R * sizeof(float));
        float *im = (float *)malloc((size_t)C * R * sizeof(float));
        if (!re || !im) break;
        for (i = 0; i < C * R; ++i) {
            re[i] = buf[2 * i];
            im[i] = buf[2 * i + 1];
        }
        aura_pipeline_push(p, re, im);
        free(re); free(im);
        frame++;
        if (aura_pipeline_snapshot(p, &snap) == 0 && snap.frame_id > 0
                && snap.frame_id != last_snap) {
            last_snap = snap.frame_id;
            printf("{\"frame\":%d,\"hr\":", snap.frame_id); jnum(snap.hr_bpm, 3);
            printf(",\"hr_fft\":"); jnum(snap.hr_fft, 3);
            printf(",\"hr_acf\":"); jnum(snap.hr_acf, 3);
            printf(",\"hr_ppi\":"); jnum(snap.hr_ppi, 3);
            printf(",\"hr_fused\":"); jnum(snap.hr_fused, 3);
            printf(",\"conf\":"); jnum(snap.hr_conf, 3);
            printf(",\"max_diff\":"); jnum(snap.hr_max_diff_bpm, 3);
            printf(",\"consistent\":%d,\"resp_hz\":", snap.hr_consistent);
            jnum(snap.resp_hz, 4);
            printf(",\"ppi_src\":%d,\"n_beats\":%d,\"confident\":%d,"
                   "\"rescued\":%d,\"bin\":%d,\"presence\":%d,\"motion\":%d,\"amp\":",
                   snap.ppi_source, snap.n_beats, snap.hr_confident,
                   snap.hr_rescued, snap.selected_bin, snap.presence,
                   snap.motion);
            jnum(snap.fused_amp, 4);
            printf(",\"coh\":"); jnum(snap.mean_coherence, 4);
            printf(",\"motion_level\":"); jnum(snap.motion_level_rad, 4);
            printf("}\n");
        }
    }
    {
        aura_hrv_result hv;
        if (aura_pipeline_get_hrv(p, 0, &hv) == 0) {
            printf("{\"hrv\":\"%s\",\"rmssd_ms\":",
                   hv.sufficient_data ? "ok" : "insufficient_data");
            jnum(hv.rmssd_ms, 2);
            printf(",\"sdrr_ms\":"); jnum(hv.sdrr_ms, 2);
            printf(",\"pnn50\":"); jnum(hv.pnn50_percent, 2);
            printf(",\"n_ibis\":%d,\"coverage\":", hv.n_ibis_total);
            jnum(hv.coverage_ratio, 2);
            printf(",\"hr_display\":"); jnum(hv.hr_display_bpm, 2);
            printf("}\n");
        }
    }
    free(buf);
    aura_pipeline_destroy(p);
    fclose(f);
    return 0;
}
