#ifndef RS6240_SPIDEV_H
#define RS6240_SPIDEV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rs6240_spidev rs6240_spidev_t;

// 打开 spidev 节点，模式 0、8 位字长
rs6240_spidev_t *rs6240_spidev_open(const char *path, uint32_t spi_hz);
void rs6240_spidev_close(rs6240_spidev_t *device);

// 片选由内核控制，接口与 CH347 版本保持一致
int rs6240_spidev_set_cs(rs6240_spidev_t *device, int active);
int rs6240_spidev_gpio_get(rs6240_spidev_t *device, uint8_t *direction, uint8_t *data);

int rs6240_spidev_transfer(
    rs6240_spidev_t *device,
    const uint8_t *tx,
    uint8_t *rx,
    size_t size);

// 一条 SPI 消息分多段传输，保持片选有效
int rs6240_spidev_transfer_continuous(
    rs6240_spidev_t *device,
    const uint8_t *tx,
    uint8_t *rx,
    size_t size,
    size_t chunk_size);

// 全双工读取任意长度，MOSI 固定发 0xFF
int rs6240_spidev_read_large(
    rs6240_spidev_t *device,
    uint8_t *rx,
    size_t size);

#ifdef __cplusplus
}
#endif

#endif
