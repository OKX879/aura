#include "rs6240_roi.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t read_le16s(const uint8_t *p)
{
    return (int16_t)read_le16(p);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

rs6240_roi_status_t rs6240_roi_decode(
    const uint8_t *payload,
    size_t payload_size,
    rs6240_roi_frame_t *frame)
{
    uint16_t version;
    uint16_t header_size;
    uint32_t point_count;
    size_t expected_size;
    uint32_t i;

    if (!payload || !frame) {
        return RS6240_ROI_INVALID_ARGUMENT;
    }
    if (payload_size < RS6240_ROI_HEADER_SIZE || read_le32(payload) != RS6240_ROI_MAGIC) {
        return RS6240_ROI_BAD_PAYLOAD;
    }
    version = read_le16(payload + 4u);
    header_size = read_le16(payload + 6u);
    if (version != RS6240_ROI_VERSION) {
        return RS6240_ROI_UNSUPPORTED_VERSION;
    }
    if (header_size != RS6240_ROI_HEADER_SIZE) {
        return RS6240_ROI_BAD_PAYLOAD;
    }

    memset(frame, 0, sizeof(*frame));
    frame->frame_index = read_le32(payload + 8u);
    frame->core_ticks = read_le32(payload + 12u);
    frame->queue_overrun = read_le32(payload + 16u);
    frame->range_start = read_le16(payload + 20u);
    frame->range_count = read_le16(payload + 22u);
    frame->tx_count = payload[24u];
    frame->rx_count = payload[25u];
    frame->doppler_index = payload[26u];
    frame->flags = payload[27u];
    point_count = read_le32(payload + 28u);

    if (frame->range_count == 0u || frame->range_count > RS6240_ROI_MAX_RANGE_BINS ||
        frame->tx_count == 0u || frame->rx_count == 0u ||
        (uint32_t)frame->tx_count * frame->rx_count > RS6240_ROI_MAX_CHANNELS ||
        point_count != (uint32_t)frame->range_count * frame->tx_count * frame->rx_count ||
        point_count > RS6240_ROI_MAX_POINTS) {
        return RS6240_ROI_RANGE_ERROR;
    }
    expected_size = RS6240_ROI_HEADER_SIZE + (size_t)point_count * RS6240_CUBE_POINT_SIZE;
    if (payload_size != expected_size) {
        return RS6240_ROI_BAD_PAYLOAD;
    }

    frame->point_count = point_count;
    for (i = 0u; i < point_count; ++i) {
        const uint8_t *point = payload + RS6240_ROI_HEADER_SIZE +
                               (size_t)i * RS6240_CUBE_POINT_SIZE;
        frame->points[i].imag = read_le16s(point);
        frame->points[i].real = read_le16s(point + 2u);
    }
    return RS6240_ROI_OK;
}

rs6240_roi_status_t rs6240_roi_point_index(
    const rs6240_roi_frame_t *frame,
    uint8_t tx,
    uint8_t rx,
    uint16_t range_offset,
    uint32_t *point_index)
{
    if (!frame || !point_index) {
        return RS6240_ROI_INVALID_ARGUMENT;
    }
    if (tx >= frame->tx_count || rx >= frame->rx_count ||
        range_offset >= frame->range_count) {
        return RS6240_ROI_RANGE_ERROR;
    }
    *point_index = ((uint32_t)tx * frame->rx_count + rx) * frame->range_count + range_offset;
    return RS6240_ROI_OK;
}
