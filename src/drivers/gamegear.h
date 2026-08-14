#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/sega_vdp.h"
#include "sound/sn76496.h"

namespace dsp {

// Sega Game Gear, ported from leniad/dsp-emulator src/consolas/sega_gg.pas.
//
// Shares the SMS memory mapper model and the SegaVdp / SN76496 chips already
// present in dsp-cpp. The LCD crops the VDP to 160×144 (GG mode); a few carts
// force full SMS resolution (detected by CRC, same list as the Pascal code).
class GameGear : public Machine {
public:
    static constexpr int kGgWidth = 160;
    static constexpr int kGgHeight = 144;
    // Full SMS-sized frame used when a cart forces SMS video mode.
    static constexpr int kSmsWidth = SegaVdp::kVisibleWidth;  // 284
    static constexpr int kSmsHeight = 243;

    static constexpr uint32_t kClockNtsc = 3579545;
    static constexpr double kFpsNtsc = 59.922743;
    static constexpr int kLinesNtsc = 262;
    static constexpr int kSampleRate = SN76496::kSampleRate;

    // Crop origin inside the 284-wide VDP line (matches Pascal actualiza_trozo).
    static constexpr int kCropX = 61;
    static constexpr int kCropY = 51;

    GameGear();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return sms_video_ ? kSmsWidth : kGgWidth; }
    int screen_height() const override { return sms_video_ ? kSmsHeight : kGgHeight; }
    double frames_per_second() const override { return kFpsNtsc; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return "Sega Game Gear"; }

    bool load_media(const std::string& path, std::string* error) override;

private:
    enum class Mapper { Sega, Codemasters };

    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    void on_cycles(int cycles);

    bool load_cartridge(const uint8_t* data, size_t length, std::string* error);
    void detect_special(uint32_t crc);
    void set_default_banks();
    static uint32_t crc32(const uint8_t* data, size_t length);

    Z80 cpu_;
    SegaVdp vdp_;
    SN76496 psg_;

    static constexpr int kMaxRomBanks = 64;
    std::array<std::array<uint8_t, 0x4000>, kMaxRomBanks> rom_{};
    int rom_banks_ = 1;
    std::array<uint8_t, 0x2000> ram_{};
    std::array<std::array<uint8_t, 0x4000>, 2> slot2_ram_{};

    std::array<uint8_t, 4> rom_bank_{};
    bool slot2_ram_enable_ = false;
    uint8_t slot2_bank_ = 0;

    Mapper mapper_ = Mapper::Sega;
    bool sms_video_ = false;  // full 284×243 instead of 160×144 crop

    std::array<uint8_t, 8> io_{};
    uint8_t keys0_ = 0xFF;  // D-pad + buttons (active low)
    uint8_t keys1_ = 0x80;  // bit7 = Start (active low), rest open bus

    int cycles_per_line_ = 0;
    int64_t audio_accumulator_ = 0;
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
