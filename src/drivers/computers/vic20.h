#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "machine/via6522.h"
#include "sound/mos6560.h"

namespace dsp {

// Commodore VIC-20 (PAL 6561 preferred; NTSC 6560 fallback).
class Vic20 : public Machine {
public:
    enum class Region { Pal, Ntsc };

    explicit Vic20(Region region = Region::Pal);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override { (void)bank; (void)value; }

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return vic_.vis_width(); }
    int screen_height() const override { return vic_.vis_height(); }
    double frames_per_second() const override { return frames_per_second_; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return Mos6560::kSampleRate; }

    const char* title() const override {
        return region_ == Region::Pal ? "Commodore VIC-20 (PAL)" : "Commodore VIC-20 (NTSC)";
    }
    bool uses_keyboard() const override { return true; }

    bool load_media(const std::string& path, std::string* error) override;

    // Direct RAM poke helpers for tests / PRG injection diagnostics.
    uint8_t debug_read(uint16_t addr) { return read_byte(addr); }
    bool prg_pending() const { return !pending_prg_.empty(); }

private:
    // PAL frames to wait before dropping a PRG into RAM (BASIC cold start).
    static constexpr int kPrgInjectFrames = 120;

    uint8_t read_byte(uint16_t addr);
    void write_byte(uint16_t addr, uint8_t value);
    uint8_t vic_videoram_r(uint16_t offset);
    uint8_t via1_pa_r();
    uint8_t via2_pa_r();
    uint8_t via2_pb_r();
    void via2_pa_w(uint8_t data);
    void via2_pb_w(uint8_t data);
    void on_cycles(int cycles);
    void update_irq();
    void update_nmi();
    bool load_roms(const std::string& path, std::string* error);
    bool queue_prg(const std::vector<uint8_t>& data, std::string* error);
    void update_pending_prg();
    void inject_prg(const std::vector<uint8_t>& data);
    bool load_cart(const std::vector<uint8_t>& data, std::string* error);

    Region region_;
    double frames_per_second_ = 50.0;

    M6502 cpu_;
    Mos6560 vic_;
    Via6522 via1_;
    Via6522 via2_;

    // Unexpanded: 1K at $0000 + 4K at $1000.
    std::array<uint8_t, 0x400> ram0_{};
    std::array<uint8_t, 0x1000> ram_main_{};
    std::array<uint8_t, 0x400> color_ram_{};
    std::array<uint8_t, 0x2000> basic_rom_{};
    std::array<uint8_t, 0x2000> kernal_rom_{};
    std::array<uint8_t, 0x1000> char_rom_{};
    std::array<uint8_t, 0x2000> cart_blk5_{};  // optional cartridge at $A000
    size_t cart_size_ = 0;

    std::array<uint8_t, 8> keyboard_{};  // columns, active-low rows
    uint8_t key_row_ = 0xff;
    uint8_t key_col_ = 0xff;
    uint8_t joy_ = 0xff;  // active-low: up/down/left/right/fire

    bool via1_nmi_ = false;
    bool via2_irq_ = false;

    std::vector<uint8_t> pending_prg_;
    int boot_frames_ = 0;

    int cpu_cycle_debt_ = 0;
    int64_t audio_acc_ = 0;
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
