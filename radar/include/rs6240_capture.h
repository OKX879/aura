#ifndef RS6240_CAPTURE_H
#define RS6240_CAPTURE_H

#define RS6240_CAPTURE_SUPPORTS_SIMPLEX 1

#include "rs6240_cube.h"
#include "rs6240_hif.h"
#include "rs6240_roi.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*rs6240_io_write_fn)(void *context, const uint8_t *data, size_t size);
typedef int (*rs6240_io_read_fn)(void *context, uint8_t *data, size_t capacity, uint32_t timeout_ms);
typedef void (*rs6240_io_delay_fn)(void *context, uint32_t delay_ms);
typedef void (*rs6240_io_delay_us_fn)(void *context, uint32_t delay_us);
// 阻塞等待数据就绪：1 就绪，0 超时，负数不支持
typedef int (*rs6240_io_wait_ready_fn)(void *context, uint32_t timeout_us);

typedef struct rs6240_io {
    void *context;
    rs6240_io_write_fn write;
    rs6240_io_read_fn read;
    rs6240_io_delay_fn delay;
    rs6240_io_delay_us_fn delay_us;
    // 可选就绪门控，设置后按设备就绪信号读取
    rs6240_io_wait_ready_fn wait_ready;
} rs6240_io_t;

typedef enum rs6240_capture_status {
    RS6240_CAPTURE_OK = 0,
    RS6240_CAPTURE_INVALID_ARGUMENT = -1,
    RS6240_CAPTURE_IO_ERROR = -2,
    RS6240_CAPTURE_WAKE_TIMEOUT = -3,
    RS6240_CAPTURE_SYNC_TIMEOUT = -4,
    RS6240_CAPTURE_PROTOCOL_ERROR = -5,
    RS6240_CAPTURE_CUBE_TIMEOUT = -6
} rs6240_capture_status_t;

typedef struct rs6240_capture {
    rs6240_io_t io;
    rs6240_hif_stream_t stream;
    rs6240_cube_reassembler_t cube;
    rs6240_roi_frame_t roi;
    uint8_t sequence;
    uint32_t wake_attempts;
    uint32_t sync_attempts;
    uint32_t message_counts[256];
    uint8_t last_message_type;
    uint8_t last_message_id;
    uint8_t last_message_flags;
    uint16_t last_message_length;
    uint32_t last_poll_reads;
    uint64_t total_poll_reads;
    uint32_t poll_command_delay_ms;
    // 非零时覆盖毫秒延时，单位为微秒
    uint32_t poll_command_delay_us;
    uint8_t simplex_mode;
} rs6240_capture_t;

int rs6240_capture_init(
    rs6240_capture_t *capture,
    const rs6240_io_t *io,
    uint32_t max_cube_points);

void rs6240_capture_destroy(rs6240_capture_t *capture);

// 设置 HOST_READ 与 SPI 读取之间的延时
void rs6240_capture_set_poll_command_delay(
    rs6240_capture_t *capture,
    uint32_t delay_ms);

// 设置微秒级轮询延时，优先于毫秒设置
void rs6240_capture_set_poll_command_delay_us(
    rs6240_capture_t *capture,
    uint32_t delay_us);

// 开启精简模式：设备主动推送报告
void rs6240_capture_set_simplex(
    rs6240_capture_t *capture,
    int enabled);

// 唤醒设备，Cube 采集只需唤醒
rs6240_capture_status_t rs6240_capture_wake(
    rs6240_capture_t *capture,
    uint32_t retries,
    uint32_t delay_ms);

// 唤醒后同步，仅在唤醒成功时使用
rs6240_capture_status_t rs6240_capture_wake_and_sync(
    rs6240_capture_t *capture,
    uint32_t retries,
    uint32_t delay_ms);

// 轮询 HOST_READ，直到拼出完整 Cube
rs6240_capture_status_t rs6240_capture_poll_cube(
    rs6240_capture_t *capture,
    uint32_t read_timeout_ms,
    uint32_t max_reads);

// 取出已缓存的 ROI 帧：1 成功，0 缺数据
int rs6240_capture_take_buffered_roi(rs6240_capture_t *capture);

// 轮询一帧 ROI 数据
rs6240_capture_status_t rs6240_capture_poll_roi(
    rs6240_capture_t *capture,
    uint32_t read_timeout_ms,
    uint32_t max_reads);

#ifdef __cplusplus
}
#endif

#endif
