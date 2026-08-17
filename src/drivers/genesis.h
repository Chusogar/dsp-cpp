#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/z80.h"
#include "sound/ym2612.h"
#include "video/sega_315_5313.h"

namespace dsp {

// Sega Genesis / Mega Drive. Completes the WIP genesis.pas driver: 68000 +
// Z80, the 315-5313 VDP (planes, window, sprites, DMA), YM2612 + SN76489,
// 3-button pads and cartridge loading (.bin/.md/.gen/.smd, plain or zipped).
class Genesis : public Machine {
public:
    static constexpr uint32_t kMasterNtsc = 53693175;
    static constexpr uint32_t kMasterPal = 53203424;
    static constexpr uint32_t kM68kClockNtsc = kMasterNtsc / 7;
    static constexpr uint32_t kM68kClockPal = kMasterPal / 7;
    static constexpr uint32_t kZ80ClockNtsc = kMasterNtsc / 15;
    static constexpr uint32_t kZ80ClockPal = kMasterPal / 15;
    static constexpr double kFpsNtsc = 59.922743;
    static constexpr double kFpsPal = 49.701460;
    static constexpr int kMaxRom = 0x800000;
    static constexpr int kScreenWidth = 320;
    static constexpr int kSampleRate = YM2612::kSampleRate;

    enum class Region { Japan, Usa, Europe };

    explicit Genesis(Region region = Region::Usa);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return vdp_.screen_height(); }
    double frames_per_second() const override { return pal_ ? kFpsPal : kFpsNtsc; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return pal_ ? "Sega Mega Drive" : "Sega Genesis"; }

    bool load_media(const std::string& path, std::string* error) override;
    bool load_rom(std::vector<uint8_t> data, std::string* error);

    // Test / debug hooks.
    uint32_t debug_pc() const { return m68k_.pc(); }
    uint16_t debug_read_word(uint32_t address) { return read_word(address); }
    void debug_write_word(uint32_t address, uint16_t value) { write_word(address, value); }
    Sega3155313& vdp() { return vdp_; }
    YM2612& ym() { return ym_; }
    bool debug_z80_has_bus() const { return z80_has_bus_; }
    bool debug_z80_reset() const { return z80_is_reset_; }
    uint8_t debug_io(int index) const { return io_data_[size_t(index) & 0xf]; }

private:
    uint16_t read_word(uint32_t address);
    void write_word(uint32_t address, uint16_t value);
    uint8_t read_byte(uint32_t address);
    void write_byte(uint32_t address, uint8_t value);

    uint8_t z80_read(uint16_t address);
    void z80_write(uint16_t address, uint8_t value);

    uint8_t read_io(uint32_t address);
    void write_io(uint32_t address, uint8_t value);
    uint8_t read_pad(int port) const;

    uint8_t cart_read(uint32_t address) const;
    void cart_write(uint32_t address, uint8_t value);

    void check_z80_bus_reset();
    void on_m68k_cycles(int cycles);
    bool load_cartridge(std::vector<uint8_t> data, std::string* error);
    void parse_header();
    uint32_t m68k_clock() const { return pal_ ? kM68kClockPal : kM68kClockNtsc; }
    uint32_t z80_clock() const { return pal_ ? kZ80ClockPal : kZ80ClockNtsc; }

    Region region_;
    bool pal_ = false;

    M68000 m68k_;
    Z80 z80_;
    Sega3155313 vdp_;
    YM2612 ym_;

    std::vector<uint8_t> rom_;
    uint32_t rom_mask_ = 0;
    std::array<uint8_t, 0x10000> ram_{};
    std::array<uint8_t, 0x2000> z80_ram_{};
    std::array<uint8_t, 0x10000> sram_{};
    uint32_t sram_start_ = 0x200000;
    uint32_t sram_end_ = 0x20ffff;
    bool sram_present_ = false;
    bool sram_enabled_ = true;

    std::array<uint32_t, 8> rom_banks_{};
    bool ssf2_mapper_ = false;

    std::array<uint8_t, 16> io_data_{};
    std::array<uint8_t, 16> io_ctrl_{};
    uint8_t version_reg_ = 0xa1;

    bool z80_has_bus_ = true;
    bool z80_is_reset_ = true;
    uint16_t z80_bank_ = 0;
    int z80_bank_shift_ = 0;

    MachineInputs pads_{};

    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
    int64_t audio_accumulator_ = 0;
    int cycles_on_line_ = 0;
};

}  // namespace dsp
