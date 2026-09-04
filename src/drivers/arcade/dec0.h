#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "core/rom_loader.h"
#include "cpu/hu6280.h"
#include "cpu/m6502.h"
#include "cpu/m68000.h"
#include "cpu/mcs51.h"
#include "sound/okim6295.h"
#include "sound/ym2203.h"
#include "sound/ym3812.h"
#include "video/deco_bac06.h"
#include "video/gfx.h"

namespace dsp {

// Data East DEC0 hardware, ported from dec0_hw.pas.
//
// Main CPU: 68000 at 10 MHz with the BAC06 tilemap chips and the MXC06 sprites.
// Robocop, Bad Dudes and Hippodrome use a 6502 sound CPU with a YM2203, a YM3812
// and an OKI6295, plus a second processor for protection (a HuC6280 on Robocop and
// Hippodrome, an i8751 on Bad Dudes). Sly Spy and Boulder Dash (the DEC1 board)
// replace the sound 6502 with a HuC6280 and bank the whole video and sound maps
// through a state machine.
class Dec0 : public Machine {
public:
    enum class Variant { Robocop, BadDudes, Hippodrome, SlySpy, BoulderDash };

    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 57.444885;
    static constexpr int kScanlines = 272;
    static constexpr uint32_t kMainClock = 10000000;
    static constexpr uint32_t kSoundClock = 1500000;
    static constexpr uint32_t kMcuClock = 21477200 / 16;
    static constexpr uint32_t kDec1SoundClock = 12000000 / 4;
    static constexpr uint32_t kMcs51Clock = 8000000;
    static constexpr uint32_t kYm3812Clock = 3000000;
    static constexpr uint32_t kYm2203Clock = 1500000;
    static constexpr uint32_t kOkiClock = 1000000;

    explicit Dec0(Variant variant);

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
    int sample_rate() const override { return YM3812::kSampleRate; }

    const char* title() const override;

    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    bool dec1() const { return variant_ == Variant::SlySpy || variant_ == Variant::BoulderDash; }

    // Main CPU (DEC0 board).
    uint16_t main_read(uint32_t address);
    void main_write(uint32_t address, uint16_t value);
    // Main CPU (DEC1 board: Sly Spy and Boulder Dash).
    uint16_t slyspy_read(uint32_t address);
    void slyspy_write(uint32_t address, uint16_t value);

    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t slyspy_sound_read(uint32_t address);
    void slyspy_sound_write(uint32_t address, uint8_t value);

    uint8_t robocop_mcu_read(uint32_t address);
    void robocop_mcu_write(uint32_t address, uint8_t value);
    uint8_t hippo_mcu_read(uint32_t address);
    void hippo_mcu_write(uint32_t address, uint8_t value);

    uint8_t mcu_port0_read();
    void mcu_port_write(int port, uint8_t value);

    void generate_audio(int cycles, uint32_t clock);

    bool load_roms(const std::string& rom_path, std::string* error);
    bool load_main_rom(RomLoader& loader, const std::vector<RomEntry>& entries,
                       std::string* error);
    void decode_graphics(const std::vector<uint8_t>& chars, const std::vector<uint8_t>& tiles1,
                         const std::vector<uint8_t>& tiles2, const std::vector<uint8_t>& sprites);

    void write_palette(int index, uint16_t value, int plane);
    void update_palette_entry(int index);
    void update_video();
    void present();

    Variant variant_;
    M68000 main_cpu_{kMainClock};
    std::unique_ptr<M6502> sound_cpu_;
    std::unique_ptr<HuC6280> huc6280_;
    std::unique_ptr<Mcs51> mcu_;
    YM2203 ym2203_{kYm2203Clock};
    YM3812 ym3812_{kYm3812Clock};
    OKIM6295 oki_{kOkiClock, true};
    Bac06Chip bac06_;

    GfxSet chars_;
    GfxSet tiles1_;
    GfxSet tiles2_;
    GfxSet sprites_;

    std::vector<uint16_t> rom_;                  // 0x60000 bytes as big endian words
    std::array<uint16_t, 0xc00> ram1_{};         // 0x242800-0x243fff
    std::array<uint16_t, 0x2000> ram2_{};        // 0xff8000-0xffbfff (0x304000 on DEC1)
    std::array<uint16_t, 0x800> sprite_buffer_{};
    std::array<uint16_t, 0x800> palette_ram_{};  // two 0x400 word planes
    std::array<uint32_t, 0x400> palette_{};
    std::array<uint8_t, 0x10000> sound_memory_{};
    std::array<uint8_t, 0x2000> mcu_ram_{};
    std::array<uint8_t, 0x2000> mcu_shared_ram_{};
    std::vector<uint8_t> mcu_rom_;

    std::array<uint16_t, Bac06Layer::kScreenWidth * Bac06Layer::kScreenHeight> pens_{};
    std::vector<uint32_t> framebuffer_;

    uint8_t sound_latch_ = 0;
    uint8_t priority_ = 0;
    uint8_t hippodrm_lsb_ = 0;
    uint8_t slyspy_state_ = 0;
    uint8_t slyspy_sound_state_ = 0;

    uint16_t i8751_return_ = 0;
    uint16_t i8751_command_ = 0;
    std::array<uint8_t, 4> i8751_ports_{};

    uint16_t in0_ = 0xffff;
    uint16_t in1_ = 0x00f7;
    uint16_t dsw_ = 0xffff;

    int main_debt_ = 0;
    int sound_debt_ = 0;
    int mcu_debt_ = 0;
    uint32_t frames_ = 0;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
    std::vector<std::string> warnings_;
};

}  // namespace dsp
