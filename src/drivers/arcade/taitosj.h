#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6805.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"
#include "video/gfx.h"

namespace dsp {

// Taito SJ hardware, ported from taitosj_hw.pas.
// Main Z80 at 4 MHz, sound Z80 at 3 MHz with four AY-3-8910 and a DAC, plus a
// MC68705 protection MCU on Elevator Action.
class TaitoSJ : public Machine {
public:
    enum class Variant { ElevatorAction, JungleKing };

    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kMainClock = 4000000;
    static constexpr uint32_t kSoundClock = 3000000;
    static constexpr uint32_t kMcuClock = 3000000;
    static constexpr uint32_t kAyClock = 1500000;
    // Sound CPU interrupt period, timers.init() in taitosj_hw.pas.
    static constexpr int kSoundIrqPeriod = 4 * 16 * 16 * 10 * 16 * (3000000 / 6000000.0);

    explicit TaitoSJ(Variant variant);

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
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override {
        return variant_ == Variant::ElevatorAction ? "Elevator Action" : "Jungle King";
    }

private:
    bool uses_mcu() const { return variant_ == Variant::ElevatorAction; }

    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t mcu_read(uint16_t address);
    void mcu_write(uint16_t address, uint8_t value);
    void on_sound_cycles(int cycles);

    void set_palette_entry(int index);
    void decode_chars(int bank);
    void update_video();
    void draw_layer(int layer);
    void draw_sprites();
    void blit_layer(int layer, uint16_t scroll_x, uint8_t scroll_y, int column_base);
    void dac_update();

    Variant variant_;
    Z80 main_cpu_;
    Z80 sound_cpu_;
    M6805 mcu_;
    AY8910 ay0_;  // reads DIP switches B and C
    AY8910 ay1_;  // drives the DAC
    AY8910 ay2_;  // writes the extra input port
    AY8910 ay3_;  // sound NMI enable

    std::array<uint8_t, 0x10000> memory_{};
    std::array<std::array<uint8_t, 0x2000>, 2> rom_banks_{};
    std::array<uint8_t, 0x4400> sound_memory_{};
    std::array<uint8_t, 0x8000> gfx_rom_{};
    std::array<uint8_t, 0x800> mcu_memory_{};
    std::array<std::array<uint8_t, 4>, 32> draw_order_{};

    // gfx 0/2: 8x8 characters of both character RAM banks.
    // gfx 1/3: 16x16 sprites built from the same character RAM.
    std::array<GfxSet, 2> chars_;
    std::array<GfxSet, 2> sprites_;

    std::array<uint8_t, 0x80> palette_ram_{};
    std::array<uint32_t, 64> palette_{};
    // Tile layers and the composed screen, all 256x256 with kTransparent pens.
    std::array<std::array<uint8_t, 256 * 256>, 3> layers_{};
    std::array<uint8_t, 256 * 256> screen_{};
    std::vector<uint32_t> framebuffer_;

    std::array<uint8_t, 4> collision_{};
    std::array<uint8_t, 6> scroll_{};
    std::array<uint8_t, 2> colorbank_{};
    std::array<uint16_t, 0x60> scroll_y_{};
    std::array<int8_t, 5> pos_x_{};
    uint16_t gfx_pos_ = 0;
    uint8_t video_priority_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t rom_bank_ = 0;
    uint8_t video_mode_ = 0;
    uint8_t dac_out_ = 0;
    uint8_t dac_vol_ = 0;
    int16_t dac_sample_ = 0;
    bool sound_semaphore_ = false;
    std::array<bool, 2> sound_nmi_{};
    std::array<bool, 2> rechars_{};

    // MCU handshake.
    uint8_t mcu_to_z80_ = 0;
    uint8_t mcu_from_z80_ = 0;
    uint16_t mcu_address_ = 0;
    uint8_t mcu_port_a_in_ = 0;
    uint8_t mcu_port_a_out_ = 0;
    bool mcu_zaccept_ = true;
    bool mcu_zready_ = false;
    bool mcu_busreq_ = false;

    uint8_t in0_ = 0xff;  // player 1
    uint8_t in1_ = 0xff;  // player 2
    uint8_t in2_ = 0xff;  // coins and start buttons
    uint8_t in4_ = 0x00;  // written by the sound CPU through AY 2 port A
    uint8_t dsw_a_ = 0xff;
    uint8_t dsw_b_ = 0x00;
    uint8_t dsw_c_ = 0xff;

    int sound_irq_counter_ = 0;
    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
