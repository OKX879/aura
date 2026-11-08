#ifndef _WIN32

#include "rs6240_spidev.h"

#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct rs6240_spidev {
    int fd;
    uint32_t speed_hz;
};

static int apply_defaults(rs6240_spidev_t *device)
{
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8u;

    if (ioctl(device->fd, SPI_IOC_WR_MODE, &mode) < 0) {
        return 0;
    }
    if (ioctl(device->fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        return 0;
    }
    if (ioctl(device->fd, SPI_IOC_WR_MAX_SPEED_HZ, &device->speed_hz) < 0) {
        return 0;
    }
    return 1;
}

rs6240_spidev_t *rs6240_spidev_open(const char *path, uint32_t spi_hz)
{
    rs6240_spidev_t *device;
    if (!path || spi_hz == 0u) {
        return NULL;
    }
    device = (rs6240_spidev_t *)calloc(1u, sizeof(*device));
    if (!device) {
        return NULL;
    }
    device->fd = open(path, O_RDWR);
    if (device->fd < 0) {
        free(device);
        return NULL;
    }
    device->speed_hz = spi_hz;
    if (!apply_defaults(device)) {
        close(device->fd);
        free(device);
        return NULL;
    }
    return device;
}

void rs6240_spidev_close(rs6240_spidev_t *device)
{
    if (!device) {
        return;
    }
    if (device->fd >= 0) {
        close(device->fd);
    }
    free(device);
}

int rs6240_spidev_set_cs(rs6240_spidev_t *device, int active)
{
    (void)device;
    (void)active;
    return 1;
}

int rs6240_spidev_gpio_get(rs6240_spidev_t *device, uint8_t *direction, uint8_t *data)
{
    (void)device;
    (void)direction;
    (void)data;
    return 0;
}

int rs6240_spidev_transfer(
    rs6240_spidev_t *device,
    const uint8_t *tx,
    uint8_t *rx,
    size_t size)
{
    struct spi_ioc_transfer transfer;
    uint8_t *fallback = NULL;

    if (!device || (!tx && !rx) || size == 0u) {
        return 0;
    }
    if (!tx) {
        fallback = (uint8_t *)malloc(size);
        if (!fallback) {
            return 0;
        }
        memset(fallback, 0xFF, size);
        tx = fallback;
    }
    memset(&transfer, 0, sizeof(transfer));
    transfer.tx_buf = (unsigned long)tx;
    transfer.rx_buf = (unsigned long)rx;
    transfer.len = (unsigned int)size;
    transfer.speed_hz = device->speed_hz;
    transfer.bits_per_word = 8u;
    transfer.cs_change = 0u;
    transfer.delay_usecs = 0u;

    int ok = ioctl(device->fd, SPI_IOC_MESSAGE(1), &transfer) >= 0;
    free(fallback);
    return ok ? 1 : 0;
}

int rs6240_spidev_transfer_continuous(
    rs6240_spidev_t *device,
    const uint8_t *tx,
    uint8_t *rx,
    size_t size,
    size_t chunk_size)
{
    size_t count;
    size_t offset;
    struct spi_ioc_transfer *transfers;
    uint8_t *fallback = NULL;
    int ok;

    if (!device || (!tx && !rx) || size == 0u || chunk_size == 0u) {
        return 0;
    }
    count = (size + chunk_size - 1u) / chunk_size;
    if (count > 32u) {
        // 限制内核传输数组长度
        return 0;
    }
    transfers = (struct spi_ioc_transfer *)calloc(count, sizeof(*transfers));
    if (!transfers) {
        return 0;
    }
    if (!tx) {
        fallback = (uint8_t *)malloc(size);
        if (!fallback) {
            free(transfers);
            return 0;
        }
        memset(fallback, 0xFF, size);
        tx = fallback;
    }
    offset = 0u;
    for (size_t i = 0u; i < count; ++i) {
        size_t length = size - offset;
        if (length > chunk_size) {
            length = chunk_size;
        }
        transfers[i].tx_buf = (unsigned long)(tx + offset);
        transfers[i].rx_buf = rx ? (unsigned long)(rx + offset) : 0ul;
        transfers[i].len = (unsigned int)length;
        transfers[i].speed_hz = device->speed_hz;
        transfers[i].bits_per_word = 8u;
        // 保持片选到最后一个分块
        transfers[i].cs_change = 0u;
        offset += length;
    }
    ok = ioctl(device->fd, SPI_IOC_MESSAGE((int)count), transfers) >= 0;
    free(fallback);
    free(transfers);
    return ok ? 1 : 0;
}

int rs6240_spidev_read_large(
    rs6240_spidev_t *device,
    uint8_t *rx,
    size_t size)
{
    uint8_t *fallback;
    int ok;

    if (!device || !rx || size == 0u) {
        return 0;
    }
    fallback = (uint8_t *)malloc(size);
    if (!fallback) {
        return 0;
    }
    memset(fallback, 0xFF, size);
    ok = rs6240_spidev_transfer(device, fallback, rx, size);
    free(fallback);
    return ok;
}

#endif  /* !_WIN32 */
