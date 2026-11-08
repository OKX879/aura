#ifndef RS6240_CH347_H
#define RS6240_CH347_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rs6240_ch347 rs6240_ch347_t;

rs6240_ch347_t *rs6240_ch347_open(uint32_t index, uint32_t spi_hz);
void rs6240_ch347_close(rs6240_ch347_t *device);
int rs6240_ch347_set_cs(rs6240_ch347_t *device, int active);
int rs6240_ch347_gpio_get(rs6240_ch347_t *device, uint8_t *direction, uint8_t *data);

// 单次传输，最大 4096 字节，缓冲区原地读写
int rs6240_ch347_transfer(
    rs6240_ch347_t *device,
    const uint8_t *tx,
    uint8_t *rx,
    size_t size);

// 保持片选连续，把长传输拆块发送
int rs6240_ch347_transfer_continuous(
    rs6240_ch347_t *device,
    const uint8_t *tx,
    uint8_t *rx,
    size_t size,
    size_t chunk_size);

// 长块读取，保持 SPI 时钟连续
int rs6240_ch347_read_large(
    rs6240_ch347_t *device,
    uint8_t *rx,
    size_t size);

#ifdef __cplusplus
}
#endif

#endif
