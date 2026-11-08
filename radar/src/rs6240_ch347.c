#include "rs6240_ch347.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

typedef BOOL (WINAPI *open_device_fn)(ULONG index);
typedef void (WINAPI *close_device_fn)(ULONG index);
typedef BOOL (WINAPI *spi_init_fn)(ULONG index, PVOID config);
typedef BOOL (WINAPI *spi_set_frequency_fn)(ULONG index, ULONG frequency);
typedef BOOL (WINAPI *spi_write_read_fn)(ULONG index, ULONG chip_select,
                                         ULONG length, PVOID io_buffer);
typedef BOOL (WINAPI *spi_change_cs_fn)(ULONG index, UCHAR status);
typedef BOOL (WINAPI *spi_read_fn)(ULONG index, ULONG chip_select,
                                   ULONG write_length, PULONG read_length,
                                   PVOID io_buffer);
typedef BOOL (WINAPI *gpio_get_fn)(ULONG index, PUCHAR direction, PUCHAR data);

#pragma pack(push, 1)
typedef struct ch347_spi_config {
    UCHAR mode;
    UCHAR clock;
    UCHAR byte_order;
    USHORT write_read_interval;
    UCHAR output_default;
    ULONG chip_select;
    UCHAR cs1_polarity;
    UCHAR cs2_polarity;
    USHORT auto_deactivate_cs;
    USHORT active_delay;
    ULONG deactivate_delay;
} ch347_spi_config_t;
#pragma pack(pop)

struct rs6240_ch347 {
    HMODULE dll;
    uint32_t index;
    open_device_fn open_device;
    close_device_fn close_device;
    spi_init_fn spi_init;
    spi_set_frequency_fn set_frequency;
    spi_write_read_fn write_read;
    spi_change_cs_fn change_cs;
    spi_read_fn spi_read;
    gpio_get_fn gpio_get;
    uint8_t *io_buffer;
};

static FARPROC required_proc(HMODULE dll, const char *name)
{
    return GetProcAddress(dll, name);
}

rs6240_ch347_t *rs6240_ch347_open(uint32_t index, uint32_t spi_hz)
{
    rs6240_ch347_t *device;
    ch347_spi_config_t config;
    FARPROC proc;

    device = (rs6240_ch347_t *)calloc(1, sizeof(*device));
    if (!device) {
        return NULL;
    }
    device->io_buffer = (uint8_t *)malloc(4096u);
    if (!device->io_buffer) {
        free(device);
        return NULL;
    }
    device->dll = LoadLibraryA("CH347DLLA64.DLL");
    if (!device->dll) {
        free(device->io_buffer);
        free(device);
        return NULL;
    }
    device->index = index;

    proc = required_proc(device->dll, "CH347OpenDevice");
    memcpy(&device->open_device, &proc, sizeof(device->open_device));
    proc = required_proc(device->dll, "CH347CloseDevice");
    memcpy(&device->close_device, &proc, sizeof(device->close_device));
    proc = required_proc(device->dll, "CH347SPI_Init");
    memcpy(&device->spi_init, &proc, sizeof(device->spi_init));
    proc = required_proc(device->dll, "CH347SPI_SetFrequency");
    memcpy(&device->set_frequency, &proc, sizeof(device->set_frequency));
    proc = required_proc(device->dll, "CH347SPI_WriteRead");
    memcpy(&device->write_read, &proc, sizeof(device->write_read));
    proc = required_proc(device->dll, "CH347SPI_ChangeCS");
    memcpy(&device->change_cs, &proc, sizeof(device->change_cs));
    proc = required_proc(device->dll, "CH347SPI_Read");
    memcpy(&device->spi_read, &proc, sizeof(device->spi_read));
    proc = required_proc(device->dll, "CH347GPIO_Get");
    memcpy(&device->gpio_get, &proc, sizeof(device->gpio_get));

    if (!device->open_device || !device->close_device || !device->spi_init ||
        !device->set_frequency || !device->write_read || !device->change_cs ||
        !device->spi_read ||
        !device->open_device(index)) {
        FreeLibrary(device->dll);
        free(device->io_buffer);
        free(device);
        return NULL;
    }

    memset(&config, 0, sizeof(config));
    config.mode = 0;                 /* 主机模式 */
    config.clock = 0;
    config.byte_order = 1;           /* 高位先行 */
    config.output_default = 0xFF;
    config.chip_select = 0x80;       /* 片选 0 */
    // 跨 DLL 调用保持片选，由调用方释放
    config.auto_deactivate_cs = 0;
    if (!device->spi_init(index, &config) || !device->set_frequency(index, spi_hz)) {
        device->close_device(index);
        FreeLibrary(device->dll);
        free(device->io_buffer);
        free(device);
        return NULL;
    }
    return device;
}

void rs6240_ch347_close(rs6240_ch347_t *device)
{
    if (!device) {
        return;
    }
    if (device->close_device) {
        device->close_device(device->index);
    }
    if (device->dll) {
        FreeLibrary(device->dll);
    }
    free(device->io_buffer);
    free(device);
}

int rs6240_ch347_set_cs(rs6240_ch347_t *device, int active)
{
    if (!device || !device->change_cs) {
        return 0;
    }
    return device->change_cs(device->index, active ? 1u : 0u) ? 1 : 0;
}

int rs6240_ch347_gpio_get(rs6240_ch347_t *device, uint8_t *direction, uint8_t *data)
{
    UCHAR dir;
    UCHAR value;
    if (!device || !direction || !data || !device->gpio_get) {
        return 0;
    }
    if (!device->gpio_get(device->index, &dir, &value)) {
        return 0;
    }
    *direction = dir;
    *data = value;
    return 1;
}

int rs6240_ch347_transfer(
    rs6240_ch347_t *device,
    const uint8_t *tx,
    uint8_t *rx,
    size_t size)
{
    BOOL ok;
    if (!device || !device->io_buffer || (!tx && !rx) || size == 0u || size > 4096u) {
        return 0;
    }
    if (tx) {
        memcpy(device->io_buffer, tx, size);
    } else {
        memset(device->io_buffer, 0xFF, size);
    }
    ok = device->write_read(device->index, 0x80u, (ULONG)size, device->io_buffer);
    if (ok && rx) {
        memcpy(rx, device->io_buffer, size);
    }
    if (!device->change_cs(device->index, 0u)) {
        ok = FALSE;
    }
    return ok ? 1 : 0;
}

int rs6240_ch347_transfer_continuous(
    rs6240_ch347_t *device,
    const uint8_t *tx,
    uint8_t *rx,
    size_t size,
    size_t chunk_size)
{
    size_t offset = 0u;
    int ok = 1;

    if (!device || !device->io_buffer || (!tx && !rx) || size == 0u ||
        chunk_size == 0u || chunk_size > 4096u) {
        return 0;
    }
    if (!device->change_cs(device->index, 1u)) {
        return 0;
    }
    while (offset < size) {
        size_t count = size - offset;
        if (count > chunk_size) {
            count = chunk_size;
        }
        if (tx) {
            memcpy(device->io_buffer, tx + offset, count);
        } else {
            memset(device->io_buffer, 0xFF, count);
        }
        // 片选已拉低，重复选中不会产生上升沿
        if (!device->write_read(device->index, 0x80u, (ULONG)count, device->io_buffer)) {
            ok = 0;
            break;
        }
        if (rx) {
            memcpy(rx + offset, device->io_buffer, count);
        }
        offset += count;
    }
    if (!device->change_cs(device->index, 0u)) {
        ok = 0;
    }
    return ok;
}

int rs6240_ch347_read_large(
    rs6240_ch347_t *device,
    uint8_t *rx,
    size_t size)
{
    ULONG read_length;
    BOOL ok;
    if (!device || !rx || size == 0u || size > 0xFFFFFFFFu) {
        return 0;
    }
    read_length = (ULONG)size;
    memset(rx, 0xFF, size);
    ok = device->spi_read(device->index, 0x80u, 0u, &read_length, rx);
    if (!device->change_cs(device->index, 0u)) {
        ok = FALSE;
    }
    return (ok && read_length == (ULONG)size) ? 1 : 0;
}

#else

struct rs6240_ch347 { int unused; };
rs6240_ch347_t *rs6240_ch347_open(uint32_t index, uint32_t spi_hz)
{
    (void)index; (void)spi_hz; return NULL;
}
void rs6240_ch347_close(rs6240_ch347_t *device) { (void)device; }
int rs6240_ch347_set_cs(rs6240_ch347_t *device, int active)
{
    (void)device; (void)active; return 0;
}
int rs6240_ch347_gpio_get(rs6240_ch347_t *device, uint8_t *direction, uint8_t *data)
{
    (void)device; (void)direction; (void)data; return 0;
}
int rs6240_ch347_transfer(rs6240_ch347_t *device, const uint8_t *tx,
                          uint8_t *rx, size_t size)
{
    (void)device; (void)tx; (void)rx; (void)size; return 0;
}
int rs6240_ch347_transfer_continuous(rs6240_ch347_t *device,
                                     const uint8_t *tx, uint8_t *rx,
                                     size_t size, size_t chunk_size)
{
    (void)device; (void)tx; (void)rx; (void)size; (void)chunk_size; return 0;
}
int rs6240_ch347_read_large(rs6240_ch347_t *device, uint8_t *rx, size_t size)
{
    (void)device; (void)rx; (void)size; return 0;
}

#endif
