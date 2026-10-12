// ILI9341 / ST7789 driver for the CYD: ESP-IDF spi_master on HSPI with DMA.
// Pins of the ESP32-2432S028R: SCK 14, MOSI 13, CS 15, DC 2, backlight 21
// (the reset line is tied to EN). Overridable with DSP_LCD_* in dsp_port_config.h.
#include "lcd.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

#include "src/gen/dsp_port_config.h"

#ifndef DSP_LCD_SCK
#define DSP_LCD_SCK 14
#endif
#ifndef DSP_LCD_MOSI
#define DSP_LCD_MOSI 13
#endif
#ifndef DSP_LCD_CS
#define DSP_LCD_CS 15
#endif
#ifndef DSP_LCD_DC
#define DSP_LCD_DC 2
#endif
#ifndef DSP_LCD_BL
#define DSP_LCD_BL 21
#endif

namespace cyd {

namespace {

spi_device_handle_t g_dev = nullptr;
uint16_t* g_band[2] = {nullptr, nullptr};
spi_transaction_t g_trans[2];
int g_in_flight = 0;
int g_next = 0;

void IRAM_ATTR pre_transfer(spi_transaction_t* t) {
    gpio_set_level(gpio_num_t(DSP_LCD_DC), int(reinterpret_cast<intptr_t>(t->user)));
}

void send(bool data, const uint8_t* bytes, int len) {
    if (len <= 0) return;
    lcd_wait();
    spi_transaction_t t = {};
    t.length = size_t(len) * 8;
    if (len <= 4) {
        t.flags = SPI_TRANS_USE_TXDATA;
        for (int i = 0; i < len; ++i) t.tx_data[i] = bytes[i];
    } else {
        t.tx_buffer = bytes;
    }
    t.user = reinterpret_cast<void*>(intptr_t(data ? 1 : 0));
    spi_device_polling_transmit(g_dev, &t);
}

void cmd(uint8_t c, std::initializer_list<uint8_t> args = {}, int delay_ms = 0) {
    send(false, &c, 1);
    uint8_t buf[16];
    int n = 0;
    for (uint8_t a : args) buf[n++] = a;
    send(true, buf, n);
    if (delay_ms) delay(delay_ms);
}

}  // namespace

bool lcd_begin(const LcdConfig& cfg) {
    gpio_set_direction(gpio_num_t(DSP_LCD_DC), GPIO_MODE_OUTPUT);
    pinMode(DSP_LCD_BL, OUTPUT);
    digitalWrite(DSP_LCD_BL, LOW);

    spi_bus_config_t bus = {};
    bus.mosi_io_num = DSP_LCD_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = DSP_LCD_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 320 * kBandRows * 2 + 16;
    if (spi_bus_initialize(HSPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = cfg.mhz * 1000 * 1000;
    dev.mode = 0;
    dev.spics_io_num = DSP_LCD_CS;
    dev.queue_size = 3;
    dev.pre_cb = pre_transfer;
    dev.flags = SPI_DEVICE_NO_DUMMY;
    if (spi_bus_add_device(HSPI_HOST, &dev, &g_dev) != ESP_OK) return false;
    for (int i = 0; i < 2; ++i) {
        g_band[i] = static_cast<uint16_t*>(heap_caps_malloc(320 * kBandRows * 2, MALLOC_CAP_DMA));
        if (!g_band[i]) return false;
    }

    const uint8_t madctl_base = cfg.st7789 ? 0x60 : 0x28;          // landscape (MV|MX or MV)
    const uint8_t madctl_flip = cfg.st7789 ? 0xa0 : 0xe8;          // landscape, turned 180
    const uint8_t madctl = uint8_t(((cfg.flip ? madctl_flip : madctl_base) & ~0x08) | (cfg.bgr ? 0x08 : 0));

    cmd(0x01, {}, 150);  // software reset
    if (cfg.st7789) {
        cmd(0x11, {}, 120);                    // sleep out
        cmd(0x3a, {0x55});                     // 16 bpp
        cmd(0x36, {madctl});
        cmd(0xb2, {0x0c, 0x0c, 0x00, 0x33, 0x33});
        cmd(0xb7, {0x35});
        cmd(0xbb, {0x28});
        cmd(0xc0, {0x0c});
        cmd(0xc2, {0x01, 0xff});
        cmd(0xc3, {0x10});
        cmd(0xc4, {0x20});
        cmd(0xc6, {0x0f});
        cmd(0xd0, {0xa4, 0xa1});
    } else {
        cmd(0xcf, {0x00, 0xc1, 0x30});
        cmd(0xed, {0x64, 0x03, 0x12, 0x81});
        cmd(0xe8, {0x85, 0x00, 0x78});
        cmd(0xcb, {0x39, 0x2c, 0x00, 0x34, 0x02});
        cmd(0xf7, {0x20});
        cmd(0xea, {0x00, 0x00});
        cmd(0xc0, {0x23});                     // power control 1
        cmd(0xc1, {0x10});                     // power control 2
        cmd(0xc5, {0x3e, 0x28});               // VCOM 1
        cmd(0xc7, {0x86});                     // VCOM 2
        cmd(0x36, {madctl});
        cmd(0x3a, {0x55});                     // 16 bpp
        cmd(0xb1, {0x00, 0x18});               // frame rate 79 Hz
        cmd(0xb6, {0x08, 0x82, 0x27});         // display function
        cmd(0xf2, {0x00});                     // 3-gamma off
        cmd(0x26, {0x01});
        cmd(0xe0, {0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1, 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00});
        cmd(0xe1, {0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1, 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f});
        cmd(0x11, {}, 120);                    // sleep out
    }
    cmd(cfg.invert ? 0x21 : 0x20);             // inversion on/off
    cmd(0x29, {}, 20);                         // display on
    lcd_fill(0, 0, 320, 240, 0);
    digitalWrite(DSP_LCD_BL, HIGH);
    return true;
}

void lcd_window(int x0, int y0, int x1, int y1) {
    cmd(0x2a, {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)});
    cmd(0x2b, {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)});
    const uint8_t c = 0x2c;
    send(false, &c, 1);
}

void lcd_push(const uint16_t* pixels, int count) {
    if (count <= 0) return;
    if (g_in_flight == 2) {  // wait for the oldest
        spi_transaction_t* done;
        spi_device_get_trans_result(g_dev, &done, portMAX_DELAY);
        --g_in_flight;
    }
    spi_transaction_t& t = g_trans[g_next];
    g_next ^= 1;
    t = spi_transaction_t();
    t.length = size_t(count) * 16;
    t.tx_buffer = pixels;
    t.user = reinterpret_cast<void*>(intptr_t(1));
    spi_device_queue_trans(g_dev, &t, portMAX_DELAY);
    ++g_in_flight;
}

void lcd_wait() {
    while (g_in_flight > 0) {
        spi_transaction_t* done;
        spi_device_get_trans_result(g_dev, &done, portMAX_DELAY);
        --g_in_flight;
    }
}

uint16_t* lcd_band(int index) { return g_band[index & 1]; }

void lcd_fill(int x, int y, int w, int h, uint16_t rgb565) {
    if (w <= 0 || h <= 0) return;
    lcd_wait();
    const uint16_t be = uint16_t((rgb565 >> 8) | (rgb565 << 8));
    for (int i = 0; i < 320 * kBandRows; ++i) g_band[0][i] = be;
    lcd_window(x, y, x + w - 1, y + h - 1);
    int left = w * h;
    while (left > 0) {
        const int n = left < 320 * kBandRows ? left : 320 * kBandRows;
        lcd_push(g_band[0], n);
        lcd_wait();
        left -= n;
    }
}

}  // namespace cyd
