// dsp-cpp on the LilyGO TTGO VGA32 v1.4 (ESP32-WROVER, 4 MB PSRAM):
//   VGA       FabGL VGAController, 320x240 60 Hz, 64 colours (R 22/21, G 19/18,
//             B 5/4, HSync 23, VSync 15)
//   keyboard  PS/2 port 0 (CLK 33, DATA 32), mouse PS/2 port 1 (CLK 26, DATA 27)
//   sound     built-in DAC on GPIO25 (I2S0), 22050 Hz mono
//   files     micro-SD (MISO 2, MOSI 12, CLK 14, CS 13) mounted on /sd
// The emulator is the normal dsp code driven by ports/common (menu, frame loop).
#include <Arduino.h>

#include <driver/i2s.h>
#include <esp_vfs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "fabgl.h"
#include "src/gen/dsp_port_config.h"

#include "memfs.h"
#include "port.h"

// The emulator needs more than the 8 KB Arduino gives loop().
SET_LOOP_TASK_STACK_SIZE(DSP_LOOP_STACK_BYTES);

namespace {

fabgl::VGAController g_vga;
fabgl::PS2Controller g_ps2;

constexpr int kWidth = 320, kHeight = 240;
constexpr int kAudioRate = 22050;

// --- sound: ring buffer -> task on core 0 -> I2S0 built-in DAC -----------------

constexpr uint32_t kRingSize = 8192;  // samples, power of two
int16_t g_ring[kRingSize];
volatile uint32_t g_ring_read = 0, g_ring_write = 0;

void audio_task(void*) {
    uint16_t frames[256 * 2];
    int16_t last = 0;
    for (;;) {
        uint32_t r = g_ring_read;
        const uint32_t w = g_ring_write;
        for (int i = 0; i < 256; ++i) {
            if (r != w) last = g_ring[r++ & (kRingSize - 1)];
            // Built-in DAC: unsigned, top byte used; same value on both slots.
            const uint16_t v = uint16_t(int32_t(last) + 32768);
            frames[2 * i] = v;
            frames[2 * i + 1] = v;
        }
        g_ring_read = r;
        size_t written = 0;
        i2s_write(I2S_NUM_0, frames, sizeof frames, &written, portMAX_DELAY);
    }
}

void audio_begin() {
    i2s_config_t cfg = {};
    cfg.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    cfg.sample_rate = kAudioRate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    cfg.intr_alloc_flags = 0;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;  // the APLL belongs to the VGA output
    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, nullptr);
    i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);  // GPIO25
    xTaskCreatePinnedToCore(audio_task, "dsp-audio", 3072, nullptr, 5, nullptr, 0);
}

// --- embedded media as a read-only VFS on /embedded -----------------------------

namespace memfs = dsp::port::memfs;

struct MemDir {
    DIR dir;  // must come first: the VFS layer fills it in
    int handle;
    struct dirent entry;
};

int vfs_open(const char* path, int flags, int) { return memfs::open(path, flags); }
ssize_t vfs_read(int fd, void* dst, size_t size) { return memfs::read(fd, dst, size); }
off_t vfs_lseek(int fd, off_t offset, int whence) { return off_t(memfs::lseek(fd, long(offset), whence)); }
int vfs_close(int fd) { return memfs::close(fd); }
int vfs_fstat(int fd, struct stat* st) { return memfs::fstat(fd, st); }
int vfs_stat(const char* path, struct stat* st) { return memfs::stat(path, st); }
DIR* vfs_opendir(const char* path) {
    const int h = memfs::opendir(path);
    if (h < 0) return nullptr;
    MemDir* d = new MemDir();
    d->handle = h;
    return &d->dir;
}
struct dirent* vfs_readdir(DIR* dir) {
    MemDir* d = reinterpret_cast<MemDir*>(dir);
    const char* name;
    bool is_dir;
    if (!memfs::readdir(d->handle, &name, &is_dir)) return nullptr;
    strlcpy(d->entry.d_name, name, sizeof d->entry.d_name);
    d->entry.d_type = is_dir ? DT_DIR : DT_REG;
    return &d->entry;
}
int vfs_closedir(DIR* dir) {
    MemDir* d = reinterpret_cast<MemDir*>(dir);
    memfs::closedir(d->handle);
    delete d;
    return 0;
}

bool mount_memfs(const char* prefix) {
    esp_vfs_t vfs = {};
    vfs.flags = ESP_VFS_FLAG_DEFAULT;
    vfs.open = vfs_open;
    vfs.read = vfs_read;
    vfs.lseek = vfs_lseek;
    vfs.close = vfs_close;
    vfs.fstat = vfs_fstat;
    vfs.stat = vfs_stat;
    vfs.opendir = vfs_opendir;
    vfs.readdir = vfs_readdir;
    vfs.closedir = vfs_closedir;
    return esp_vfs_register(prefix, &vfs, nullptr) == ESP_OK;
}

// --- keyboard -------------------------------------------------------------------

struct VkMap {
    fabgl::VirtualKey vk, shifted;
    dsp::Key key;
};
const VkMap kKeys[] = {
    {fabgl::VK_UP, fabgl::VK_KP_UP, dsp::Key::Up},
    {fabgl::VK_DOWN, fabgl::VK_KP_DOWN, dsp::Key::Down},
    {fabgl::VK_LEFT, fabgl::VK_KP_LEFT, dsp::Key::Left},
    {fabgl::VK_RIGHT, fabgl::VK_KP_RIGHT, dsp::Key::Right},
    {fabgl::VK_RETURN, fabgl::VK_KP_ENTER, dsp::Key::Enter},
    {fabgl::VK_SPACE, fabgl::VK_NONE, dsp::Key::Space},
    {fabgl::VK_LSHIFT, fabgl::VK_NONE, dsp::Key::LeftShift},
    {fabgl::VK_RSHIFT, fabgl::VK_NONE, dsp::Key::RightShift},
    {fabgl::VK_LCTRL, fabgl::VK_NONE, dsp::Key::LeftCtrl},
    {fabgl::VK_RCTRL, fabgl::VK_NONE, dsp::Key::RightCtrl},
    {fabgl::VK_RALT, fabgl::VK_NONE, dsp::Key::RightAlt},
    {fabgl::VK_LGUI, fabgl::VK_NONE, dsp::Key::LeftGui},
    {fabgl::VK_RGUI, fabgl::VK_NONE, dsp::Key::RightGui},
    {fabgl::VK_BACKSPACE, fabgl::VK_NONE, dsp::Key::Backspace},
    {fabgl::VK_ESCAPE, fabgl::VK_NONE, dsp::Key::Escape},
    {fabgl::VK_TAB, fabgl::VK_NONE, dsp::Key::Tab},
    {fabgl::VK_CAPSLOCK, fabgl::VK_NONE, dsp::Key::CapsLock},
    {fabgl::VK_HOME, fabgl::VK_KP_HOME, dsp::Key::Home},
    {fabgl::VK_DELETE, fabgl::VK_KP_DELETE, dsp::Key::Delete},
    {fabgl::VK_COMMA, fabgl::VK_LESS, dsp::Key::Comma},
    {fabgl::VK_PERIOD, fabgl::VK_GREATER, dsp::Key::Period},
    {fabgl::VK_SEMICOLON, fabgl::VK_COLON, dsp::Key::Semicolon},
    {fabgl::VK_QUOTE, fabgl::VK_QUOTEDBL, dsp::Key::Quote},
    {fabgl::VK_SLASH, fabgl::VK_QUESTION, dsp::Key::Slash},
    {fabgl::VK_MINUS, fabgl::VK_UNDERSCORE, dsp::Key::Minus},
    {fabgl::VK_EQUALS, fabgl::VK_PLUS, dsp::Key::Equals},
    {fabgl::VK_KP_PLUS, fabgl::VK_NONE, dsp::Key::Plus},
    {fabgl::VK_KP_MULTIPLY, fabgl::VK_NONE, dsp::Key::Asterisk},
    {fabgl::VK_BACKSLASH, fabgl::VK_VERTICALBAR, dsp::Key::Backslash},
    {fabgl::VK_GRAVEACCENT, fabgl::VK_TILDE, dsp::Key::Backquote},
    {fabgl::VK_LEFTBRACKET, fabgl::VK_LEFTBRACE, dsp::Key::At},
    {fabgl::VK_RIGHTBRACKET, fabgl::VK_RIGHTBRACE, dsp::Key::Asterisk},
    {fabgl::VK_1, fabgl::VK_EXCLAIM, dsp::Key::Num1},
    {fabgl::VK_2, fabgl::VK_AT, dsp::Key::Num2},
    {fabgl::VK_3, fabgl::VK_HASH, dsp::Key::Num3},
    {fabgl::VK_4, fabgl::VK_DOLLAR, dsp::Key::Num4},
    {fabgl::VK_5, fabgl::VK_PERCENT, dsp::Key::Num5},
    {fabgl::VK_6, fabgl::VK_CARET, dsp::Key::Num6},
    {fabgl::VK_7, fabgl::VK_AMPERSAND, dsp::Key::Num7},
    {fabgl::VK_8, fabgl::VK_ASTERISK, dsp::Key::Num8},
    {fabgl::VK_9, fabgl::VK_LEFTPAREN, dsp::Key::Num9},
    {fabgl::VK_0, fabgl::VK_RIGHTPAREN, dsp::Key::Num0},
};

class Vga32Hal : public dsp::port::Hal {
public:
    void begin() {
        for (int i = 0; i < 64; ++i)
            lut_[i] = g_vga.createRawPixel(RGB222((i >> 4) & 3, (i >> 2) & 3, i & 3));
        clear();
    }

    const char* board_name() const override { return "TTGO VGA32"; }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }

    void present(const uint32_t* argb, int width, int height, int display_width,
                 int display_height) override {
        if (map_.update(width, height, display_width, display_height, kWidth, kHeight)) clear();
        const uint16_t* xs = map_.xs.data();
        for (int y = 0; y < map_.dst_h; ++y) {
            const uint32_t* src = argb + size_t(map_.ys[size_t(y)]) * size_t(width);
            uint8_t* row = g_vga.getScanline(map_.dst_y + y);
            for (int x = 0; x < map_.dst_w; ++x) {
                const uint32_t p = src[xs[x]];
                VGA_PIXELINROW(row, map_.dst_x + x) =
                    lut_[((p >> 18) & 0x30) | ((p >> 12) & 0x0c) | ((p >> 6) & 0x03)];
            }
        }
    }

    int output_rate() const override { return kAudioRate; }
    void push_audio(const int16_t* samples, size_t count) override {
        uint32_t w = g_ring_write;
        for (size_t i = 0; i < count && w - g_ring_read < kRingSize; ++i) g_ring[w++ & (kRingSize - 1)] = samples[i];
        g_ring_write = w;
    }
    size_t audio_queued() const override { return size_t(g_ring_write - g_ring_read); }

    void poll(dsp::port::HostInput& in) override {
        in = dsp::port::HostInput();
        if (fabgl::Keyboard* kb = g_ps2.keyboard()) {
            if (kb->isKeyboardAvailable()) {
                for (int i = 0; i < 26; ++i) {
                    const bool down = kb->isVKDown(fabgl::VirtualKey(fabgl::VK_a + i)) ||
                                      kb->isVKDown(fabgl::VirtualKey(fabgl::VK_A + i));
                    in.keys[size_t(dsp::Key::A) + i] = down;
                }
                for (int i = 0; i < 12; ++i)
                    in.keys[size_t(dsp::Key::F1) + i] = kb->isVKDown(fabgl::VirtualKey(fabgl::VK_F1 + i));
                for (const VkMap& m : kKeys) {
                    if (kb->isVKDown(m.vk) || (m.shifted != fabgl::VK_NONE && kb->isVKDown(m.shifted)))
                        in.keys[size_t(m.key)] = true;
                }
                in.left_alt = kb->isVKDown(fabgl::VK_LALT);
            }
        }
        if (fabgl::Mouse* mouse = g_ps2.mouse()) {
            if (mouse->isMouseAvailable()) {
                in.mouse_connected = true;
                fabgl::MouseDelta d;
                while (mouse->deltaAvailable() && mouse->getNextDelta(&d, 0)) {
                    in.mouse_dx += d.deltaX;
                    in.mouse_dy -= d.deltaY;  // PS/2: up is positive
                    buttons_ = d.buttons;
                }
                in.mouse_left = buttons_.left;
                in.mouse_right = buttons_.right;
            }
        }
    }

    uint64_t micros() override { return uint64_t(esp_timer_get_time()); }
    void sleep_us(uint32_t us) override {
        if (us >= 2000) vTaskDelay(pdMS_TO_TICKS(us / 1000));
        else delayMicroseconds(us);
    }
    void idle() override { vTaskDelay(1); }

    std::string storage_root() override {
        if (!sd_mounted_)
            sd_mounted_ = fabgl::FileBrowser::mountSDCard(false, "/sd", 8, 16 * 1024, DSP_SD_MISO,
                                                          DSP_SD_MOSI, DSP_SD_CLK, DSP_SD_CS);
        return sd_mounted_ ? "/sd" : "";
    }
    bool mount_embedded(const DspEmbeddedFile* files, size_t count, std::string* dir,
                        std::string* error) override {
        memfs::mount(files, count, "/embedded");
        if (!mount_memfs("/embedded")) {
            *error = "cannot register the /embedded file system";
            return false;
        }
        *dir = "/embedded";
        return true;
    }
    void log(const char* text) override { Serial.println(text); }

private:
    void clear() {
        for (int y = 0; y < kHeight; ++y) {
            uint8_t* row = g_vga.getScanline(y);
            for (int x = 0; x < kWidth; ++x) VGA_PIXELINROW(row, x) = lut_[0];
        }
    }

    uint8_t lut_[64] = {};
    dsp::port::ScaleMap map_;
    fabgl::MouseButtons buttons_;
    bool sd_mounted_ = false;
};

Vga32Hal* g_hal = nullptr;

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.printf("dsp-cpp TTGO VGA32, PSRAM %u KB free\n", unsigned(ESP.getFreePsram() / 1024));
    g_ps2.begin(fabgl::PS2Preset::KeyboardPort0_MousePort1, fabgl::KbdMode::GenerateVirtualKeys);
    g_vga.begin();  // TTGO VGA32 pins are FabGL's defaults
    g_vga.setResolution(QVGA_320x240_60Hz);
    g_vga.enableBackgroundPrimitiveExecution(false);  // the frame buffer is written directly
    audio_begin();
    g_hal = new Vga32Hal();
    g_hal->begin();
}

void loop() {
    dsp::port::run(*g_hal);  // does not return
}
