#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// Atari TIA (NTSC): 160 visible colour clocks, 228 clocks/line, 262 lines,
// two independent audio channels. Cycle-perfect mid-line sprite tricks are
// approximated by sampling object state at the end of each scanline.
class Tia {
public:
    static constexpr int kScreenWidth = 160;
    static constexpr int kColorClocksPerLine = 228;
    static constexpr int kHblankClocks = 68;
    static constexpr int kCpuCyclesPerLine = 76;
    static constexpr int kScanlines = 262;
    static constexpr int kSampleRate = 44100;

    Tia();

    void reset();

    void write(uint8_t reg, uint8_t value);
    uint8_t read(uint8_t reg) const;

    bool wsync() const { return wsync_; }
    void clear_wsync() { wsync_ = false; }

    bool vsync() const { return vsync_; }
    bool vblank() const { return vblank_; }
    bool blanked() const { return vsync_ || vblank_; }

    void set_hclock(int color_clocks);
    void add_cpu_cycles(int cycles);

    // Draw the 160 visible pixels of the current line into `dest` and update
    // collision latches. Blanked lines are filled with black.
    void render_line(uint32_t* dest);

    // One HSYNC tick of the two audio channels, then emit 44100 Hz samples
    // corresponding to `cpu_cycles` of 6507 time.
    void clock_audio();
    void emit_audio(int cpu_cycles, uint32_t cpu_clock, std::vector<int16_t>& dest);
    int16_t last_sample() const { return sample_; }

    void set_inpt4(bool pressed);
    void set_inpt5(bool pressed);

    static uint32_t ntsc_color(uint8_t colu);

private:
    struct Channel {
        uint8_t audc = 0;
        uint8_t audf = 0;
        uint8_t audv = 0;
        int divider = 0;
        int div6 = 0;
        uint8_t poly4 = 0x0f;
        uint8_t poly5 = 0x1f;
        uint16_t poly9 = 0x1ff;
        uint8_t bit = 0;
    };

    void apply_hmove();
    void reset_object(int index);
    int player_pixel(int clock, int which) const;
    int missile_pixel(int clock, int which) const;
    int ball_pixel(int clock) const;
    int playfield_pixel(int x) const;
    uint8_t visible_grp(int which) const;
    bool missile_enabled(int which) const;
    bool ball_enabled() const;
    void clock_channel(Channel& ch);
    static int player_scale(uint8_t nusiz);
    static int copy_count(uint8_t nusiz);
    static int copy_offset(uint8_t nusiz, int copy);
    static int missile_width(uint8_t nusiz);

    bool wsync_ = false;
    bool vsync_ = false;
    bool vblank_ = false;
    bool dump_ports_ = false;
    bool latch_inputs_ = false;

    uint8_t nusiz_[2]{};
    uint8_t colup_[2]{};
    uint8_t colupf_ = 0;
    uint8_t colubk_ = 0;
    uint8_t ctrlpf_ = 0;
    uint8_t refp_[2]{};
    uint8_t pf0_ = 0, pf1_ = 0, pf2_ = 0;
    uint8_t grp_[2]{};
    uint8_t grp_delay_[2]{};
    uint8_t enam_[2]{};
    uint8_t enabl_ = 0;
    uint8_t enabl_delay_ = 0;
    uint8_t hmm_[5]{};  // P0 P1 M0 M1 BL
    bool vdelp_[2]{};
    bool vdelbl_ = false;
    bool resmp_[2]{};

    int pos_[5]{};  // colour clocks, P0 P1 M0 M1 BL
    int hclock_ = 0;

    uint8_t cx_[8]{};
    bool inpt4_ = false;
    bool inpt5_ = false;
    bool inpt4_latched_ = false;
    bool inpt5_latched_ = false;

    std::array<Channel, 2> ch_{};
    int16_t sample_ = 0;
    double audio_phase_ = 0;
};

}  // namespace dsp
