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
#include "machine/i8255.h"
#include "sound/sega_pcm.h"
#include "sound/ym2151.h"
#include "sound/ym2203.h"
#include "video/sega16.h"

namespace dsp {

// Hang-On, Enduro Racer and Space Harrier, ported from hangon_hw.pas.
class HangOn : public Machine {
public:
    enum class Game { HangOn, Enduro, Sharrier };

    static constexpr int kScreenWidth = 320;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 262;

    explicit HangOn(Game game = Game::HangOn);

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
    int sample_rate() const override { return YM2151::kSampleRate; }

    const char* title() const override;

    uint32_t debug_pc() const { return main_cpu_.pc(); }
    uint16_t debug_sound_pc() const { return sound_cpu_.pc(); }
    uint8_t debug_sound_latch() const { return sound_latch_; }
    bool debug_z80_reset() const { return z80_reset_; }
    uint32_t debug_ppi_a_writes() const { return ppi_a_writes_; }
    uint16_t debug_mcu_pc() const { return mcu_ ? mcu_->pc() : uint16_t(0); }
    uint32_t debug_mcu_irqs() const { return mcu_irqs_; }

private:
    bool is_sharrier_map() const { return game_ != Game::HangOn; }

    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    uint16_t hangon_read(uint32_t address);
    void hangon_write(uint32_t address, uint16_t value);
    uint16_t sharrier_read(uint32_t address);
    void sharrier_write(uint32_t address, uint16_t value);
    uint16_t sub_read(uint32_t address);
    void sub_write(uint32_t address, uint16_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);
    void on_sound_cycles(int cycles);
    void update_video();
    void update_controls();
    bool load_roms(const std::string& rom_path, std::string* error);

    Game game_;
    uint32_t main_clock_ = 25174800 / 4;
    uint32_t sound_clock_ = 4000000;
    int cpu_sync_ = 2;

    M68000 main_cpu_;
    M68000 sub_cpu_;
    Z80 sound_cpu_;
    YM2203 ym2203_;
    YM2151 ym2151_;
    SegaPcm pcm_;
    I8255 ppi0_;
    I8255 ppi1_;
    std::unique_ptr<Mcs51> mcu_;
    Sega16Video video_;

    std::vector<uint16_t> rom_;
    std::vector<uint16_t> rom_data_;
    std::vector<uint16_t> rom2_;
    std::array<uint16_t, 0x2000> ram_{};
    std::array<uint16_t, 0x2000> ram2_{};
    std::array<uint16_t, 0x800> road_ram_{};
    std::vector<uint16_t> sprite_rom_;
    std::vector<uint32_t> sprite_rom32_;
    std::vector<uint8_t> zoom_;
    std::vector<uint8_t> pcm_rom_;
    std::vector<uint8_t> road_gfx_;
    std::array<uint8_t, 0x10000> sound_mem_{};

    std::vector<uint32_t> framebuffer_;
    std::vector<uint32_t> bg_, fg_, text_low_, text_high_;

    uint8_t adc_select_ = 0;
    uint8_t sound_latch_ = 0;
    uint32_t ppi_a_writes_ = 0;
    uint8_t control_res_ = 0;
    uint8_t analog_x_ = 0x80;
    uint8_t analog_y_ = 0x80;
    uint8_t analog_gas_ = 0;
    uint8_t analog_brake_ = 0;
    uint8_t analog_moto_ = 0;
    uint16_t in0_ = 0xffff;
    uint16_t dsw_a_ = 0xffff;
    uint16_t dsw_b_ = 0xfffe;
    bool z80_reset_ = false;
    int sprite_banks_ = 7;
    uint8_t i8751_addr_ = 0;
    uint32_t mcu_irqs_ = 0;
    bool use_fd1089_ = false;
    bool use_ym2151_ = false;
    bool sharrier_road_ = false;

    int64_t audio_acc_ = 0;
    int64_t pcm_acc_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
