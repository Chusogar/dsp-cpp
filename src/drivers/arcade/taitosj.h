#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "core/rom_loader.h"
#include "cpu/m6805.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"

namespace dsp {

// Taito SJ hardware, ported from taitosj_hw.pas.
// Main Z80 at 4 MHz, sound Z80 at 3 MHz with four AY-3-8910 and a DAC, plus a
// MC68705 protection MCU on Elevator Action.
//
// Written to run on an ESP32 without PSRAM (about 200 KB of heap, no block
// over ~100 KB): the tile layers and sprites are drawn straight from the
// character RAM into an 8-bit picture, with no decoded graphics and no
// 256x256 layer bitmaps, and the 32-bit framebuffer is only built when asked
// for (indexed_framebuffer() is what the small boards show).
class TaitoSJ : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
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

    const uint32_t* framebuffer() const override;
    const uint8_t* indexed_framebuffer() const override { return screen_.data(); }
    const uint32_t* indexed_palette() const override { return palette_.data(); }
    void set_draw_enabled(bool enabled) override { draw_enabled_ = enabled; }
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
    uint8_t bus_peek(uint16_t address) const;  // what the MCU sees on the Z80 bus
    uint8_t main_rom(size_t offset) const { return main_pages_[offset >> 12][offset & 0xfff]; }
    struct LayerFrame {
        const uint8_t* chars = nullptr;
        const uint8_t* map = nullptr;
        uint16_t scroll_x = 0;
        uint8_t color = 0;
        std::array<int, 32> offset_y{};  // per 8-pixel group of the screen
    };
    struct SpriteFrame {
        const uint8_t* pixels;
        uint8_t sx, sy, color;
        bool flip_x, flip_y;
    };
    void update_video();
    LayerFrame layer_frame(int layer, uint16_t scroll_x, uint8_t scroll_y, int column_base) const;
    static void blit_layer_row(const LayerFrame& f, int y, uint8_t* line);
    int sprite_frame(std::array<SpriteFrame, 32>& out) const;
    static void draw_sprites_row(const SpriteFrame* sprites, int count, int y, uint8_t* line);
    void dac_update();

    Variant variant_;
    Z80 main_cpu_;
    Z80 sound_cpu_;
    M6805 mcu_;
    AY8910 ay0_;  // reads DIP switches B and C
    AY8910 ay1_;  // drives the DAC
    AY8910 ay2_;  // writes the extra input port
    AY8910 ay3_;  // sound NMI enable

    // ROMs: main $0000-$5fff, then the two $6000-$7fff banks from $6000 and
    // $8000 of the image; sound $0000-$3fff; graphics read through $d404.
    std::array<const uint8_t*, 9> main_pages_{};
    std::array<const uint8_t*, 4> sound_pages_{};
    std::array<const uint8_t*, 8> gfx_pages_{};
    std::vector<uint8_t> main_rom_, sound_rom_, gfx_rom_;  // when not read in place
    std::vector<uint8_t> ram_;  // $8000-$d1ff: work RAM, character RAM, tile maps, sprites
    std::array<uint8_t, 0x400> sound_ram_{};
    std::array<uint8_t, 0x800> mcu_memory_{};
    std::array<std::array<uint8_t, 4>, 32> draw_order_{};

    std::array<uint8_t, 0x80> palette_ram_{};
    std::array<uint32_t, 256> palette_{};  // 64 used; 256 for indexed_palette()
    std::vector<uint8_t> screen_;  // 256x224 pens into palette_
    mutable std::vector<uint32_t> framebuffer_;  // built by framebuffer()

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
    int32_t dac_mixed_ = 0;  // dac_sample_ at its mixing level
    bool draw_enabled_ = true;
    bool sound_semaphore_ = false;
    std::array<bool, 2> sound_nmi_{};

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
