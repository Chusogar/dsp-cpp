#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "video/maria.h"
#include "video/tia.h"

namespace dsp {

// Atari 7800 ProSystem. A 6502 ("SALLY") paired with the MARIA display
// processor, which halts the CPU while it fetches graphics. Sound comes
// from the 2600's TIA, with some carts adding a POKEY.
class A7800 : public Machine {
public:
    enum class Region { Ntsc, Pal };

    static constexpr int kWidth = Maria::kWidth;
    static constexpr int kHeight = 240;
    static constexpr int kCyclesPerLine = 114;
    static constexpr uint32_t kCpuClock = 1789773;
    static constexpr int kSampleRate = 44100;

    explicit A7800(Region region = Region::Ntsc);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int, uint8_t) override {}
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return region_ == Region::Pal ? 49.86 : 59.92; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "Atari 7800"; }

private:
    uint8_t cpu_read(uint16_t address);
    void cpu_write(uint16_t address, uint8_t value);
    uint8_t cart_read(uint16_t address) const;
    void cart_write(uint16_t address, uint8_t value);
    bool load_cart(const std::string& path, std::string* error);

    Region region_;
    M6502 cpu_;
    Maria maria_;
    // The 7800 keeps the 2600's TIA on the board purely for its two audio
    // channels; MARIA handles all the video.
    Tia tia_;

    std::vector<uint8_t> cart_;
    int cart_banks_ = 0;          // 16K banks, SuperGame carts only
    bool supergame_ = false;
    bool bank6_at_4000_ = false;
    uint8_t bank_ = 0;

    std::array<uint8_t, 0x1000> ram_{};      // $1800-$27FF
    std::array<uint8_t, 0x80> riot_ram_{};

    // RIOT timer: counts down at one of four divider rates.
    uint32_t riot_timer_ = 0;
    int riot_shift_ = 10;
    bool riot_irq_ = false;

    uint8_t swcha_ = 0xff, swchb_ = 0x0b;
    uint8_t inpt_[6] = {0, 0, 0, 0, 0x80, 0x80};

    std::array<uint32_t, size_t(kWidth) * kHeight> framebuffer_{};
    std::array<uint8_t, size_t(kWidth)> line_{};
    std::vector<int16_t> audio_;
};

}  // namespace dsp
