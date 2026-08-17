#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Atari AVG as used by Star Wars, ported from MAME avgdvg.cpp (AVG_STARWARS).
class AvgStarwars {
public:
    using ReadByte = std::function<uint8_t(uint16_t)>;

    struct Line {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        uint32_t color = 0;
        int intensity = 0;
    };

    static constexpr int kVisWidth = 250;
    static constexpr int kVisHeight = 280;

    void set_prom(const uint8_t* data, size_t size);
    void set_memory(ReadByte read) { read_ = std::move(read); }

    void reset();
    void go();
    void vg_reset();
    bool done() const { return sync_halt_ != 0; }

    const std::vector<Line>& lines() const { return display_; }

private:
    static constexpr int kMaxVect = 10000;
    static constexpr int kVgSlice = 10000;
    static constexpr uint32_t kMasterClock = 12096000;

    struct Vect {
        int status = 0;
        int x = 0, y = 0;
        uint32_t color = 0;
        int intensity = 0;
    };

    uint8_t op0() const { return uint8_t(op_ & 1); }
    uint8_t op1() const { return uint8_t((op_ >> 1) & 1); }
    uint8_t op2() const { return uint8_t((op_ >> 2) & 1); }
    uint8_t st3() const { return uint8_t((state_latch_ >> 3) & 1); }

    uint8_t state_addr() const;
    void update_databus();
    void vggo();
    void vgrst();
    void vg_set_halt(int value);
    void vg_add_point(int x, int y, uint32_t color, int intensity);
    void vg_flush();
    void run_state_machine();

    int handler_0();
    int handler_1();
    int handler_2();
    int handler_3();
    int handler_4();
    int handler_5();
    int handler_6();
    int handler_7();
    int common_strobe1();
    int common_strobe2();
    int common_strobe3();

    static uint32_t color111(int color);

    ReadByte read_;
    std::array<uint8_t, 256> prom_{};

    int xmin_ = 0, ymin_ = 0, xmax_ = kVisWidth - 1, ymax_ = kVisHeight - 1;
    int32_t xcenter_ = 0, ycenter_ = 0;
    int32_t xpos_ = 0, ypos_ = 0;

    uint16_t pc_ = 0;
    uint8_t sp_ = 0;
    uint16_t dvx_ = 0, dvy_ = 0;
    std::array<uint16_t, 4> stack_{};
    uint16_t data_ = 0;
    uint8_t state_latch_ = 0;
    uint8_t scale_ = 0;
    uint8_t intensity_ = 0;
    uint8_t op_ = 0;
    uint8_t halt_ = 1;
    uint8_t sync_halt_ = 1;
    uint8_t dvy12_ = 0;
    uint16_t timer_ = 0;
    uint8_t int_latch_ = 0;
    uint8_t bin_scale_ = 0;
    uint8_t color_ = 0;
    uint16_t xdac_xor_ = 0x200;
    uint16_t ydac_xor_ = 0x200;

    int nvect_ = 0;
    std::array<Vect, kMaxVect> vectbuf_{};
    std::vector<Line> display_;
};

}  // namespace dsp
