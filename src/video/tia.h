#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dsp {

// Atari TIA (NTSC): 160 visible colour clocks, 228 clocks/line, 262 lines,
// two independent audio channels.
//
// The chip is stepped one colour clock at a time with run(); a register
// write takes effect at the current colour clock plus the per-register
// delay of the real chip (as in Stella), so mid-line changes of colours,
// playfield and graphics land on the right pixel.
//
// Players, missiles and the ball follow the real divide-by-160 position
// counters: RESPx mid-line does not draw the main copy until the next line
// while NUSIZ copies still appear, a reset during a copy's start-up delay
// restarts it, and HMOVE moves objects with extra counter clocks that only
// count during HBLANK. An HMOVE early in the line stretches HBLANK by 8
// clocks (the black "comb"); the late HMOVE of Activision kernels moves the
// objects without it.
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

    // Colour clock within the current line (0..228).
    int hclock() const { return hclock_; }
    void set_hclock(int color_clocks);

    // Advance `clocks` colour clocks, never past the end of the line.
    void run(int clocks);
    bool line_done() const { return hclock_ >= kColorClocksPerLine; }
    // Start the next line (clears the per-line HMOVE blank).
    void begin_line();
    const std::array<uint32_t, kScreenWidth>& line() const { return line_; }

    // Finish the current line and copy its 160 pixels into `dest`.
    void render_line(uint32_t* dest);

    // One tick of the 31.4 kHz audio clock (two per scanline), then emit
    // 44100 Hz samples corresponding to `cpu_cycles` of 6507 time.
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
        int div3 = 0;
        uint8_t poly4 = 0x0f;
        uint8_t poly5 = 0x1f;
        uint16_t poly9 = 0x1ff;
        uint8_t bit = 0;
    };

    enum { P0 = 0, P1 = 1, M0 = 2, M1 = 3, BL = 4 };

    // A movable object: a divide-by-160 position counter that starts a copy
    // of the graphics when it decodes the right values (Stella's model).
    struct Object {
        int counter = 0;
        bool rendering = false;
        int render = 0;  // pixels since the copy started drawing (<0: delay)
        bool moving = false;
        int hmm_clocks = 8;
    };

    void clock(int c);
    void tick_object(int i);
    bool decodes(int i, int counter) const;
    uint32_t pixel(int x);
    void apply(uint8_t reg, uint8_t value);
    void reset_object(int index);
    bool player_on(int which) const;
    bool missile_on(int which) const;
    bool ball_on() const;
    bool playfield_on(int x) const;
    void clock_channel(Channel& ch);
    static int player_scale(uint8_t nusiz);

    struct Delayed { uint8_t reg, value; int clocks; };
    std::vector<Delayed> queue_;

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
    uint8_t grp_old_[2]{};
    uint8_t enam_[2]{};
    uint8_t enabl_ = 0;
    uint8_t enabl_old_ = 0;
    std::array<uint8_t, 5> hm_{};  // P0 P1 M0 M1 BL
    std::array<Object, 5> obj_{};
    bool movement_ = false;
    int movement_clock_ = 0;
    bool extended_hblank_ = false;
    bool vdelp_[2]{};
    bool vdelbl_ = false;
    bool resmp_[2]{};

    int hclock_ = 0;
    std::array<uint32_t, kScreenWidth> line_{};

    std::array<uint8_t, 8> cx_{};
    bool inpt4_ = false;
    bool inpt5_ = false;
    bool inpt4_latched_ = false;
    bool inpt5_latched_ = false;

    std::array<Channel, 2> ch_{};
    int16_t sample_ = 0;
    double audio_phase_ = 0;
};

}  // namespace dsp
