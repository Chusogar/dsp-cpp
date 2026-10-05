#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/mcs51.h"
#include "cpu/z80.h"
#include "machine/fd1094.h"
#include "machine/sega_315_5195.h"
#include "machine/sega_315_5296.h"
#include "sound/rf5c68.h"
#include "sound/ym2612.h"
#include "video/sega16.h"
#include "video/sega_315_5313.h"

namespace dsp {

// Sega System 18. Hardware is a System 16B tilemap/sprite board plus a Genesis
// VDP, dual YM3438 (YM2612), RF5C68 PCM and (on Moonwalker) an I8751 MCU.
class System18 : public Machine {
public:
    enum class Game {
        Astorm,
        Bloxeed,
        Cltchitr,
        Ddcrew,
        Desertbr,
        Hamaway,
        Lghost,
        Mwalk,
        Pontoon,
        Shdancer,
        Wwallyj,
    };

    enum class RomBoard {
        Shadow,     // Shadow Dancer — VDP on mapper region 1
        Board5874,  // 171-5874 — VDP on region 2, extra ROM window on region 1
        Board5987,  // 171-5987 — VDP on region 2, bank writes on region 1
        Board8377525,  // Hammer Away proto
    };

    static constexpr int kNativeWidth = 320;
    static constexpr int kNativeHeight = 224;
    static constexpr int kScanlines = 262;
    static constexpr int kCpuSync = 4;
    static constexpr uint32_t kMainClock = 10000000;
    static constexpr uint32_t kSoundClock = 8000000;
    static constexpr uint32_t kMcuClock = 8000000;
    static constexpr double kMixGain = 1.5;

    explicit System18(Game game = Game::Mwalk);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override {
        return rotated_ ? rotated_fb_.data() : framebuffer_.data();
    }
    int screen_width() const override { return rotated_ ? kNativeHeight : kNativeWidth; }
    int screen_height() const override { return rotated_ ? kNativeWidth : kNativeHeight; }
    double frames_per_second() const override { return fps_; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return YM2612::kSampleRate; }

    const char* title() const override;

    uint32_t debug_pc() const { return main_cpu_.pc(); }
    uint16_t debug_sound_pc() const { return sound_cpu_.pc(); }
    uint16_t debug_mcu_pc() const { return mcu_ ? mcu_->pc() : 0; }

private:
    bool load_roms(const std::string& rom_path, std::string* error);
    void configure_game();
    void update_video();
    void overlay_vdp(int priority_layer);
    void rotate_framebuffer();

    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value, bool allow_mapper);
    uint16_t read_rom_word(uint32_t byte_offset);
    uint16_t misc_io_r(uint16_t word_offset);
    void misc_io_w(uint16_t word_offset, uint16_t value);
    void bank5987_w(uint16_t offset, uint16_t value);
    void bank837_w(uint16_t offset, uint16_t value);

    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);
    void on_sound_cycles(int cycles);

    void apply_tile_bank_5874(uint8_t data);

    Game game_;
    RomBoard rom_board_ = RomBoard::Board5874;
    bool use_fd1094_ = false;
    bool use_mcu_ = false;
    bool rotated_ = false;  // true for ROT90/ROT270
    bool rot90_ = false;    // false = ROT270, true = ROT90
    double fps_ = 57.23;
    int tile_n_ = 8;
    int sprite_banks_ = 16;
    uint32_t rom0_size_ = 0x80000;
    uint32_t rom1_offset_ = 0x80000;

    M68000 main_cpu_;
    Z80 sound_cpu_;
    std::unique_ptr<Mcs51> mcu_;
    Sega3155195 mapper_;
    Sega3155296 io_;
    Fd1094 fd1094_;
    YM2612 ym1_;
    YM2612 ym2_;
    Rf5c68 rf5c68_;
    Sega3155313 vdp_;
    Sega16Video video_;

    std::vector<uint16_t> rom_;
    std::vector<uint16_t> sprite_rom_;
    std::vector<uint8_t> sound_rom_;
    std::array<uint8_t, 0x2000> sound_ram_{};
    std::array<uint16_t, 0x2000> work_ram_{};

    std::vector<uint32_t> framebuffer_;
    std::vector<uint32_t> rotated_fb_;
    std::vector<uint32_t> bg_low_, bg_high_, fg_low_, fg_high_, text_low_, text_high_;
    std::vector<uint32_t> vdp_fb_;
    std::vector<uint8_t> vdp_pri_;

    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;
    double main_debt_ = 0;
    double sound_debt_ = 0;
    double mcu_debt_ = 0;

    uint8_t sound_bank_ = 0;
    uint8_t tile_bank_latch_ = 0;
    uint8_t vdp_mixing_ = 0;
    bool vdp_enable_ = false;
    bool grayscale_ = false;

    uint8_t in_p1_ = 0xff;
    uint8_t in_p2_ = 0xff;
    uint8_t in_p3_ = 0xff;
    uint8_t in_service_ = 0xff;
    uint8_t dsw_coinage_ = 0xff;
    uint8_t dsw_ = 0xfd;  // demo sounds on, rest defaults
};

}  // namespace dsp
