#include "rs6240_hif.h"

#include <string.h>

#define PHY_HEADER_SIZE 2u
#define MSG_HEADER_SIZE 4u
#define BASE_HEADER_SIZE (PHY_HEADER_SIZE + MSG_HEADER_SIZE)
#define EXT_HEADER_SIZE 4u
#define CHECKSUM_SIZE 4u

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

uint32_t rs6240_hif_checksum32(const uint8_t *data, size_t size)
{
    uint32_t sum = 0;
    size_t offset = 0;

    if (!data && size != 0u) {
        return 0;
    }

    while (size - offset >= 4u) {
        sum += read_le32(data + offset);
        offset += 4u;
    }

    if (offset < size) {
        uint32_t tail = 0;
        unsigned shift = 0;
        while (offset < size) {
            tail |= (uint32_t)data[offset++] << shift;
            shift += 8u;
        }
        sum += tail;
    }
    return sum;
}

rs6240_hif_status_t rs6240_hif_encode_command_ex(
    uint8_t msg_id,
    uint8_t sequence,
    const uint8_t *payload,
    uint16_t payload_size,
    rs6240_hif_transport_t transport,
    int request_ack,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size)
{
    uint8_t flags = request_ack ? 1u : 0u; /* HIF_MSG_FLAG_REQ_BIT */
    uint8_t header_sum;
    size_t header_pad;
    size_t payload_offset;
    size_t checksum_offset;
    size_t checksum_pad;
    size_t required;
    uint32_t checksum;

    if (!output || !output_size || (!payload && payload_size != 0u)) {
        return RS6240_HIF_INVALID_ARGUMENT;
    }
    if (payload_size > 0x0FFFu) {
        return RS6240_HIF_BAD_LENGTH;
    }
    if (transport != RS6240_HIF_TRANSPORT_UART &&
        transport != RS6240_HIF_TRANSPORT_SPI) {
        return RS6240_HIF_INVALID_ARGUMENT;
    }
    if (payload_size != 0u) {
        flags |= RS6240_HIF_FLAG_CHECK;
    }
    header_pad = transport == RS6240_HIF_TRANSPORT_SPI ? 1u : 0u;
    checksum_pad = (transport == RS6240_HIF_TRANSPORT_SPI && payload_size != 0u) ? 1u : 0u;
    payload_offset = BASE_HEADER_SIZE + header_pad;
    checksum_offset = payload_offset + payload_size;
    required = checksum_offset + (payload_size != 0u ? CHECKSUM_SIZE + checksum_pad : 0u);
    if (required > output_capacity) {
        return RS6240_HIF_BAD_LENGTH;
    }

    memset(output, 0, required);
    output[0] = RS6240_HIF_PHY_MAGIC;
    output[2] = (uint8_t)(1u | (flags << 2)); /* 类型：发往设备 */
    output[3] = msg_id;
    output[4] = (uint8_t)payload_size;
    output[5] = (uint8_t)(((payload_size >> 8) & 0x0Fu) |
                          ((sequence & 0x07u) << 4));
    header_sum = (uint8_t)(output[0] + output[2] + output[3] + output[4] + output[5]);
    output[1] = (uint8_t)~header_sum;

    if (header_pad != 0u) {
        output[BASE_HEADER_SIZE] = 0xFFu;
    }
    if (payload_size != 0u) {
        memcpy(output + payload_offset, payload, payload_size);
        checksum = rs6240_hif_checksum32(output + PHY_HEADER_SIZE, MSG_HEADER_SIZE);
        checksum += rs6240_hif_checksum32(payload, payload_size);
        write_le32(output + checksum_offset, ~checksum);
        if (checksum_pad != 0u) {
            output[checksum_offset + CHECKSUM_SIZE] = 0xFFu;
        }
    }
    *output_size = required;
    return RS6240_HIF_OK;
}

rs6240_hif_status_t rs6240_hif_encode_command(
    uint8_t msg_id,
    uint8_t sequence,
    const uint8_t *payload,
    uint16_t payload_size,
    rs6240_hif_transport_t transport,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size)
{
    return rs6240_hif_encode_command_ex(
        msg_id, sequence, payload, payload_size, transport, 1,
        output, output_capacity, output_size);
}

static void decode_message_header(const uint8_t p[4], rs6240_hif_message_t *m)
{
    // 消息头位域：类型、标志、ID、长度、序号、分片
    m->type = p[0] & 0x03u;
    m->flags = (p[0] >> 2) & 0x3Fu;
    m->msg_id = p[1];
    m->payload_length = (uint16_t)p[2] | ((uint16_t)(p[3] & 0x0Fu) << 8);
    m->sequence = (p[3] >> 4) & 0x07u;
    m->fragment = (p[3] >> 7) & 0x01u;
}

static rs6240_hif_status_t decode_common(
    const uint8_t *frame,
    size_t frame_size,
    int spi,
    rs6240_hif_message_t *message,
    size_t *consumed)
{
    rs6240_hif_message_t parsed;
    size_t header_pad = spi ? 1u : 0u;
    size_t checksum_pad = 0u;
    size_t payload_offset;
    size_t checksum_offset;
    size_t required;
    uint8_t header_sum = 0;
    size_t i;

    if (!frame || !message) {
        return RS6240_HIF_INVALID_ARGUMENT;
    }
    if (frame_size < BASE_HEADER_SIZE) {
        return RS6240_HIF_NEED_MORE;
    }
    if (frame[0] != RS6240_HIF_PHY_MAGIC) {
        return RS6240_HIF_BAD_MAGIC;
    }

    for (i = 0; i < BASE_HEADER_SIZE; ++i) {
        header_sum = (uint8_t)(header_sum + frame[i]);
    }
    if (header_sum != 0xFFu) {
        return RS6240_HIF_BAD_HEADER_CHECKSUM;
    }

    memset(&parsed, 0, sizeof(parsed));
    decode_message_header(frame + PHY_HEADER_SIZE, &parsed);

    payload_offset = BASE_HEADER_SIZE + header_pad;
    if ((parsed.flags & RS6240_HIF_FLAG_EXTEND) != 0u) {
        payload_offset += EXT_HEADER_SIZE;
    }
    if ((parsed.flags & RS6240_HIF_FLAG_CHECK) != 0u && spi) {
        checksum_pad = 1u;
    }
    checksum_offset = payload_offset + parsed.payload_length;
    required = checksum_offset;
    if ((parsed.flags & RS6240_HIF_FLAG_CHECK) != 0u) {
        required += CHECKSUM_SIZE + checksum_pad;
    }
    if (required > frame_size) {
        return RS6240_HIF_NEED_MORE;
    }

    if ((parsed.flags & RS6240_HIF_FLAG_CHECK) != 0u) {
        uint32_t sum = rs6240_hif_checksum32(frame + PHY_HEADER_SIZE, MSG_HEADER_SIZE);
        if ((parsed.flags & RS6240_HIF_FLAG_EXTEND) != 0u) {
            sum += rs6240_hif_checksum32(frame + BASE_HEADER_SIZE + header_pad,
                                         EXT_HEADER_SIZE);
        }
        sum += rs6240_hif_checksum32(frame + payload_offset, parsed.payload_length);
        if ((uint32_t)(~sum) != read_le32(frame + checksum_offset)) {
            return RS6240_HIF_BAD_DATA_CHECKSUM;
        }
    }

    parsed.payload = frame + payload_offset;
    *message = parsed;
    if (consumed) {
        *consumed = required;
    }
    return RS6240_HIF_OK;
}

rs6240_hif_status_t rs6240_hif_decode(
    const uint8_t *frame,
    size_t frame_size,
    rs6240_hif_message_t *message,
    size_t *consumed)
{
    return decode_common(frame, frame_size, 0, message, consumed);
}

rs6240_hif_status_t rs6240_hif_decode_spi(
    const uint8_t *frame,
    size_t frame_size,
    rs6240_hif_message_t *message,
    size_t *consumed)
{
    return decode_common(frame, frame_size, 1, message, consumed);
}

void rs6240_hif_stream_init(
    rs6240_hif_stream_t *stream,
    rs6240_hif_transport_t transport)
{
    if (!stream) {
        return;
    }
    memset(stream, 0, sizeof(*stream));
    stream->transport = transport;
}

rs6240_hif_status_t rs6240_hif_stream_feed(
    rs6240_hif_stream_t *stream,
    const uint8_t *data,
    size_t size)
{
    if (!stream || (!data && size != 0u)) {
        return RS6240_HIF_INVALID_ARGUMENT;
    }
    if (size > RS6240_HIF_STREAM_CAPACITY - stream->input_size) {
        return RS6240_HIF_BAD_LENGTH;
    }
    if (size > 0u) {
        memcpy(stream->input + stream->input_size, data, size);
        stream->input_size += size;
    }
    return RS6240_HIF_OK;
}

static void stream_discard(rs6240_hif_stream_t *stream, size_t size)
{
    if (size > stream->input_size) {
        size = stream->input_size;
    }
    if (size < stream->input_size) {
        memmove(stream->input, stream->input + size, stream->input_size - size);
    }
    stream->input_size -= size;
    stream->stats.bytes_discarded += size;
}

static size_t expected_frame_size(const uint8_t *frame, int spi)
{
    rs6240_hif_message_t m;
    size_t size = BASE_HEADER_SIZE + (spi ? 1u : 0u);
    decode_message_header(frame + PHY_HEADER_SIZE, &m);
    if ((m.flags & RS6240_HIF_FLAG_EXTEND) != 0u) {
        size += EXT_HEADER_SIZE;
    }
    size += m.payload_length;
    if ((m.flags & RS6240_HIF_FLAG_CHECK) != 0u) {
        size += CHECKSUM_SIZE + (spi ? 1u : 0u);
    }
    return size;
}

rs6240_hif_status_t rs6240_hif_stream_next(
    rs6240_hif_stream_t *stream,
    rs6240_hif_message_t *message)
{
    int spi;

    if (!stream || !message) {
        return RS6240_HIF_INVALID_ARGUMENT;
    }
    spi = stream->transport == RS6240_HIF_TRANSPORT_SPI;

    for (;;) {
        size_t sync = 0;
        size_t needed;
        size_t consumed = 0;
        rs6240_hif_status_t status;
        uint8_t sum = 0;
        size_t i;

        if (stream->input_size < BASE_HEADER_SIZE) {
            return RS6240_HIF_NEED_MORE;
        }
        while (sync < stream->input_size && stream->input[sync] != RS6240_HIF_PHY_MAGIC) {
            ++sync;
        }
        if (sync > 0u) {
            stream_discard(stream, sync);
            if (stream->input_size < BASE_HEADER_SIZE) {
                return RS6240_HIF_NEED_MORE;
            }
        }

        for (i = 0; i < BASE_HEADER_SIZE; ++i) {
            sum = (uint8_t)(sum + stream->input[i]);
        }
        if (sum != 0xFFu) {
            stream->stats.bad_headers++;
            stream_discard(stream, 1u);
            continue;
        }

        needed = expected_frame_size(stream->input, spi);
        if (needed > RS6240_HIF_FRAME_CAPACITY) {
            stream->stats.bad_headers++;
            stream_discard(stream, 1u);
            continue;
        }
        if (stream->input_size < needed) {
            return RS6240_HIF_NEED_MORE;
        }

        memcpy(stream->frame, stream->input, needed);
        stream->frame_size = needed;
        status = spi
            ? rs6240_hif_decode_spi(stream->frame, needed, message, &consumed)
            : rs6240_hif_decode(stream->frame, needed, message, &consumed);
        if (status == RS6240_HIF_BAD_DATA_CHECKSUM) {
            stream->stats.bad_checksums++;
            stream_discard(stream, 1u);
            continue;
        }
        if (status != RS6240_HIF_OK || consumed != needed) {
            stream->stats.bad_headers++;
            stream_discard(stream, 1u);
            continue;
        }

        // 载荷指向独立帧缓冲，移除数据不影响它
        if (needed < stream->input_size) {
            memmove(stream->input, stream->input + needed, stream->input_size - needed);
        }
        stream->input_size -= needed;
        stream->stats.frames_decoded++;
        return RS6240_HIF_OK;
    }
}
