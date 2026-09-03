#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Atari Digital Vector Generator (DVG) as used by Asteroids.
// Ported from dsp-emulator avg_dvg.pas / MAME avgdvg.cpp (dvg_device).
class Dvg {
public:
    using ReadByte = std::function<uint8_t(uint16_t)>;

    struct Line {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        int intensity = 0;
    };

    static constexpr int kVisWidth = 400;
    static constexpr int kVisHeight = 320;

    explicit Dvg(uint16_t membase = 0x4000, uint16_t x_desp = 40);

    void set_prom(const uint8_t* data, size_t size);
    void set_memory(ReadByte read) { read_ = std::move(read); }

    void reset();
    void go();
    bool done() const { return sync_halt_ != 0; }

    const std::vector<Line>& lines() const { return display_; }

private:
    static constexpr int kMaxVect = 10000;

    struct Vect {
        int x = 0, y = 0;
        int intensity = 0;
    };

    uint8_t op0() const { return uint8_t(op_ & 1); }
    uint8_t op1() const { return uint8_t((op_ >> 1) & 1); }
    uint8_t op3() const { return uint8_t((op_ >> 3) & 1); }

    uint8_t state_addr() const;
    void update_databus();
    void vg_add_point(int x, int y, int intensity);
    void draw_to(int x, int y, int intensity);
    void vg_flush();
    void run_until_halt();

    int handler_dmapush();
    int handler_dmald();
    int handler_gostrobe();
    int handler_haltstrobe();
    int handler_latch0();
    int handler_latch1();
    int handler_latch2();
    int handler_latch3();

    static int map_x(int x);
    static int map_y(int y);

    ReadByte read_;
    std::array<uint8_t, 256> prom_{};

    uint16_t membase_ = 0x4000;
    uint16_t x_desp_ = 40;

    uint16_t pc_ = 0;
    uint8_t sp_ = 0;
    uint16_t dvx_ = 0, dvy_ = 0;
    std::array<uint16_t, 4> stack_{};
    uint8_t data_ = 0;
    uint8_t state_latch_ = 0;
    uint8_t scale_ = 0;
    uint8_t intensity_ = 0;
    uint8_t op_ = 0;
    uint8_t halt_ = 1;
    uint8_t sync_halt_ = 1;
    int xpos_ = 0, ypos_ = 0;

    int nvect_ = 0;
    std::array<Vect, kMaxVect> vectbuf_{};
    std::vector<Line> display_;
};

}  // namespace dsp
