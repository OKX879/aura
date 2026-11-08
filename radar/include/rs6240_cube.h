#ifndef RS6240_CUBE_H
#define RS6240_CUBE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS6240_CUBE_FRAGMENT_HEADER_SIZE 12u
#define RS6240_CUBE_POINT_SIZE 4u
#define RS6240_CUBE_VENDOR_DATA_SIZE 3072u

typedef struct rs6240_complex16 {
    int16_t imag;
    int16_t real;
} rs6240_complex16_t;

typedef struct rs6240_cube_geometry {
    uint16_t tx_count;
    uint16_t rx_count;
    uint16_t doppler_bins;
    uint16_t range_bins;
} rs6240_cube_geometry_t;

typedef enum rs6240_cube_status {
    RS6240_CUBE_ACCEPTED = 0,
    RS6240_CUBE_COMPLETE = 1,
    RS6240_CUBE_DUPLICATE = 2,
    RS6240_CUBE_INVALID_ARGUMENT = -1,
    RS6240_CUBE_BAD_PAYLOAD = -2,
    RS6240_CUBE_FRAME_TOO_LARGE = -3,
    RS6240_CUBE_RANGE_ERROR = -4,
    RS6240_CUBE_CONFLICT = -5,
    RS6240_CUBE_FRAME_NOT_READY = -6,
    RS6240_CUBE_OUTPUT_TOO_SMALL = -7
} rs6240_cube_status_t;

typedef struct rs6240_cube_stats {
    uint64_t fragments_accepted;
    uint64_t duplicate_fragments;
    uint64_t conflicting_fragments;
    uint64_t completed_frames;
    uint64_t incomplete_frames_dropped;
} rs6240_cube_stats_t;

typedef struct rs6240_cube_reassembler {
    uint8_t *point_bytes;
    uint8_t *received;
    uint32_t capacity_points;
    uint32_t frame_index;
    uint32_t frame_points;
    uint32_t received_points;
    int active;
    int ready;
    rs6240_cube_stats_t stats;
} rs6240_cube_reassembler_t;

int rs6240_cube_reassembler_init(
    rs6240_cube_reassembler_t *reassembler,
    uint32_t max_points);

void rs6240_cube_reassembler_reset(rs6240_cube_reassembler_t *reassembler);
void rs6240_cube_reassembler_destroy(rs6240_cube_reassembler_t *reassembler);

// 送入一片 0xC1 载荷，偏移按复数点计
rs6240_cube_status_t rs6240_cube_feed(
    rs6240_cube_reassembler_t *reassembler,
    const uint8_t *payload,
    size_t payload_size);

rs6240_cube_status_t rs6240_cube_copy_frame(
    const rs6240_cube_reassembler_t *reassembler,
    rs6240_complex16_t *output,
    uint32_t output_points,
    uint32_t *frame_index,
    uint32_t *frame_points);

// 计算点索引，顺序为 TX、RX、多普勒、距离
rs6240_cube_status_t rs6240_cube_point_index(
    const rs6240_cube_geometry_t *geometry,
    uint16_t tx,
    uint16_t rx,
    uint16_t doppler,
    uint16_t range,
    uint32_t *point_index);

#ifdef __cplusplus
}
#endif

#endif
