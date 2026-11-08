#include "rs6240_cube.h"

#include <stdlib.h>
#include <string.h>

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int16_t read_le16s(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

int rs6240_cube_reassembler_init(
    rs6240_cube_reassembler_t *r,
    uint32_t max_points)
{
    if (!r || max_points == 0u || max_points > (UINT32_MAX / RS6240_CUBE_POINT_SIZE)) {
        return 0;
    }
    memset(r, 0, sizeof(*r));
    r->point_bytes = (uint8_t *)malloc((size_t)max_points * RS6240_CUBE_POINT_SIZE);
    r->received = (uint8_t *)malloc((size_t)max_points);
    if (!r->point_bytes || !r->received) {
        rs6240_cube_reassembler_destroy(r);
        return 0;
    }
    r->capacity_points = max_points;
    return 1;
}

void rs6240_cube_reassembler_reset(rs6240_cube_reassembler_t *r)
{
    if (!r) {
        return;
    }
    r->active = 0;
    r->ready = 0;
    r->frame_index = 0;
    r->frame_points = 0;
    r->received_points = 0;
    if (r->received && r->capacity_points > 0u) {
        memset(r->received, 0, (size_t)r->capacity_points);
    }
}

void rs6240_cube_reassembler_destroy(rs6240_cube_reassembler_t *r)
{
    if (!r) {
        return;
    }
    free(r->point_bytes);
    free(r->received);
    memset(r, 0, sizeof(*r));
}

static void begin_frame(rs6240_cube_reassembler_t *r, uint32_t index, uint32_t points)
{
    if (r->active && !r->ready && r->received_points != r->frame_points) {
        r->stats.incomplete_frames_dropped++;
    }
    r->frame_index = index;
    r->frame_points = points;
    r->received_points = 0;
    r->active = 1;
    r->ready = 0;
    memset(r->received, 0, (size_t)points);
}

rs6240_cube_status_t rs6240_cube_feed(
    rs6240_cube_reassembler_t *r,
    const uint8_t *payload,
    size_t payload_size)
{
    uint32_t frame_index;
    uint32_t frame_points;
    uint32_t point_offset;
    uint32_t fragment_points;
    uint32_t i;
    int all_duplicate = 1;

    if (!r || !payload || !r->point_bytes || !r->received) {
        return RS6240_CUBE_INVALID_ARGUMENT;
    }
    if (payload_size < RS6240_CUBE_FRAGMENT_HEADER_SIZE ||
        ((payload_size - RS6240_CUBE_FRAGMENT_HEADER_SIZE) % RS6240_CUBE_POINT_SIZE) != 0u) {
        return RS6240_CUBE_BAD_PAYLOAD;
    }

    frame_index = read_le32(payload);
    frame_points = read_le32(payload + 4u);
    point_offset = read_le32(payload + 8u);
    fragment_points = (uint32_t)((payload_size - RS6240_CUBE_FRAGMENT_HEADER_SIZE) /
                                 RS6240_CUBE_POINT_SIZE);

    if (frame_points == 0u || frame_points > r->capacity_points) {
        return RS6240_CUBE_FRAME_TOO_LARGE;
    }
    if (point_offset > frame_points || fragment_points > frame_points - point_offset) {
        return RS6240_CUBE_RANGE_ERROR;
    }

    if (!r->active || frame_index != r->frame_index) {
        begin_frame(r, frame_index, frame_points);
    } else if (frame_points != r->frame_points) {
        return RS6240_CUBE_CONFLICT;
    }

    // 先检查冲突，避免留下部分写入的数据
    for (i = 0; i < fragment_points; ++i) {
        uint32_t point = point_offset + i;
        const uint8_t *source = payload + RS6240_CUBE_FRAGMENT_HEADER_SIZE +
                                (size_t)i * RS6240_CUBE_POINT_SIZE;
        uint8_t *destination = r->point_bytes + (size_t)point * RS6240_CUBE_POINT_SIZE;
        if (r->received[point] && memcmp(destination, source, RS6240_CUBE_POINT_SIZE) != 0) {
            r->stats.conflicting_fragments++;
            return RS6240_CUBE_CONFLICT;
        }
    }

    for (i = 0; i < fragment_points; ++i) {
        uint32_t point = point_offset + i;
        const uint8_t *source = payload + RS6240_CUBE_FRAGMENT_HEADER_SIZE +
                                (size_t)i * RS6240_CUBE_POINT_SIZE;
        uint8_t *destination = r->point_bytes + (size_t)point * RS6240_CUBE_POINT_SIZE;
        if (!r->received[point]) {
            memcpy(destination, source, RS6240_CUBE_POINT_SIZE);
            r->received[point] = 1u;
            r->received_points++;
            all_duplicate = 0;
        }
    }

    if (all_duplicate && fragment_points > 0u) {
        r->stats.duplicate_fragments++;
        return RS6240_CUBE_DUPLICATE;
    }
    r->stats.fragments_accepted++;
    if (r->received_points == r->frame_points) {
        if (!r->ready) {
            r->stats.completed_frames++;
        }
        r->ready = 1;
        return RS6240_CUBE_COMPLETE;
    }
    return RS6240_CUBE_ACCEPTED;
}

rs6240_cube_status_t rs6240_cube_copy_frame(
    const rs6240_cube_reassembler_t *r,
    rs6240_complex16_t *output,
    uint32_t output_points,
    uint32_t *frame_index,
    uint32_t *frame_points)
{
    uint32_t i;
    if (!r || !output) {
        return RS6240_CUBE_INVALID_ARGUMENT;
    }
    if (!r->ready) {
        return RS6240_CUBE_FRAME_NOT_READY;
    }
    if (output_points < r->frame_points) {
        return RS6240_CUBE_OUTPUT_TOO_SMALL;
    }
    for (i = 0; i < r->frame_points; ++i) {
        const uint8_t *p = r->point_bytes + (size_t)i * RS6240_CUBE_POINT_SIZE;
        output[i].imag = read_le16s(p);
        output[i].real = read_le16s(p + 2u);
    }
    if (frame_index) {
        *frame_index = r->frame_index;
    }
    if (frame_points) {
        *frame_points = r->frame_points;
    }
    return RS6240_CUBE_COMPLETE;
}

rs6240_cube_status_t rs6240_cube_point_index(
    const rs6240_cube_geometry_t *g,
    uint16_t tx,
    uint16_t rx,
    uint16_t doppler,
    uint16_t range,
    uint32_t *point_index)
{
    uint64_t total;
    uint64_t index;
    if (!g || !point_index || g->tx_count == 0u || g->rx_count == 0u ||
        g->doppler_bins == 0u || g->range_bins == 0u) {
        return RS6240_CUBE_INVALID_ARGUMENT;
    }
    if (tx >= g->tx_count || rx >= g->rx_count ||
        doppler >= g->doppler_bins || range >= g->range_bins) {
        return RS6240_CUBE_RANGE_ERROR;
    }
    total = (uint64_t)g->tx_count * g->rx_count * g->doppler_bins * g->range_bins;
    if (total > UINT32_MAX) {
        return RS6240_CUBE_FRAME_TOO_LARGE;
    }
    index = (((uint64_t)tx * g->rx_count + rx) * g->doppler_bins + doppler) *
            g->range_bins + range;
    *point_index = (uint32_t)index;
    return RS6240_CUBE_ACCEPTED;
}
