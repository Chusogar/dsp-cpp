#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "core/rom_loader.h"
#include "cpu/m6809.h"
#include "machine/mos6532.h"
#include "machine/slapstic.h"
#include "machine/starwars_math.h"
#include "sound/pokey.h"
#include "sound/tms5220.h"
#include "video/avg_starwars.h"

namespace dsp {

// Atari Star Wars (1983) and The Empire Strikes Back (1985): dual 6809,
// AVG vector display, mathbox, 4×POKEY + TMS5220. ESB adds slapstic #101
// over $8000-$9FFF and a second ROM bank at $A000-$FFFF.
class StarWars : public Machine {
public:
    enum class Game { StarWars, Empire };

    static constexpr int kScreenWidth = 400;
    static constexpr int kScreenHeight = 300;
    static constexpr uint32_t kMasterClock = 12096000;
    static constexpr uint32_t kCpuClock = kMasterClock / 8;
    static constexpr uint32_t kClock3k = kMasterClock / 4096;
    static constexpr int kIrqsPerFrame = 6;
    static constexpr double kFramesPerSecond = double(kClock3k) / 12.0 / 6.0;
    static constexpr int kSampleRate = 44100;

    explicit StarWars(Game game = Game::StarWars);

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

    const char* title() const override;

    uint16_t debug_pc() const { return main_cpu_.pc(); }
    size_t debug_avg_lines() const { return avg_.lines().size(); }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_main_cycles(int cycles);
    void on_sound_cycles(int cycles);
    void quad_pokey_w(uint16_t offset, uint8_t data);
    void outlatch_w(int bit, bool value);
    void update_video();
    void draw_line(int x0, int y0, int x1, int y1, uint32_t color, int intensity);
    uint8_t avg_read(uint16_t address) const;
    uint8_t adc_channel(int channel) const;
    bool load_starwars(RomLoader& loader, std::string* error);
    bool load_esb(RomLoader& loader, std::string* error);

    Game game_;
    M6809 main_cpu_;
    M6809 sound_cpu_;
    Pokey pokey0_;
    Pokey pokey1_;
    Pokey pokey2_;
    Pokey pokey3_;
    Tms5220 tms_;
    Mos6532 riot_;
    StarwarsMath math_;
    AvgStarwars avg_;
    Slapstic slapstic_;

    std::array<uint8_t, 0x22000> main_rom_{};
    std::array<uint8_t, 0x1000> vector_rom_{};
    std::array<uint8_t, 0x3000> vector_ram_{};
    std::array<uint8_t, 0x800> work_ram_{};
    std::array<uint8_t, 0x800> sound_ram_{};
    std::array<uint8_t, 0x10000> sound_rom_{};
    std::array<uint8_t, 0x100> nvram_{};

    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t dsw0_ = 0x98;
    uint8_t dsw1_ = 0x02;
    uint8_t analog_x_ = 0x80;
    uint8_t analog_y_ = 0x80;
    uint8_t adc_value_ = 0x80;
    int adc_channel_ = 0;
    uint8_t bank_ = 0;
    uint8_t bank2_ = 0;
    uint8_t outlatch_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t main_latch_ = 0;
    bool sound_pending_ = false;
    bool main_pending_ = false;
    uint32_t prng_ = 0x1;
    int64_t audio_accumulator_ = 0;
    uint8_t riot_pa_out_ = 0xff;
};

}  // namespace dsp
