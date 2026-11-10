#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"

struct as1_core;
struct as1_mo_list;

namespace dsp {

// Atari System 1 games of the lean C machine of src/lean/atarisy1
// (atarisy1_core.c, a port of AtariSystem1 tuned to run on the ESP32 and
// RP2350 boards): Indiana Jones and the Temple of Doom and Road Runner.
// Musashi for the 68010 with the programs' busy loops fast-forwarded exactly,
// the sound 6502 compiled inside the machine with its polling loop
// fast-forwarded, the chips' work done lazily, the tiles packed to 4 bpp rows
// and the picture composed from the video RAM (no planes kept). Marble Madness
// and Peter Pack Rat stay on AtariSystem1.
class AtariSystem1Lean : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    static constexpr int kScreenWidth = 336;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 59.922743;
    static constexpr int kSampleRate = 44100;

    enum class Game { Indy, RoadRunner };

    explicit AtariSystem1Lean(Game game = Game::Indy);
    ~AtariSystem1Lean() override;

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int, uint8_t) override {}

    const uint32_t* framebuffer() const override;
    PenRows pen_rows() const override;
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }
    void set_draw_enabled(bool enabled) override { draw_ = enabled; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    void set_audio_enabled(bool enabled) override;

    const char* title() const override;

    // Writes the images the core runs from (lean_*.bin) to `dir`: a ROM
    // "set" of just those files is read in place on the boards (from flash),
    // with no copy in RAM (tools/lean_roms).
    bool write_prepared(const std::string& dir, std::string* error) const;

    // Cycles the core fast-forwarded (68010, 6502), for the tests.
    uint32_t debug_skipped_main() const;
    uint32_t debug_skipped_sound() const;

private:
    bool load_roms(const std::string& rom_path, std::string* error);
    static void on_line_239(void* user, as1_core* core);
    static void rows(const void* source, int y, int n, uint16_t* pens);
    void compose() const;

    struct Lean;                      // the C core
    Game game_;
    std::unique_ptr<Lean> lean_;
    // The images (in place on a board when the set is prepared, else here):
    const uint8_t* main_ = nullptr;   // 0x88000 bytes, big endian words
    const uint8_t* sound_ = nullptr;  // 0xc000 bytes: 6502 $4000-$FFFF
    const uint8_t* chars_ = nullptr;  // 0x2000 bytes, 2bpp
    const uint8_t* proms_ = nullptr;  // 0x400 bytes
    const uint32_t* tiles_[8] = {};   // graphics banks 1..7, 4bpp rows
    std::vector<uint8_t> main_rom_, sound_rom_, char_rom_, prom_data_;
    std::vector<std::vector<uint32_t>> tile_rows_;
    // Motion object lists: the one being built, the last finished one and the
    // one pen_rows() draws from (maybe on the other core).
    as1_mo_list* mo_[3] = {};
    int mo_build_ = 0;
    std::atomic<int> mo_ready_{-1};
    mutable std::atomic<int> mo_used_{-1};
    mutable const as1_mo_list* mo_draw_ = nullptr;
    mutable int rows_y_ = 1 << 30;    // last row pen_rows() drew (smaller next: a new picture)
    uint32_t palette_[0x400] = {};    // ARGB, at line 239
    bool ready_ = false;
    bool draw_ = true;
    mutable std::vector<uint32_t> framebuffer_;  // made when asked for
    std::vector<int16_t> audio_;
    std::vector<int16_t> frame_audio_;
};

}  // namespace dsp
