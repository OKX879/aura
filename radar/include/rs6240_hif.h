#ifndef RS6240_HIF_H
#define RS6240_HIF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS6240_HIF_PHY_MAGIC       0xA5u
#define RS6240_HIF_MSG_FFT_DATA    0xC1u
#define RS6240_HIF_MSG_ADC_DATA    0xC0u
#define RS6240_HIF_MSG_ROI_DATA    0xC4u

#define RS6240_HIF_FLAG_CHECK      (1u << 2)
#define RS6240_HIF_FLAG_EXTEND     (1u << 4)

typedef enum rs6240_hif_status {
    RS6240_HIF_OK = 0,
    RS6240_HIF_NEED_MORE = 1,
    RS6240_HIF_INVALID_ARGUMENT = -1,
    RS6240_HIF_BAD_MAGIC = -2,
    RS6240_HIF_BAD_HEADER_CHECKSUM = -3,
    RS6240_HIF_BAD_LENGTH = -4,
    RS6240_HIF_BAD_DATA_CHECKSUM = -5,
    RS6240_HIF_UNSUPPORTED = -6
} rs6240_hif_status_t;

typedef struct rs6240_hif_message {
    uint8_t type;
    uint8_t flags;
    uint8_t msg_id;
    uint16_t payload_length;
    uint8_t sequence;
    uint8_t fragment;
    const uint8_t *payload;
} rs6240_hif_message_t;

#define RS6240_HIF_STREAM_CAPACITY 16384u
#define RS6240_HIF_FRAME_CAPACITY  8192u

typedef enum rs6240_hif_transport {
    RS6240_HIF_TRANSPORT_UART = 0,
    RS6240_HIF_TRANSPORT_SPI = 1
} rs6240_hif_transport_t;

typedef struct rs6240_hif_stream_stats {
    uint64_t bytes_discarded;
    uint64_t bad_headers;
    uint64_t bad_checksums;
    uint64_t frames_decoded;
} rs6240_hif_stream_stats_t;

typedef struct rs6240_hif_stream {
    uint8_t input[RS6240_HIF_STREAM_CAPACITY];
    size_t input_size;
    uint8_t frame[RS6240_HIF_FRAME_CAPACITY];
    size_t frame_size;
    rs6240_hif_transport_t transport;
    rs6240_hif_stream_stats_t stats;
} rs6240_hif_stream_t;

// 解析标准 HIF 帧：头、载荷与可选校验
rs6240_hif_status_t rs6240_hif_decode(
    const uint8_t *frame,
    size_t frame_size,
    rs6240_hif_message_t *message,
    size_t *consumed);

// 解析 SPI 帧，头部与校验后各有一个填充字节
rs6240_hif_status_t rs6240_hif_decode_spi(
    const uint8_t *frame,
    size_t frame_size,
    rs6240_hif_message_t *message,
    size_t *consumed);

uint32_t rs6240_hif_checksum32(const uint8_t *data, size_t size);

// 组包下发命令，序号取低 3 位
rs6240_hif_status_t rs6240_hif_encode_command(
    uint8_t msg_id,
    uint8_t sequence,
    const uint8_t *payload,
    uint16_t payload_size,
    rs6240_hif_transport_t transport,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size);

// 扩展组包，可指定是否请求应答
rs6240_hif_status_t rs6240_hif_encode_command_ex(
    uint8_t msg_id,
    uint8_t sequence,
    const uint8_t *payload,
    uint16_t payload_size,
    rs6240_hif_transport_t transport,
    int request_ack,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size);

void rs6240_hif_stream_init(
    rs6240_hif_stream_t *stream,
    rs6240_hif_transport_t transport);

// 追加接收数据，容量不足时返回长度错误
rs6240_hif_status_t rs6240_hif_stream_feed(
    rs6240_hif_stream_t *stream,
    const uint8_t *data,
    size_t size);

// 取出一个完整帧，自动跳过噪声与坏帧
rs6240_hif_status_t rs6240_hif_stream_next(
    rs6240_hif_stream_t *stream,
    rs6240_hif_message_t *message);

#ifdef __cplusplus
}
#endif

#endif
