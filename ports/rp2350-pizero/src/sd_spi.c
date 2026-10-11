// FatFs disk driver for the micro-SD slot of the Waveshare RP2350-PiZero, in
// SPI mode on SPI1: SCK GPIO30, MOSI GPIO31, MISO GPIO40, CS GPIO43 (the pins
// can be changed with the DSP_SD_* definitions in CMakeLists.txt).
// SDSC, SDHC and SDXC cards; one drive (pdrv 0).
#include <string.h>

#include "ff.h"
#include "diskio.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#ifndef DSP_SD_SPI
#define DSP_SD_SPI spi1
#endif
#ifndef DSP_SD_SCK
#define DSP_SD_SCK 30
#endif
#ifndef DSP_SD_MOSI
#define DSP_SD_MOSI 31
#endif
#ifndef DSP_SD_MISO
#define DSP_SD_MISO 40
#endif
#ifndef DSP_SD_CS
#define DSP_SD_CS 43
#endif
#ifndef DSP_SD_BAUD
#define DSP_SD_BAUD (25 * 1000 * 1000)
#endif

#define CMD0 0     // GO_IDLE_STATE
#define CMD8 8     // SEND_IF_COND
#define CMD9 9     // SEND_CSD
#define CMD12 12   // STOP_TRANSMISSION
#define CMD16 16   // SET_BLOCKLEN
#define CMD17 17   // READ_SINGLE_BLOCK
#define CMD18 18   // READ_MULTIPLE_BLOCK
#define CMD24 24   // WRITE_BLOCK
#define CMD25 25   // WRITE_MULTIPLE_BLOCK
#define CMD55 55   // APP_CMD
#define CMD58 58   // READ_OCR
#define ACMD41 (0x80 | 41)

static volatile DSTATUS g_status = STA_NOINIT;
static bool g_block_addressing = false;  // SDHC/SDXC: sector numbers, SDSC: bytes

static inline void cs_high(void) { gpio_put(DSP_SD_CS, 1); }
static inline void cs_low(void) { gpio_put(DSP_SD_CS, 0); }

static uint8_t xchg(uint8_t out) {
    uint8_t in;
    spi_write_read_blocking(DSP_SD_SPI, &out, &in, 1);
    return in;
}

static bool wait_ready(uint32_t timeout_ms) {
    const absolute_time_t end = make_timeout_time_ms(timeout_ms);
    do {
        if (xchg(0xff) == 0xff) return true;
    } while (absolute_time_diff_us(get_absolute_time(), end) > 0);
    return false;
}

static void deselect(void) {
    cs_high();
    xchg(0xff);  // one more clock to release MISO
}

static bool select_card(void) {
    cs_low();
    xchg(0xff);
    if (wait_ready(500)) return true;
    deselect();
    return false;
}

// Sends a command; returns the R1 response (bit 7 set = timeout).
static uint8_t send_cmd(uint8_t cmd, uint32_t arg) {
    if (cmd & 0x80) {  // ACMDn = CMD55 + CMDn
        cmd &= 0x7f;
        const uint8_t r = send_cmd(CMD55, 0);
        if (r > 1) return r;
    }
    if (cmd != CMD12) {
        deselect();
        if (!select_card()) return 0xff;
    }
    uint8_t frame[6] = {(uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
                        (uint8_t)(arg >> 8), (uint8_t)arg, 0x01};
    if (cmd == CMD0) frame[5] = 0x95;
    if (cmd == CMD8) frame[5] = 0x87;
    spi_write_blocking(DSP_SD_SPI, frame, 6);
    if (cmd == CMD12) xchg(0xff);  // skip a stuff byte
    uint8_t r;
    int n = 10;
    do {
        r = xchg(0xff);
    } while ((r & 0x80) && --n);
    return r;
}

static bool receive_block(uint8_t* buf, unsigned len) {
    const absolute_time_t end = make_timeout_time_ms(200);
    uint8_t token;
    do {
        token = xchg(0xff);
    } while (token == 0xff && absolute_time_diff_us(get_absolute_time(), end) > 0);
    if (token != 0xfe) return false;
    memset(buf, 0xff, len);
    spi_read_blocking(DSP_SD_SPI, 0xff, buf, len);
    xchg(0xff);  // CRC
    xchg(0xff);
    return true;
}

static bool send_block(const uint8_t* buf, uint8_t token) {
    if (!wait_ready(500)) return false;
    xchg(token);
    if (token == 0xfd) return true;  // stop tran
    spi_write_blocking(DSP_SD_SPI, buf, 512);
    xchg(0xff);
    xchg(0xff);
    return (xchg(0xff) & 0x1f) == 0x05;  // data accepted
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    spi_init(DSP_SD_SPI, 400 * 1000);
    gpio_set_function(DSP_SD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(DSP_SD_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(DSP_SD_MISO, GPIO_FUNC_SPI);
    gpio_pull_up(DSP_SD_MISO);
    gpio_init(DSP_SD_CS);
    gpio_set_dir(DSP_SD_CS, GPIO_OUT);
    cs_high();
    for (int i = 0; i < 10; ++i) xchg(0xff);  // 80 clocks with CS high

    g_status = STA_NOINIT;
    g_block_addressing = false;
    if (send_cmd(CMD0, 0) == 1) {
        const absolute_time_t end = make_timeout_time_ms(1000);
        if (send_cmd(CMD8, 0x1aa) == 1) {  // SD v2
            uint8_t ocr[4];
            for (int i = 0; i < 4; ++i) ocr[i] = xchg(0xff);
            if (ocr[2] == 0x01 && ocr[3] == 0xaa) {
                while (absolute_time_diff_us(get_absolute_time(), end) > 0 && send_cmd(ACMD41, 1u << 30))
                    ;
                if (absolute_time_diff_us(get_absolute_time(), end) > 0 && send_cmd(CMD58, 0) == 0) {
                    for (int i = 0; i < 4; ++i) ocr[i] = xchg(0xff);
                    g_block_addressing = (ocr[0] & 0x40) != 0;
                    g_status = 0;
                }
            }
        } else {  // SD v1 (or MMC, not supported)
            while (absolute_time_diff_us(get_absolute_time(), end) > 0 && send_cmd(ACMD41, 0))
                ;
            if (absolute_time_diff_us(get_absolute_time(), end) > 0 && send_cmd(CMD16, 512) == 0)
                g_status = 0;
        }
    }
    deselect();
    if (g_status == 0) spi_set_baudrate(DSP_SD_SPI, DSP_SD_BAUD);
    return g_status;
}

DSTATUS disk_status(BYTE pdrv) { return pdrv == 0 ? g_status : STA_NOINIT; }

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || count == 0) return RES_PARERR;
    if (g_status & STA_NOINIT) return RES_NOTRDY;
    if (!g_block_addressing) sector *= 512;
    if (count == 1) {
        if (send_cmd(CMD17, (uint32_t)sector) == 0 && receive_block(buff, 512)) count = 0;
    } else if (send_cmd(CMD18, (uint32_t)sector) == 0) {
        do {
            if (!receive_block(buff, 512)) break;
            buff += 512;
        } while (--count);
        send_cmd(CMD12, 0);
    }
    deselect();
    return count ? RES_ERROR : RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || count == 0) return RES_PARERR;
    if (g_status & STA_NOINIT) return RES_NOTRDY;
    if (!g_block_addressing) sector *= 512;
    if (count == 1) {
        if (send_cmd(CMD24, (uint32_t)sector) == 0 && send_block(buff, 0xfe)) count = 0;
    } else if (send_cmd(CMD25, (uint32_t)sector) == 0) {
        do {
            if (!send_block(buff, 0xfc)) break;
            buff += 512;
        } while (--count);
        if (!send_block(NULL, 0xfd)) count = 1;
    }
    deselect();
    return count ? RES_ERROR : RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != 0) return RES_PARERR;
    if (g_status & STA_NOINIT) return RES_NOTRDY;
    DRESULT res = RES_ERROR;
    switch (cmd) {
        case CTRL_SYNC:
            if (select_card()) res = RES_OK;
            break;
        case GET_SECTOR_COUNT: {
            uint8_t csd[16];
            if (send_cmd(CMD9, 0) == 0 && receive_block(csd, 16)) {
                if ((csd[0] >> 6) == 1) {  // CSD v2
                    const uint32_t c_size = ((uint32_t)(csd[7] & 0x3f) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
                    *(LBA_t*)buff = (LBA_t)(c_size + 1) << 10;
                } else {  // CSD v1
                    const uint32_t n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                    const uint32_t c_size = ((uint32_t)(csd[8] >> 6)) + ((uint32_t)csd[7] << 2) +
                                            ((uint32_t)(csd[6] & 3) << 10) + 1;
                    *(LBA_t*)buff = (LBA_t)c_size << (n - 9);
                }
                res = RES_OK;
            }
            break;
        }
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 128;
            res = RES_OK;
            break;
        default:
            res = RES_PARERR;
    }
    deselect();
    return res;
}
