#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "machine/mos6526.h"
#include "sound/sid.h"
#include "video/mos6566.h"

namespace dsp {

// Commodore 64 (PAL), ported from commodore64.pas + mos6566.pas +
// mos6526_old.pas + sid_sound.pas.
class C64 : public Machine {
public:
    static constexpr uint32_t kClock = 985248;
    static constexpr int kScanlines = 312;
    static constexpr int kCyclesPerLine = 63;
    static constexpr int kScreenWidth = 384;
    static constexpr int kScreenHeight = 270;
    static constexpr double kFramesPerSecond = double(kClock) / (kScanlines * kCyclesPerLine);
    static constexpr int kSampleRate = Sid::kSampleRate;

    C64();

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

    const char* title() const override { return "Commodore 64"; }
    bool uses_keyboard() const override { return true; }

    bool load_media(const std::string& path, std::string* error) override;
    void tape_toggle_play() override;
    bool tape_loaded() const override { return tape_loaded_; }

    // Debug / test helpers (same API the existing C64 debug tools expect).
    void poke(uint16_t address, uint8_t value) { write_byte(address, value); }
    uint8_t peek(uint16_t address) { return read_byte(address); }
    void set_pc(uint16_t value) { cpu_.set_pc(value); }
    uint16_t pc() const { return cpu_.pc(); }

    // Unit tests that do not ship copyrighted KERNAL/BASIC/CHAR ROMs.
    void init_synthetic_roms();

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    void update_pla();
    void on_cpu_cycles(int cycles);
    uint8_t cia1_pa_read();
    uint8_t cia1_pb_read();
    void cia2_pa_write(uint8_t value);
    void apply_keyboard(const MachineInputs& inputs);
    bool load_prg(const uint8_t* data, size_t size, std::string* error);
    bool load_t64(const uint8_t* data, size_t size, std::string* error);
    bool load_tap(const uint8_t* data, size_t size, std::string* error);
    bool load_d64(const uint8_t* data, size_t size, std::string* error);
    void advance_tape(int cycles);
    void inject_prg_payload(uint16_t address, const uint8_t* data, size_t size);

    M6502 cpu_;
    Mos6566 vic_;
    Mos6526 cia1_;
    Mos6526 cia2_;
    Sid sid_;

    std::array<uint8_t, 0x10000> ram_{};
    std::array<uint8_t, 0x2000> kernal_{};
    std::array<uint8_t, 0x2000> basic_{};
    std::array<uint8_t, 0x1000> chargen_{};
    std::array<uint8_t, 0x400> color_ram_{};
    std::array<uint8_t, 8> keyboard_{};
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};

    uint8_t port_bits_ = 0xef;
    uint8_t port_val_ = 0xef;
    uint8_t tape_control_ = 0x10;
    bool write_ram_ = false;
    bool read_ram_a_ = false;
    bool read_ram_e_ = false;
    uint8_t read_ram_d_ = 2;
    bool tape_motor_ = false;
    bool cia_irq_ = false;
    bool vic_irq_ = false;
    bool cia_nmi_ = false;
    bool shift_lock_ = false;
    bool caps_held_ = false;
    bool restore_held_ = false;

    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;

    bool tape_loaded_ = false;
    bool tape_playing_ = false;
    std::vector<uint8_t> tape_data_;
    size_t tape_pos_ = 0;
    int tape_cycles_left_ = 0;
    uint8_t tape_level_ = 0;
    uint8_t tape_version_ = 0;
};

}  // namespace dsp
