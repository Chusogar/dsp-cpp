// See esp32_support.h. Compiled only into the ESP32 ports.
#include "esp32_support.h"

#include <dirent.h>
#include <driver/i2s.h>
#include <esp_vfs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/stat.h>

#include <cstring>

#include "memfs.h"

namespace dsp {
namespace port {
namespace esp32 {

namespace {

// --- sound ------------------------------------------------------------------------

constexpr uint32_t kRingSize = 8192;  // samples, power of two
int16_t g_ring[kRingSize];
volatile uint32_t g_read = 0, g_write = 0;
bool g_dac2 = false;

void audio_task(void*) {
    uint16_t frames[256 * 2];
    int16_t last = 0;
    for (;;) {
        uint32_t r = g_read;
        const uint32_t w = g_write;
        for (int i = 0; i < 256; ++i) {
            if (r != w) last = g_ring[r++ & (kRingSize - 1)];
            // Built-in DAC: unsigned, top byte used; same value on both slots.
            const uint16_t v = uint16_t(int32_t(last) + 32768);
            frames[2 * i] = v;
            frames[2 * i + 1] = v;
        }
        g_read = r;
        size_t written = 0;
        i2s_write(I2S_NUM_0, frames, sizeof frames, &written, portMAX_DELAY);
    }
}

// --- embedded media VFS --------------------------------------------------------------

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
    std::strncpy(d->entry.d_name, name, sizeof d->entry.d_name - 1);
    d->entry.d_name[sizeof d->entry.d_name - 1] = '\0';
    d->entry.d_type = is_dir ? DT_DIR : DT_REG;
    return &d->entry;
}
int vfs_closedir(DIR* dir) {
    MemDir* d = reinterpret_cast<MemDir*>(dir);
    memfs::closedir(d->handle);
    delete d;
    return 0;
}

}  // namespace

void audio_begin(int rate, int dac_gpio) {
    g_dac2 = dac_gpio == 26;
    i2s_config_t cfg = {};
    cfg.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    cfg.sample_rate = rate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    cfg.intr_alloc_flags = 0;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;  // the APLL belongs to FabGL's VGA output on the TTGO
    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, nullptr);
    i2s_set_dac_mode(g_dac2 ? I2S_DAC_CHANNEL_LEFT_EN : I2S_DAC_CHANNEL_RIGHT_EN);
    xTaskCreatePinnedToCore(audio_task, "dsp-audio", 3072, nullptr, 5, nullptr, 0);
}

void audio_push(const int16_t* samples, size_t count) {
    uint32_t w = g_write;
    for (size_t i = 0; i < count && w - g_read < kRingSize; ++i) g_ring[w++ & (kRingSize - 1)] = samples[i];
    g_write = w;
}

size_t audio_queued() { return size_t(g_write - g_read); }

bool mount_embedded(const DspEmbeddedFile* files, size_t count, const char* prefix) {
    memfs::mount(files, count, prefix);
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

}  // namespace esp32
}  // namespace port
}  // namespace dsp
