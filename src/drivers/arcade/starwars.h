#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6809.h"
#include "machine/mos6532.h"
#include "machine/starwars_math.h"
#include "sound/pokey.h"
#include "sound/tms5220.h"
#include "video/avg_starwars.h"

namespace dsp {

// Atari Star Wars (1983): dual 6809, AVG vector display, mathbox, 4×POKEY + TMS5220.
//
// Small enough for boards without PSRAM: the ROMs are read in place when the
// board maps them, and with a picture limit (set_picture_limit) the vectors
// are drawn scaled into an 8-bit picture of that size (3-bit colour and 5-bit
// brightness a pixel) instead of the 400x300 32-bit one.
class StarWars : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    // The Empire Strikes Back runs on the same board with an extra bank of
    // ROM at $8000-$9FFF selected by an Atari "slapstic" security chip.
    enum class Game { StarWars, Esb };

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

    const uint32_t* framebuffer() const override;
    const uint8_t* indexed_framebuffer() const override { return small_ ? picture_.data() : nullptr; }
    const uint32_t* indexed_palette() const override { return small_ ? palette_.data() : nullptr; }
    void set_picture_limit(int width, int height) override;
    int screen_width() const override { return width_; }
    int screen_height() const override { return height_; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return int(sample_rate_); }
    void set_sample_rate_limit(int rate) override {
        if (rate > 0 && rate < kSampleRate) {
            sample_rate_ = uint32_t(rate);
            tms_.set_sample_rate(sample_rate_);
        }
    }
    void set_draw_enabled(bool enabled) override { draw_enabled_ = enabled; }
    // Off: the POKEYs are not run (the program never reads them) and the
    // speech chip only runs for its status lines.
    void set_audio_enabled(bool enabled) override { audio_enabled_ = enabled; }

    const char* title() const override {
        return game_ == Game::Esb ? "The Empire Strikes Back" : "Star Wars";
    }

    // The yoke is an analog control, so let the front end hand us the mouse
    // position and fly with it.
    bool uses_pointer() const override { return true; }

    uint16_t debug_pc() const { return main_cpu_.pc(); }
    size_t debug_avg_lines() const { return avg_.lines().size(); }
    // Called with the main CPU PC after every instruction (debugging).
    void debug_set_trace(std::function<void(uint16_t)> trace) { trace_ = std::move(trace); }
    uint8_t debug_main_read(uint16_t address) { return main_read(address); }
    int debug_slapstic_bank() const { return slapstic_bank_; }

private:
    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void on_main_cycles(int cycles);
    void on_sound_cycles(int cycles);
    void quad_pokey_w(uint16_t offset, uint8_t data);
    void outlatch_w(int bit, bool value);
    void set_sound_pending(bool pending);
    void update_video();
    void draw_line(int x0, int y0, int x1, int y1, uint32_t color, int intensity);
    void draw_line_small(int x0, int y0, int x1, int y1, int color, int intensity);
    void decay_small();
    uint8_t rom_byte(uint32_t offset) const {
        const uint8_t* page = main_pages_[(offset >> 13) % main_pages_.size()];
        return page ? page[offset & 0x1fff] : 0;
    }
    uint8_t avg_read(uint16_t address) const;
    uint8_t adc_channel(int channel) const;

    Game game_ = Game::StarWars;
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

    // Main program: 8 KB pages of the 0x22000-byte image the memory map uses
    // (null: reads as 0); sound program: 8 KB pages of its 64 KB space.
    std::array<const uint8_t*, 0x11> main_pages_{};
    std::array<const uint8_t*, 8> sound_pages_{};
    const uint8_t* vector_rom_ = nullptr;  // 4 KB
    std::vector<std::vector<uint8_t>> owned_roms_;  // the ROMs the board does not map
    std::array<uint8_t, 0x3000> vector_ram_{};
    std::array<uint8_t, 0x800> work_ram_{};
    std::array<uint8_t, 0x800> sound_ram_{};
    std::array<uint8_t, 0x100> nvram_{};

    // Picture: 32-bit at 400x300, or (small_) 8-bit at width_ x height_.
    int width_ = kScreenWidth, height_ = kScreenHeight;
    bool small_ = false;
    int scale_ = 1 << 16;  // small_: picture pixels per screen pixel, 16.16
    mutable std::vector<uint32_t> framebuffer_;
    mutable bool framebuffer_stale_ = false;
    std::vector<uint8_t> picture_;          // colour << 5 | brightness 0..31
    std::array<uint32_t, 256> palette_{};
    std::array<uint8_t, 256> decay_{};      // a pixel one frame later
    uint32_t sample_rate_ = kSampleRate;
    bool draw_enabled_ = true;
    bool audio_enabled_ = true;
    std::vector<int16_t> audio_;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t dsw0_ = 0x98;
    uint8_t dsw1_ = 0x02;
    uint8_t analog_x_ = 0x80;
    uint8_t analog_y_ = 0x80;
    uint8_t adc_value_ = 0x80;
    int adc_channel_ = 0;
    // Slapstic (137412-101) state. It watches accesses inside its address
    // window and switches bank on recognised address sequences.
    void slapstic_access(uint16_t address);
    uint8_t main_read_raw(uint16_t address);
    int slapstic_state_ = 0;
    int slapstic_bank_ = 3;
    int slapstic_loaded_bank_ = 0;
    uint8_t bank_ = 0;
    uint8_t outlatch_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t main_latch_ = 0;
    bool sound_pending_ = false;
    bool main_pending_ = false;
    uint32_t prng_ = 0x1;
    int64_t audio_accumulator_ = 0;
    int64_t pokey_sum_ = 0;
    int64_t pokey_cycles_ = 0;
    // Below 44.1 kHz (a board): the POKEYs run in batches, when written and
    // twice an output sample, and those two readings make the sample.
    void sync_pokeys();
    void emit_sample(double pokeys);
    int pokey_pending_ = 0;
    int64_t sub_accumulator_ = 0;
    int sub_samples_ = 0;
    double dc_in_ = 0.0;
    double dc_out_ = 0.0;
    uint8_t riot_pa_out_ = 0xff;
    std::function<void(uint16_t)> trace_;
};

}  // namespace dsp
