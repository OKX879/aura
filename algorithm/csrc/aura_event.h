/* 块式事件检测器接口。 */
#ifndef AURA_EVENT_H
#define AURA_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double *times_s;              /* 事件绝对时间（n_events） */
    double *amps;                 /* 事件幅度（n_events） */
    double *ibis_ms;              /* 生理窗内 IBI（n_ibis） */
    double *ibi_times_s;          /* 每个 IBI 的结束事件时间（n_ibis） */
    int *segment_ids;             /* 每个 IBI 所属片段（n_ibis） */
    double *segment_start_times_s;/* 各片段首个 IBI 时间 */
    int n_events;
    int n_ibis;
    int n_raw;                    /* 去重前原始检测数 */
    int n_segments;               /* 片段数（无 IBI 时为 0） */
    int n_segment_starts;
} aura_event_result;

/* 心搏信号输入，结果需释放。 */
int aura_event_detect(const double *signal, int n, double fs,
                      double start_time_s, aura_event_result *out);
void aura_event_free(aura_event_result *r);

#ifdef __cplusplus
}
#endif

#endif /* AURA_EVENT_H */
