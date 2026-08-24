#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/z80.h"
#include "sound/ym2610.h"
#include "video/neogeo_video.h"

namespace dsp {

// SNK NeoGeo MVS (arcade): 12 MHz 68000, 4 MHz Z80, YM2610, LSPC2-A2 sprites
// and fix layer. Loads a MAME split cart plus `neogeo.zip` (BIOS, SFIX, SM1, LO).
class NeoGeo : public Machine {
public:
    static constexpr uint32_t kMainClock = 12000000;
    static constexpr uint32_t kZ80Clock = 4000000;
    static constexpr uint32_t kYmClock = 8000000;
    static constexpr double kFramesPerSecond = 59.185606;
    static constexpr int kScanlines = NeoGeoVideo::kScanlines;
    static constexpr int kSampleRate = YM2610::kSampleRate;

    explicit NeoGeo(std::string game_name = "neogeo");

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return NeoGeoVideo::kScreenWidth; }
    int screen_height() const override { return NeoGeoVideo::kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return title_.c_str(); }

    // Test hook: install already-decoded images (program ROM is 68k big-endian).
    bool load_synthetic(std::vector<uint8_t> bios, std::vector<uint8_t> sfix, std::vector<uint8_t> sm1,
                        std::vector<uint8_t> p_rom, std::vector<uint8_t> s_rom,
                        std::vector<uint8_t> m_rom, std::vector<uint8_t> c_rom,
                        std::vector<uint8_t> v_rom, std::string* error);

    uint32_t debug_pc() const { return m68k_.pc(); }
    uint16_t debug_read_word(uint32_t address) { return read_word(address); }
    void debug_write_word(uint32_t address, uint16_t value) { write_word(address, value); }
    NeoGeoVideo& video() { return video_; }
    YM2610& ym() { return ym_; }

    static bool is_game_name(const std::string& name);

private:
    uint16_t read_word(uint32_t address);
    void write_word(uint32_t address, uint16_t value);
    uint8_t read_byte(uint32_t address);
    void write_byte(uint32_t address, uint8_t value);

    uint8_t z80_read(uint16_t address);
    void z80_write(uint16_t address, uint8_t value);
    uint8_t z80_in(uint16_t port);
    void z80_out(uint16_t port, uint8_t value);

    uint8_t p1_inputs() const;
    uint8_t p2_inputs() const;
    uint8_t status_a() const;
    uint8_t status_b() const;

    void on_m68k_cycles(int cycles);
    void update_irqs();
    void kick_watchdog();
    void rtc_write(uint8_t value);

    bool load_roms(const std::string& rom_path, std::string* error);
    void finish_load(bool byteswap_program);
    const uint8_t* z80_rom() const;

    std::string game_name_;
    std::string title_ = "NeoGeo";

    M68000 m68k_{kMainClock};
    Z80 z80_{kZ80Clock};
    YM2610 ym_{kYmClock, 1.2f};
    NeoGeoVideo video_;

    std::vector<uint8_t> bios_;
    std::vector<uint8_t> sfix_;
    std::vector<uint8_t> sm1_;
    std::vector<uint8_t> lo_;
    std::vector<uint8_t> p_rom_;
    std::vector<uint8_t> s_rom_;
    std::vector<uint8_t> m_rom_;
    std::vector<uint8_t> c_rom_;
    std::vector<uint8_t> v_rom_;

    std::array<uint8_t, 0x10000> ram_{};
    std::array<uint8_t, 0x10000> sram_{};
    std::array<uint8_t, 0x2000> z80_ram_{};
    std::array<uint8_t, 0x800> memcard_{};

    uint32_t cart_bank_ = 0;
    bool bios_present_ = false;
    bool bios_vectors_ = true;
    bool sram_unlocked_ = false;
    bool sound_nmi_enabled_ = false;
    uint8_t sound_latch_ = 0;
    uint8_t sound_reply_ = 0;
    uint8_t z80_bank_ = 0;
    uint8_t dsw_ = 0xff;
    int watchdog_ = 0;

    MachineInputs inputs_{};
    bool service_ = false;

    uint8_t rtc_shift_ = 0;
    uint8_t rtc_command_ = 0;
    int rtc_bits_ = 0;
    uint8_t rtc_ctrl_ = 0;
    bool rtc_pulse_ = false;

    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
    int64_t audio_accumulator_ = 0;
};

}  // namespace dsp
