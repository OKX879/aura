#include "rs6240_capture.h"

#include <string.h>

// 单次 Cube 读取的 SPI 块大小
#define RS6240_CUBE_READ_BLOCK_SIZE 8448u
#ifndef RS6240_ROI_READ_BLOCK_SIZE
#define RS6240_ROI_READ_BLOCK_SIZE 640u
#endif

// 先取已缓存的 ROI 报告，再发起轮询
int rs6240_capture_take_buffered_roi(rs6240_capture_t *capture)
{
    if (!capture) {
        return -1;
    }
    for (;;) {
        rs6240_hif_message_t message;
        rs6240_hif_status_t status = rs6240_hif_stream_next(&capture->stream, &message);
        if (status == RS6240_HIF_NEED_MORE) {
            return 0;
        }
        if (status != RS6240_HIF_OK) {
            return -1;
        }
        capture->message_counts[message.msg_id]++;
        capture->last_message_type = message.type;
        capture->last_message_id = message.msg_id;
        capture->last_message_flags = message.flags;
        capture->last_message_length = message.payload_length;
        if (message.type == 2u && message.msg_id == RS6240_HIF_MSG_ROI_DATA) {
            return rs6240_roi_decode(message.payload, message.payload_length,
                                     &capture->roi) == RS6240_ROI_OK ? 1 : -1;
        }
    }
}

static void delay_if_configured(rs6240_capture_t *capture, uint32_t delay_ms)
{
    if (capture->io.delay && delay_ms > 0u) {
        capture->io.delay(capture->io.context, delay_ms);
    }
}

// 轮询前延时，微秒设置优先
static void delay_before_poll(rs6240_capture_t *capture)
{
    if (capture->poll_command_delay_us > 0u) {
        if (capture->io.delay_us) {
            capture->io.delay_us(capture->io.context,
                                 capture->poll_command_delay_us);
        } else {
            delay_if_configured(capture,
                                (capture->poll_command_delay_us + 999u) / 1000u);
        }
        return;
    }
    delay_if_configured(capture, capture->poll_command_delay_ms);
}

// 轮询后等待就绪，无就绪线时退回固定延时
static void wait_after_poll(rs6240_capture_t *capture)
{
    const uint32_t budget_us =
        capture->poll_command_delay_us > 0u ? capture->poll_command_delay_us : 1000u;
    if (capture->io.wait_ready != NULL) {
        const int ready = capture->io.wait_ready(capture->io.context, budget_us);
        if (ready > 0) {
            return;   /* 已就绪，立即读 */
        }
        if (ready == 0) {
            return;   /* 超时也直接读，避免叠加延时 */
        }
        /* 无就绪线：退回固定延时 */
    }
    delay_before_poll(capture);
}

int rs6240_capture_init(
    rs6240_capture_t *capture,
    const rs6240_io_t *io,
    uint32_t max_cube_points)
{
    if (!capture || !io || !io->write || !io->read) {
        return 0;
    }
    memset(capture, 0, sizeof(*capture));
    capture->io = *io;
    capture->poll_command_delay_ms = 5u;
    rs6240_hif_stream_init(&capture->stream, RS6240_HIF_TRANSPORT_SPI);
    if (!rs6240_cube_reassembler_init(&capture->cube, max_cube_points)) {
        memset(capture, 0, sizeof(*capture));
        return 0;
    }
    return 1;
}

void rs6240_capture_set_poll_command_delay(
    rs6240_capture_t *capture,
    uint32_t delay_ms)
{
    if (capture) {
        capture->poll_command_delay_ms = delay_ms;
    }
}

void rs6240_capture_set_poll_command_delay_us(
    rs6240_capture_t *capture,
    uint32_t delay_us)
{
    if (capture) {
        capture->poll_command_delay_us = delay_us;
    }
}

void rs6240_capture_set_simplex(rs6240_capture_t *capture, int enabled)
{
    if (capture) {
        capture->simplex_mode = enabled ? 1u : 0u;
    }
}

void rs6240_capture_destroy(rs6240_capture_t *capture)
{
    if (!capture) {
        return;
    }
    rs6240_cube_reassembler_destroy(&capture->cube);
    memset(capture, 0, sizeof(*capture));
}

rs6240_capture_status_t rs6240_capture_wake(
    rs6240_capture_t *capture,
    uint32_t retries,
    uint32_t delay_ms)
{
    static const uint8_t wake[2] = {0x55, 0xFF};
    uint8_t reply[2];
    uint32_t i;

    if (!capture || !capture->io.write || !capture->io.read || retries == 0u) {
        return RS6240_CAPTURE_INVALID_ARGUMENT;
    }

    for (i = 0; i < retries; ++i) {
        int n;
        capture->wake_attempts++;
        if (capture->io.write(capture->io.context, wake, sizeof(wake)) != (int)sizeof(wake)) {
            delay_if_configured(capture, delay_ms);
            continue;
        }
        delay_if_configured(capture, delay_ms);
        n = capture->io.read(capture->io.context, reply, sizeof(reply), 100u);
        if (n > 0 && (reply[0] == 0x79u || (n > 1 && reply[1] == 0x79u))) {
            return RS6240_CAPTURE_OK;
        }
        delay_if_configured(capture, delay_ms);
    }
    return RS6240_CAPTURE_WAKE_TIMEOUT;
}

rs6240_capture_status_t rs6240_capture_wake_and_sync(
    rs6240_capture_t *capture,
    uint32_t retries,
    uint32_t delay_ms)
{
    static const uint8_t sync = 0x7E;
    uint8_t reply[1];
    uint32_t i;
    rs6240_capture_status_t wake_status = rs6240_capture_wake(capture, retries, delay_ms);

    if (wake_status != RS6240_CAPTURE_OK) {
        return wake_status;
    }

    // 仅在唤醒成功后才进入同步
    for (i = 0; i < retries; ++i) {
        int n;
        capture->sync_attempts++;
        if (capture->io.write(capture->io.context, &sync, 1u) != 1) {
            delay_if_configured(capture, delay_ms);
            continue;
        }
        delay_if_configured(capture, delay_ms);
        n = capture->io.read(capture->io.context, reply, 1u, 100u);
        if (n == 1 && reply[0] == 0x59u) {
            return RS6240_CAPTURE_OK;
        }
        delay_if_configured(capture, delay_ms);
    }
    return RS6240_CAPTURE_SYNC_TIMEOUT;
}

rs6240_capture_status_t rs6240_capture_poll_cube(
    rs6240_capture_t *capture,
    uint32_t read_timeout_ms,
    uint32_t max_reads)
{
    static const uint8_t poll_payload[4] = {2, 0, 0, 1};
    uint8_t command[32];
    uint8_t block[RS6240_CUBE_READ_BLOCK_SIZE];
    size_t command_size = 0;
    uint32_t poll_count;

    if (!capture || max_reads == 0u) {
        return RS6240_CAPTURE_INVALID_ARGUMENT;
    }
    capture->last_poll_reads = 0u;
    // 一次 HOST_READ 后连续读完整个 Cube
    for (poll_count = 0; poll_count < max_reads; ++poll_count) {
        int n;
        capture->last_poll_reads++;
        capture->total_poll_reads++;
        if (!capture->simplex_mode) {
            if (rs6240_hif_encode_command_ex(0x0Cu, capture->sequence++,
                                              poll_payload, sizeof(poll_payload),
                                              RS6240_HIF_TRANSPORT_UART, 0,
                                              command, sizeof(command), &command_size) != RS6240_HIF_OK) {
                return RS6240_CAPTURE_PROTOCOL_ERROR;
            }
            if (capture->io.write(capture->io.context, command, command_size) != (int)command_size) {
                return RS6240_CAPTURE_IO_ERROR;
            }
            wait_after_poll(capture);
        }
        n = capture->io.read(capture->io.context, block, sizeof(block), read_timeout_ms);
        if (n < 0) {
            return RS6240_CAPTURE_IO_ERROR;
        }
        if (n > 0 && rs6240_hif_stream_feed(&capture->stream, block, (size_t)n) != RS6240_HIF_OK) {
            return RS6240_CAPTURE_PROTOCOL_ERROR;
        }
        for (;;) {
            rs6240_hif_message_t message;
            rs6240_hif_status_t status = rs6240_hif_stream_next(&capture->stream, &message);
            if (status == RS6240_HIF_NEED_MORE) {
                break;
            }
            if (status != RS6240_HIF_OK) {
                return RS6240_CAPTURE_PROTOCOL_ERROR;
            }
            capture->message_counts[message.msg_id]++;
            capture->last_message_type = message.type;
            capture->last_message_id = message.msg_id;
            capture->last_message_flags = message.flags;
            capture->last_message_length = message.payload_length;
            if (message.type == 2u && message.msg_id == RS6240_HIF_MSG_FFT_DATA) {
                rs6240_cube_status_t cube_status =
                    rs6240_cube_feed(&capture->cube, message.payload, message.payload_length);
                if (cube_status == RS6240_CUBE_COMPLETE) {
                    return RS6240_CAPTURE_OK;
                }
                if (cube_status < 0) {
                    return RS6240_CAPTURE_PROTOCOL_ERROR;
                }
            }
        }
        delay_if_configured(capture, 5u);
    }
    return RS6240_CAPTURE_CUBE_TIMEOUT;
}

rs6240_capture_status_t rs6240_capture_poll_roi(
    rs6240_capture_t *capture,
    uint32_t read_timeout_ms,
    uint32_t max_reads)
{
    static const uint8_t poll_payload[4] = {2, 0, 0, 1};
    uint8_t command[32];
    uint8_t block[RS6240_ROI_READ_BLOCK_SIZE];
    size_t command_size = 0;
    uint32_t poll_count;

    if (!capture || max_reads == 0u) {
        return RS6240_CAPTURE_INVALID_ARGUMENT;
    }
    capture->last_poll_reads = 0u;
    {
        int buffered = rs6240_capture_take_buffered_roi(capture);
        if (buffered > 0) {
            return RS6240_CAPTURE_OK;
        }
        if (buffered < 0) {
            return RS6240_CAPTURE_PROTOCOL_ERROR;
        }
    }
    for (poll_count = 0; poll_count < max_reads; ++poll_count) {
        int n;
        capture->last_poll_reads++;
        capture->total_poll_reads++;
        if (!capture->simplex_mode) {
            if (rs6240_hif_encode_command_ex(0x0Cu, capture->sequence++,
                                              poll_payload, sizeof(poll_payload),
                                              RS6240_HIF_TRANSPORT_UART, 0,
                                              command, sizeof(command), &command_size) != RS6240_HIF_OK) {
                return RS6240_CAPTURE_PROTOCOL_ERROR;
            }
            if (capture->io.write(capture->io.context, command, command_size) != (int)command_size) {
                return RS6240_CAPTURE_IO_ERROR;
            }
            wait_after_poll(capture);
        }
        n = capture->io.read(capture->io.context, block, sizeof(block), read_timeout_ms);
        if (n < 0) {
            return RS6240_CAPTURE_IO_ERROR;
        }
        if (n > 0 && rs6240_hif_stream_feed(&capture->stream, block, (size_t)n) != RS6240_HIF_OK) {
            return RS6240_CAPTURE_PROTOCOL_ERROR;
        }
        {
            int buffered = rs6240_capture_take_buffered_roi(capture);
            if (buffered > 0) {
                return RS6240_CAPTURE_OK;
            }
            if (buffered < 0) {
                return RS6240_CAPTURE_PROTOCOL_ERROR;
            }
        }
    }
    return RS6240_CAPTURE_CUBE_TIMEOUT;
}
