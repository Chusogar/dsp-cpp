// dsp-cpp on the ESP32 "Cheap Yellow Display" (ESP32-2432S028R, also the
// ST7789 "CYD2USB"): ESP32-WROOM-32, 4 MB flash, no PSRAM.
//   display   2.8" 320x240 ILI9341 (or ST7789) on HSPI: SCK 14, MOSI 13, CS 15,
//             DC 2, backlight 21 (lcd.cpp)
//   touch     XPT2046: CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 (touch.cpp)
//   buttons   BOOT (GPIO0) is button A; more on free GPIOs with --buttons
//   nunchuk   (--galagino / --nunchuk) Wii Nunchuk on CN1, SDA 22, SCL 27, as
//             galagino: stick, Z = A (fire), C = B; BOOT = coin then start,
//             held = menu (nunchuk.cpp)
//   sound     speaker connector, amplifier on GPIO26 (DAC2, I2S0), 22050 Hz
//   files     micro-SD on VSPI: SCK 18, MISO 19, MOSI 23, CS 5, mounted on /sd
// The emulator is the normal dsp code driven by ports/common (menu, frame loop).
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <esp_timer.h>

#include "src/gen/dsp_port_config.h"

#include "esp32_support.h"
#include "lcd.h"
#include "nunchuk.h"
#include "port.h"
#include "touch.h"

SET_LOOP_TASK_STACK_SIZE(DSP_LOOP_STACK_BYTES);

namespace {

constexpr int kWidth = 320, kHeight = 240;
constexpr int kAudioRate = 22050;

SPIClass g_sd_spi(VSPI);

// Size of the last allocation the heap could not make (for memory_note()).
volatile size_t g_failed_request = 0;
void on_failed_alloc(size_t size, uint32_t caps, const char* function) {
    (void)caps, (void)function;
    g_failed_request = size;
}

// Touch screen as a game pad (the XPT2046 sees one finger at a time):
//
//   +------------+------------+------------+
//   |            |   SELECT   |            |
//   |   D-PAD    |  (coin)    |     B      |
//   |  (left     +------------+            |
//   |   third:   |   START    +------------+
//   |   push     |            |            |
//   |   away     +------------+     A      |
//   |   from the |  MENU (F12,|            |
//   |   centre)  |   hold)    |            |
//   +------------+------------+------------+
void touch_to_pad(int x, int y, dsp::port::HostInput& in) {
    if (x < kWidth / 3) {
        const int dx = x - kWidth / 6, dy = y - kHeight / 2;
        const int dead = 14;
        const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        // A diagonal when both components are large enough.
        if (ax > dead && ax * 2 > ay) (dx < 0 ? in.pad_left : in.pad_right) = true;
        if (ay > dead && ay * 2 > ax) (dy < 0 ? in.pad_up : in.pad_down) = true;
    } else if (x >= kWidth * 2 / 3) {
        (y < kHeight / 2 ? in.pad_b : in.pad_a) = true;
    } else if (y < kHeight / 3) {
        in.pad_select = true;
    } else if (y < kHeight * 2 / 3) {
        in.pad_start = true;
    } else {
        in.keys[size_t(dsp::Key::F12)] = true;
    }
}

bool button(int gpio) { return gpio >= 0 && digitalRead(gpio) == LOW; }

class CydHal : public dsp::port::Hal {
public:
    const char* board_name() const override { return "ESP32 CYD"; }
    // Without PSRAM a picture larger than the screen is not kept: drivers
    // that can make it 320x240 (cropped or, for vectors, scaled) do.
    bool crops_picture() const override { return ESP.getPsramSize() == 0; }
    // Added to "not enough memory".
    std::string memory_note() override {
        char text[96];
        std::snprintf(text, sizeof text, "asked %u KB; %u KB free, largest %u KB",
                      unsigned((g_failed_request + 1023) / 1024),
                      unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
        return text;
    }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }

    void present(const uint32_t* argb, int width, int height, int display_width,
                 int display_height) override {
        wait_present();
        if (map_.update(width, height, display_width, display_height, kWidth, kHeight)) {
            cyd::lcd_fill(0, 0, kWidth, kHeight, 0);
        }
        cyd::lcd_window(map_.dst_x, map_.dst_y, map_.dst_x + map_.dst_w - 1, map_.dst_y + map_.dst_h - 1);
        const uint16_t* xs = map_.xs.data();
        int band = 0;
        for (int y0 = 0; y0 < map_.dst_h; y0 += cyd::kBandRows) {
            const int rows = std::min(cyd::kBandRows, map_.dst_h - y0);
            uint16_t* out = cyd::lcd_band(band);
            for (int r = 0; r < rows; ++r) {
                const uint32_t* src = argb + size_t(map_.ys[size_t(y0 + r)]) * size_t(width);
                uint16_t* dst = out + r * map_.dst_w;
                for (int x = 0; x < map_.dst_w; ++x) dst[x] = cyd::to565be(src[xs[x]]);
            }
            cyd::lcd_push(out, rows * map_.dst_w);  // converts the next band meanwhile
            band ^= 1;
        }
        cyd::lcd_wait();
    }

    // The 8-bit picture goes to the LCD from core 0 (present_task) while the
    // emulation carries on on core 1: at 40 MHz the SPI needs ~30 ms for a
    // frame, two frames' worth of time the emulation used to wait for. A
    // frame that comes while the previous one is still being sent is dropped;
    // the pixels are read as they are sent (a row can show the next frame).
    bool present_indexed(const uint8_t* pens, const uint32_t* palette, int width, int height,
                         int display_width, int display_height) override {
        if (job_busy_) return true;  // core 0 still on the previous frame
        if (present_task_ == nullptr) {
            xTaskCreatePinnedToCore(present_task, "dsp-lcd", 3072, this, 2, &present_task_, 0);
        }
        Job& j = job_;
        for (int i = 0; i < 256; ++i) j.lut[i] = cyd::to565be(palette[i]);
        j.pens = pens;
        j.width = width;
        j.height = height;
        j.display_width = display_width;
        j.display_height = display_height;
        job_busy_ = true;
        xTaskNotifyGive(present_task_);
        return true;
    }

    void draw_rows(int y, int rows, const uint32_t* argb) override {
        wait_present();
        cyd::lcd_window(0, y, kWidth - 1, y + rows - 1);
        int band = 0;
        for (int r0 = 0; r0 < rows; r0 += cyd::kBandRows) {
            const int n = std::min(cyd::kBandRows, rows - r0);
            uint16_t* out = cyd::lcd_band(band);
            const uint32_t* src = argb + size_t(r0) * kWidth;
            for (int i = 0; i < n * kWidth; ++i) out[i] = cyd::to565be(src[i]);
            cyd::lcd_push(out, n * kWidth);
            band ^= 1;
        }
        cyd::lcd_wait();
        map_.invalidate();
    }

    int output_rate() const override { return kAudioRate; }
#if DSP_NO_SOUND
    bool sound() const override { return false; }
    void push_audio(const int16_t*, size_t) override {}
    size_t audio_queued() const override { return 0; }
#else
    void push_audio(const int16_t* samples, size_t count) override {
        dsp::port::esp32::audio_push(samples, count);
    }
    size_t audio_queued() const override { return dsp::port::esp32::audio_queued(); }
#endif

    void poll(dsp::port::HostInput& in) override {
        in = dsp::port::HostInput();
        in.pad_connected = true;
        int x, y;
        if (cyd::touch_read(&x, &y)) {
            touch_to_pad(x, y, in);
            // A coin is a short pulse however long the zone is held: a coin
            // switch held down is ignored by many games (Star Wars).
            const bool select = in.pad_select;
            in.pad_select = select && !touch_select_held_;
            if (select && !touch_select_held_) coin_polls_ = kCoinPolls;
            touch_select_held_ = select;
#if DSP_TOUCH_DEBUG
            static uint32_t last = 0;
            if (millis() - last > 300) {
                int rx, ry;
                cyd::touch_raw(&rx, &ry);
                Serial.printf("touch raw %d,%d -> %d,%d\n", rx, ry, x, y);
                last = millis();
            }
#endif
        } else {
            touch_select_held_ = false;
        }
#if DSP_NUNCHUK
        const cyd::NunchukState n = cyd::nunchuk_read();
        in.pad_up = in.pad_up || n.up;
        in.pad_down = in.pad_down || n.down;
        in.pad_left = in.pad_left || n.left;
        in.pad_right = in.pad_right || n.right;
        in.pad_a = in.pad_a || n.z;
        in.pad_b = in.pad_b || n.c;
#endif
#if DSP_BOOT_COIN_START
        boot_coin_start(in);
#endif
        if (coin_polls_ > 0 && !in.pad_select) {
            in.pad_select = true;
            --coin_polls_;
        }
        in.pad_a = in.pad_a || button(DSP_BTN_A);
        in.pad_b = in.pad_b || button(DSP_BTN_B);
        in.pad_start = in.pad_start || button(DSP_BTN_START);
        in.pad_select = in.pad_select || button(DSP_BTN_SELECT);
    }

    uint64_t micros() override { return uint64_t(esp_timer_get_time()); }
    void sleep_us(uint32_t us) override {
        if (us >= 2000) vTaskDelay(pdMS_TO_TICKS(us / 1000));
        else delayMicroseconds(us);
    }
    void idle() override { vTaskDelay(1); }

    std::string storage_root() override {
        if (!sd_mounted_) {
            g_sd_spi.begin(DSP_SD_CLK, DSP_SD_MISO, DSP_SD_MOSI, DSP_SD_CS);
            sd_mounted_ = SD.begin(DSP_SD_CS, g_sd_spi, 20000000, "/sd", 5);
        }
        return sd_mounted_ ? "/sd" : "";
    }
    bool mount_embedded(const DspEmbeddedFile* files, size_t count, std::string* dir,
                        std::string* error) override {
        if (!dsp::port::esp32::mount_embedded(files, count, "/embedded")) {
            *error = "cannot register the /embedded file system";
            return false;
        }
        *dir = "/embedded";
        return true;
    }
    void log(const char* text) override {
        Serial.printf("%s  [heap %u KB free, largest block %u KB]\n", text,
                      unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    }

private:
#if DSP_BOOT_COIN_START
    // galagino's single button on the CYD (BOOT, GPIO0): a press inserts a
    // coin, then presses START; held for a second and a half it goes back
    // to the menu (F12 held). In the menu START picks the game. Counted in
    // polls (one a frame), not milliseconds, so a slow game still sees each
    // step for a few frames, and a short tap does the whole sequence.
    void boot_coin_start(dsp::port::HostInput& in) {
        const bool down = digitalRead(0) == LOW;
        const uint32_t now = millis();
        if (down && !boot_down_) {
            boot_since_ = now;
            boot_step_ = 0;
        }
        boot_down_ = down;
        if (boot_step_ >= 0) {
            const int step = boot_step_++;
            if (step < kCoinPolls) in.pad_select = true;                                  // coin
            else if (step >= 2 * kCoinPolls && step < 3 * kCoinPolls) in.pad_start = true;  // start
            else if (step >= 3 * kCoinPolls) boot_step_ = -1;
        }
        if (down && now - boot_since_ >= 1500) in.keys[size_t(dsp::Key::F12)] = true;  // menu
    }
    bool boot_down_ = false;
    uint32_t boot_since_ = 0;
    int boot_step_ = -1;  // polls since the press; -1: done
#endif
    static constexpr int kCoinPolls = 3;
    int coin_polls_ = 0;
    bool touch_select_held_ = false;

    struct Job {
        const uint8_t* pens = nullptr;
        int width = 0, height = 0, display_width = 0, display_height = 0;
        uint16_t lut[256];
    };

    void wait_present() {
        while (job_busy_) vTaskDelay(1);
    }

    static void present_task(void* self) {
        CydHal* hal = static_cast<CydHal*>(self);
        for (;;) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            hal->send_job();
            hal->job_busy_ = false;
        }
    }

    void send_job() {
        const Job& j = job_;
        if (map_.update(j.width, j.height, j.display_width, j.display_height, kWidth, kHeight)) {
            cyd::lcd_fill(0, 0, kWidth, kHeight, 0);
        }
        cyd::lcd_window(map_.dst_x, map_.dst_y, map_.dst_x + map_.dst_w - 1, map_.dst_y + map_.dst_h - 1);
        const uint16_t* xs = map_.xs.data();
        int band = 0;
        for (int y0 = 0; y0 < map_.dst_h; y0 += cyd::kBandRows) {
            const int rows = std::min(cyd::kBandRows, map_.dst_h - y0);
            uint16_t* out = cyd::lcd_band(band);
            for (int r = 0; r < rows; ++r) {
                const uint8_t* src = j.pens + size_t(map_.ys[size_t(y0 + r)]) * size_t(j.width);
                uint16_t* dst = out + r * map_.dst_w;
                for (int x = 0; x < map_.dst_w; ++x) dst[x] = j.lut[src[xs[x]]];
            }
            cyd::lcd_push(out, rows * map_.dst_w);  // converts the next band meanwhile
            band ^= 1;
        }
        cyd::lcd_wait();
    }

    Job job_;
    volatile bool job_busy_ = false;
    TaskHandle_t present_task_ = nullptr;
    dsp::port::ScaleMap map_;
    bool sd_mounted_ = false;
};

CydHal* g_hal = nullptr;

}  // namespace

void setup() {
    Serial.begin(115200);
    heap_caps_register_failed_alloc_callback(on_failed_alloc);
    // RGB LED off (active low).
    for (int pin : {4, 16, 17}) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
    for (int pin : {DSP_BTN_A, DSP_BTN_B, DSP_BTN_START, DSP_BTN_SELECT})
        if (pin >= 0) pinMode(pin, pin >= 34 ? INPUT : INPUT_PULLUP);  // 34-39: no pull-ups
#if DSP_BOOT_COIN_START
    pinMode(0, INPUT_PULLUP);
#endif
#if DSP_NUNCHUK
    cyd::nunchuk_begin(DSP_NUNCHUK_SDA, DSP_NUNCHUK_SCL, DSP_NUNCHUK_THRESHOLD);
#endif

    cyd::LcdConfig lcd;
    lcd.st7789 = DSP_LCD_ST7789;
    lcd.invert = DSP_LCD_INVERT;
    lcd.bgr = DSP_LCD_BGR;
    lcd.flip = DSP_LCD_FLIP;
    lcd.mhz = DSP_LCD_MHZ;
    if (!cyd::lcd_begin(lcd)) Serial.println("display initialisation failed");

#if !DSP_NO_SOUND
    dsp::port::esp32::audio_begin(kAudioRate, 26);
#endif
    // After the audio: its DAC setup touches GPIO25, the touch clock.
    cyd::TouchCalibration cal;
    cal.x_min = DSP_TOUCH_X_MIN;
    cal.x_max = DSP_TOUCH_X_MAX;
    cal.y_min = DSP_TOUCH_Y_MIN;
    cal.y_max = DSP_TOUCH_Y_MAX;
    cal.swap_xy = DSP_TOUCH_SWAP_XY;
    cal.flip_x = DSP_TOUCH_FLIP_X;
    cal.flip_y = DSP_TOUCH_FLIP_Y;
    cyd::touch_begin(cal);
    g_hal = new CydHal();
    Serial.printf("dsp-cpp ESP32 CYD: heap %u KB free, largest block %u KB, PSRAM %u KB\n",
                  unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024),
                  unsigned(ESP.getPsramSize() / 1024));
}

void loop() {
    dsp::port::run(*g_hal);  // does not return
}
