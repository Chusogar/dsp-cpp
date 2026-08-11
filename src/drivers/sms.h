#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/sega_vdp.h"
#include "sound/sn76496.h"
#include "sound/ym2413.h"

namespace dsp {

// Sega Master System / Mark III, ported from sms.pas (dsp-emulator).
// Uses SegaVdp (Mode 4), Z80, SN76496 and YM2413 (OPLL).
class Sms : public Machine {
public:
    // Display includes the VDP borders (284 × visible height).
    static constexpr int kScreenWidth = SegaVdp::kVisibleWidth;  // 284
    static constexpr int kScreenHeightNtsc = 243;
    static constexpr int kScreenHeightPal = 294;

    static constexpr uint32_t kClockNtsc = 3579545;
    static constexpr uint32_t kClockPal = 3546895;
    static constexpr double kFpsNtsc = 59.922743;
    static constexpr double kFpsPal = 49.701460;

    static constexpr int kSampleRate = SN76496::kSampleRate;

    // 0 = PAL, 1 = Japan NTSC, 2 = Export NTSC
    enum class Model : uint8_t { Pal = 0, Japan = 1, Export = 2 };

    explicit Sms(Model model = Model::Export);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override {
        return model_ == Model::Pal ? kScreenHeightPal : kScreenHeightNtsc;
    }
    double frames_per_second() const override {
        return model_ == Model::Pal ? kFpsPal : kFpsNtsc;
    }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override { return "Sega Master System"; }

    // Load a cartridge image (.sms / .bin / raw). Optional BIOS is loaded from
    // init() when present in the ROM set.
    bool load_media(const std::string& path, std::string* error) override;

    Model model() const { return model_; }

private:
    enum class Mapper {
        Sega,
        Codemasters,
        Korean,
        FourPack,
        CyborgZ,
        Nemesis,
    };

    uint8_t read_byte(uint16_t address) const;
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    void on_cycles(int cycles);

    void config_io(uint8_t value);
    void apply_model_timing();
    bool load_cartridge(const uint8_t* data, size_t length, std::string* error);
    void detect_mapper(uint32_t crc);
    void set_default_banks();

    // Memory map helpers for the various mappers.
    uint8_t read_sega(uint16_t address) const;
    void write_sega(uint16_t address, uint8_t value);
    uint8_t read_no_sega(uint16_t address) const;
    void write_codemasters(uint16_t address, uint8_t value);
    void write_korean(uint16_t address, uint8_t value);
    void write_four_pack(uint16_t address, uint8_t value);
    uint8_t read_cyborgz(uint16_t address) const;
    void write_cyborgz(uint16_t address, uint8_t value);
    uint8_t read_nemesis(uint16_t address) const;

    Model model_;
    Mapper mapper_ = Mapper::Sega;

    Z80 cpu_;
    SegaVdp vdp_;
    SN76496 psg_;
    YM2413 opll_;

    // Cartridge: up to 64 × 16 KiB banks.
    static constexpr int kMaxRomBanks = 64;
    std::array<std::array<uint8_t, 0x4000>, kMaxRomBanks> rom_{};
    int rom_banks_ = 1;

    // BIOS: up to 16 × 16 KiB.
    static constexpr int kMaxBiosBanks = 16;
    std::array<std::array<uint8_t, 0x4000>, kMaxBiosBanks> bios_{};
    int bios_banks_ = 1;
    bool has_bios_ = false;

    std::array<uint8_t, 0x2000> ram_{};
    std::array<std::array<uint8_t, 0x4000>, 2> slot2_ram_{};

    std::array<uint8_t, 4> rom_bank_{};
    std::array<uint8_t, 4> bios_bank_{};
    uint8_t slot2_bank_ = 0;
    bool bslot2_ram_ = false;

    bool bios_enabled_ = true;
    bool cart_enabled_ = false;
    bool io_enabled_ = true;

    std::array<uint8_t, 2> keys_{0xff, 0xff};
    bool push_pause_ = false;
    uint8_t old_3f_ = 0;
    uint8_t old_f2_ = 0;

    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
    int64_t audio_accumulator_ = 0;

    int cycles_per_line_ = 228;
    int lines_per_frame_ = SegaVdp::kLinesNtsc;
};

}  // namespace dsp
