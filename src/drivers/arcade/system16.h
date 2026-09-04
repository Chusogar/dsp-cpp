#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/mcs48.h"
#include "cpu/mcs51.h"
#include "cpu/z80.h"
#include "machine/i8255.h"
#include "machine/sega_315_5195.h"
#include "sound/dac.h"
#include "sound/upd7759.h"
#include "sound/ym2151.h"
#include "video/sega16.h"

namespace dsp {

// Sega System 16A/16B, ported from system16a_hw.pas and system16b_hw.pas.
class System16 : public Machine {
public:
    enum class Game { Fantzone, Shinobi, Tetris, Altbeast, Alexkidd, Aliensyn, Wb3 };

    static constexpr int kScreenWidth = 320;
    static constexpr int kScreenHeight = 224;
    static constexpr int kScanlines = 262;
    static constexpr int kCpuSync = 4;
    static constexpr uint32_t kMainClock = 10000000;

    explicit System16(Game game);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return fps_; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return YM2151::kSampleRate; }

    const char* title() const override;

    uint32_t debug_pc() const { return main_cpu_.pc(); }

private:
    bool is_16b() const { return game_ == Game::Altbeast; }
    bool uses_n7751() const {
        return game_ == Game::Shinobi || game_ == Game::Alexkidd || game_ == Game::Aliensyn;
    }
    bool uses_fd1089() const { return game_ == Game::Aliensyn || game_ == Game::Wb3; }

    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint16_t read_16a(uint32_t address);
    void write_16a(uint32_t address, uint16_t value);
    uint16_t read_16b(uint32_t address);
    void write_16b(uint32_t address, uint16_t value, bool allow_mapper);
    uint16_t io_16a(uint16_t address);
    uint16_t io_16b(uint16_t address);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);
    void on_sound_cycles(int cycles);
    void update_video();
    bool load_roms(const std::string& rom_path, std::string* error);
    void region2_write(uint32_t address, uint16_t value);
    void n7751_rom_offset_w(int port, uint8_t value);
    uint8_t n7751_in(uint16_t port);
    void n7751_out(uint16_t port, uint8_t value);

    Game game_;
    double fps_ = 60.0;
    uint32_t sound_clock_ = 4000000;

    M68000 main_cpu_;
    Z80 sound_cpu_;
    YM2151 ym_;
    I8255 ppi_;
    Sega3155195 mapper_;
    std::unique_ptr<Mcs51> mcu_;
    std::unique_ptr<Mcs48> n7751_;
    std::unique_ptr<Upd7759> upd_;
    Dac dac_;
    Sega16Video video_;

    std::vector<uint16_t> rom_;
    std::vector<uint16_t> rom_data_;
    std::vector<uint16_t> sprite_rom_;
    std::array<uint16_t, 0x10000> ram_{};
    std::array<uint8_t, 0x10000> sound_mem_{};
    std::array<std::array<uint8_t, 0x4000>, 16> sound_bank_{};
    std::vector<uint8_t> n7751_data_;

    std::vector<uint32_t> framebuffer_;
    std::vector<uint32_t> bg_low_, bg_high_, fg_low_, fg_high_, text_low_, text_high_;

    uint16_t in0_ = 0xffff;
    uint16_t in1_ = 0xffff;
    uint16_t in2_ = 0xffff;
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0xfc;
    uint8_t sound_latch_ = 0;
    uint8_t sound_bank_num_ = 0;
    int sprite_banks_ = 4;
    int tile_n_ = 1;
    bool use_mcu_ = false;
    bool use_fd1089_ = false;
    uint8_t n7751_numroms_ = 0;
    uint8_t n7751_command_ = 0;
    uint32_t n7751_rom_address_ = 0;

    int64_t audio_acc_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
