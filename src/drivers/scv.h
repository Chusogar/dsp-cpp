#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/upd7801.h"
#include "sound/upd1771.h"

namespace dsp {

// Epoch Super Cassette Vision, ported from
// leniad/dsp-emulator `src/consolas/super_cassette_vision.pas`.
//
// CPU: µPD7801 @ 4 MHz crystal (internally /2)   Sound: µPD1771C @ 6 MHz
// Display: 192×222 cropped from an internal 256×256 plane at (24, 23).
class Scv : public Machine {
public:
    static constexpr int kScreenWidth = 192;
    static constexpr int kScreenHeight = 222;
    static constexpr uint32_t kCrystal = 4000000;
    static constexpr uint32_t kCpuClock = kCrystal / 2;
    static constexpr int kScanlines = 262;
    static constexpr double kFramesPerSecond = 59.922745;
    static constexpr int kSampleRate = Upd1771::kSampleRate;
    static constexpr int kCyclesPerLine =
        int(double(kCpuClock) / (kFramesPerSecond * double(kScanlines)) + 0.5);
    static constexpr size_t kMaxCartridge = 0x20000;

    Scv();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return "Super Cassette Vision"; }
    bool uses_keyboard() const override { return true; }

    bool load_media(const std::string& path, std::string* error) override;
    bool bios_loaded() const { return bios_loaded_; }
    uint16_t debug_pc() const { return cpu_.pc(); }
    uint8_t debug_a() const { return cpu_.a(); }

private:
    enum class Mapper { Standard, PolePosition2 };

    uint8_t read_mem(uint16_t addr);
    void write_mem(uint16_t addr, uint8_t value);
    uint8_t port_b_in(uint8_t mask);
    uint8_t port_c_in(uint8_t mask);
    void port_a_out(uint8_t value);
    void port_c_out(uint8_t value);
    void on_cycles(int cycles);

    bool search_bios(const std::string& rom_path, std::string* error);
    bool install_bios(const std::vector<uint8_t>& bios, const std::vector<uint8_t>& chr,
                      std::string* error);
    bool load_cart_bytes(std::vector<uint8_t> data, std::string* error);
    void apply_cart_crc(uint32_t crc);
    bool load_split_companion(const std::string& path, std::vector<uint8_t>& data,
                              std::string* error);

    void update_video();
    void draw_text(int x, int y, uint16_t char_data, uint8_t fg, uint8_t bg);
    void draw_semi_graph(int x, int y, uint8_t data, uint8_t fg);
    void draw_block_graph(int x, int y, uint8_t col);
    void plot_sprite_part(int x, int y, uint8_t pat, uint8_t col, int start_line);
    void draw_sprite(int x, int y, uint8_t tile_idx, uint8_t col, bool left, bool right,
                     bool top, bool bottom, uint8_t clip_y, int start_line);
    void put_pixel(int x, int y, uint32_t color);

    static constexpr uint32_t kPalette[16] = {
        0xFF00009B, 0xFF000000, 0xFF0000FF, 0xFFA100FF, 0xFF00FF00, 0xFFA0FF9D,
        0xFF00FFFF, 0xFF00A100, 0xFFFF0000, 0xFFFFA100, 0xFFFF00FF, 0xFFFFA09F,
        0xFFFFFF00, 0xFFA3A000, 0xFFA1A09D, 0xFFFFFFFF,
    };
    static constexpr uint8_t kSpr2ColLut0[16] = {0, 15, 12, 13, 10, 11, 8, 9,
                                                 6, 7, 4, 5, 2, 3, 1, 1};
    static constexpr uint8_t kSpr2ColLut1[16] = {0, 1, 8, 11, 2, 3, 10, 9,
                                                 4, 5, 12, 13, 6, 7, 14, 15};

    Upd7801 cpu_;
    Upd1771 sound_;

    std::array<uint8_t, 0x10000> mem_{};
    std::array<uint8_t, 0x400> chars_{};
    std::array<std::array<uint8_t, 0x8000>, 4> rom_{};
    uint8_t rom_window_ = 0;
    uint8_t rom_bank_type_ = 0;
    bool ram_bank_ = false;
    bool ram_bank2_ = false;
    Mapper mapper_ = Mapper::Standard;
    bool bios_loaded_ = false;

    uint8_t porta_val_ = 0xFF;
    uint8_t portc_val_ = 0xFF;
    std::array<uint8_t, 9> keys_{};

    std::array<uint32_t, 256 * 256> plane_{};
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
