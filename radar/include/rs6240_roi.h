#ifndef RS6240_ROI_H
#define RS6240_ROI_H

#include "rs6240_cube.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS6240_ROI_MAGIC 0x31494F52u /* 小端 "ROI1" */
#define RS6240_ROI_VERSION 1u
#define RS6240_ROI_HEADER_SIZE 32u
#define RS6240_ROI_MAX_RANGE_BINS 64u
#define RS6240_ROI_MAX_CHANNELS 8u
#define RS6240_ROI_MAX_POINTS (RS6240_ROI_MAX_RANGE_BINS * RS6240_ROI_MAX_CHANNELS)

typedef enum rs6240_roi_status {
    RS6240_ROI_OK = 0,
    RS6240_ROI_INVALID_ARGUMENT = -1,
    RS6240_ROI_BAD_PAYLOAD = -2,
    RS6240_ROI_UNSUPPORTED_VERSION = -3,
    RS6240_ROI_RANGE_ERROR = -4,
    RS6240_ROI_OUTPUT_TOO_SMALL = -5
} rs6240_roi_status_t;

typedef struct rs6240_roi_frame {
    uint32_t frame_index;
    uint32_t core_ticks;
    uint32_t queue_overrun;
    uint16_t range_start;
    uint16_t range_count;
    uint8_t tx_count;
    uint8_t rx_count;
    uint8_t doppler_index;
    uint8_t flags;
    uint32_t point_count;
    rs6240_complex16_t points[RS6240_ROI_MAX_POINTS];
} rs6240_roi_frame_t;

// 载荷顺序为 TX、RX、距离，距离连续排列
rs6240_roi_status_t rs6240_roi_decode(
    const uint8_t *payload,
    size_t payload_size,
    rs6240_roi_frame_t *frame);

rs6240_roi_status_t rs6240_roi_point_index(
    const rs6240_roi_frame_t *frame,
    uint8_t tx,
    uint8_t rx,
    uint16_t range_offset,
    uint32_t *point_index);

#ifdef __cplusplus
}
#endif

#endif
