#include "video/avg_starwars.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr int kVgVector = 0;

}  // namespace

void AvgStarwars::set_prom(const uint8_t* data, size_t size) {
    prom_.fill(0);
    if (data == nullptr || size == 0) return;
    std::memcpy(prom_.data(), data, std::min(size, prom_.size()));
}

void AvgStarwars::reset() {
    vg_reset();
    nvect_ = 0;
    display_.clear();
    pc_ = 0;
    sp_ = 0;
    dvx_ = dvy_ = 0;
    data_ = 0;
    op_ = 0;
    timer_ = 0;
    int_latch_ = 0;
    xpos_ = xcenter_ = ((xmax_ - xmin_) / 2) << 16;
    ypos_ = ycenter_ = ((ymax_ - ymin_) / 2) << 16;
    xdac_xor_ = ydac_xor_ = 0x200;
}

void AvgStarwars::go() {
    vggo();
    nvect_ = 0;
    vg_set_halt(0);
    run_state_machine();
    vg_flush();
}

void AvgStarwars::vg_reset() {
    vgrst();
    vg_set_halt(1);
}

uint8_t AvgStarwars::state_addr() const {
    return uint8_t((((state_latch_ >> 4) ^ 1) << 7) | (op_ << 4) | (state_latch_ & 0xf));
}

void AvgStarwars::update_databus() { data_ = read_ ? read_(pc_) : 0; }

void AvgStarwars::vggo() {
    pc_ = 0;
    sp_ = 0;
}

void AvgStarwars::vgrst() {
    state_latch_ = 0;
    bin_scale_ = 0;
    scale_ = 0;
    color_ = 0;
}

void AvgStarwars::vg_set_halt(int value) {
    halt_ = uint8_t(value);
    sync_halt_ = uint8_t(value);
}

uint32_t AvgStarwars::color111(int color) {
    const uint8_t r = (color & 1) ? 0xff : 0;
    const uint8_t g = (color & 2) ? 0xff : 0;
    const uint8_t b = (color & 4) ? 0xff : 0;
    return 0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void AvgStarwars::vg_add_point(int x, int y, uint32_t color, int intensity) {
    if (nvect_ >= kMaxVect) return;
    vectbuf_[size_t(nvect_)].status = kVgVector;
    vectbuf_[size_t(nvect_)].x = x;
    vectbuf_[size_t(nvect_)].y = y;
    vectbuf_[size_t(nvect_)].color = color;
    vectbuf_[size_t(nvect_)].intensity = intensity;
    nvect_++;
}

void AvgStarwars::vg_flush() {
    display_.clear();
    if (nvect_ <= 0) return;
    int xs = vectbuf_[0].x;
    int ys = vectbuf_[0].y;
    for (int i = 0; i < nvect_; i++) {
        const int xe = vectbuf_[size_t(i)].x;
        const int ye = vectbuf_[size_t(i)].y;
        if (vectbuf_[size_t(i)].intensity > 0) {
            Line line;
            line.x0 = xs;
            line.y0 = ys;
            line.x1 = xe;
            line.y1 = ye;
            line.color = vectbuf_[size_t(i)].color;
            line.intensity = vectbuf_[size_t(i)].intensity;
            display_.push_back(line);
        }
        xs = xe;
        ys = ye;
    }
    nvect_ = 0;
}

void AvgStarwars::run_state_machine() {
    int extra = 0;
    for (int step = 0; step < 200000; step++) {
        state_latch_ = uint8_t((state_latch_ & 0x10) | (prom_[state_addr()] & 0xf));
        if (st3()) {
            update_databus();
            switch (state_latch_ & 7) {
                case 0: handler_0(); break;
                case 1: handler_1(); break;
                case 2: handler_2(); break;
                case 3: handler_3(); break;
                case 4: handler_4(); break;
                case 5: handler_5(); break;
                case 6: handler_6(); break;
                case 7: handler_7(); break;
            }
        }
        state_latch_ = uint8_t((halt_ << 4) | (state_latch_ & 0xf));
        if (halt_) {
            extra++;
            if (extra > 24) break;
        }
    }
    sync_halt_ = halt_ ? 1 : 0;
}

int AvgStarwars::handler_0() {
    dvy_ = uint16_t((dvy_ & 0x1f00) | data_);
    pc_++;
    return 0;
}

int AvgStarwars::handler_1() {
    dvy12_ = uint8_t((data_ >> 4) & 1);
    op_ = uint8_t(data_ >> 5);
    int_latch_ = 0;
    dvy_ = uint16_t((dvy12_ << 12) | ((data_ & 0xf) << 8));
    dvx_ = 0;
    pc_++;
    return 0;
}

int AvgStarwars::handler_2() {
    dvx_ = uint16_t((dvx_ & 0x1f00) | data_);
    pc_++;
    return 0;
}

int AvgStarwars::handler_3() {
    int_latch_ = uint8_t(data_ >> 4);
    dvx_ = uint16_t((uint16_t(int_latch_ & 1) << 12) | ((data_ & 0xf) << 8) | (dvx_ & 0xff));
    pc_++;
    return 0;
}

int AvgStarwars::handler_4() {
    if (op0()) {
        stack_[sp_ & 3] = pc_;
    } else {
        int i = 0;
        while ((((dvy_ ^ uint16_t(dvy_ << 1)) & 0x1000) == 0) &&
               (((dvx_ ^ uint16_t(dvx_ << 1)) & 0x1000) == 0) && (i++ < 16)) {
            dvy_ = uint16_t((dvy_ & 0x1000) | ((dvy_ << 1) & 0x1fff));
            dvx_ = uint16_t((dvx_ & 0x1000) | ((dvx_ << 1) & 0x1fff));
            timer_ = uint16_t((timer_ >> 1) | 0x4000 | (uint16_t(op1()) << 7));
        }
        if (op1()) timer_ &= 0xff;
    }
    return 0;
}

int AvgStarwars::common_strobe1() {
    if (op2()) {
        if (op1()) sp_ = uint8_t((sp_ - 1) & 0xf);
        else sp_ = uint8_t((sp_ + 1) & 0xf);
    }
    return 0;
}

int AvgStarwars::handler_5() {
    if (!op2()) {
        for (int i = bin_scale_; i > 0; i--) {
            timer_ = uint16_t((timer_ >> 1) | 0x4000 | (uint16_t(op1()) << 7));
        }
        if (op1()) timer_ &= 0xff;
    }
    return common_strobe1();
}

int AvgStarwars::common_strobe2() {
    if (op2()) {
        if (op0()) {
            pc_ = uint16_t(dvy_ << 1);
            // Tempest/Quantum (and Star Wars if it JSRL 0) treat a jump to
            // address 0 as a frame boundary. Halt once we already have a list
            // so a later end-of-go flush does not replace it with a partial one.
            if (dvy_ == 0 && nvect_ > 10) halt_ = 1;
        } else {
            pc_ = stack_[sp_ & 3];
        }
    } else if (dvy12_) {
        scale_ = uint8_t(dvy_ & 0xff);
        bin_scale_ = uint8_t((dvy_ >> 8) & 7);
    }
    return 0;
}

int AvgStarwars::handler_6() {
    if (!op2() && !dvy12_) {
        intensity_ = uint8_t(dvy_ & 0xff);
        color_ = uint8_t((dvy_ >> 8) & 0xf);
    }
    return common_strobe2();
}

int AvgStarwars::common_strobe3() {
    int cycles = 0;
    halt_ = op0();
    if (!op0() && !op2()) {
        if (op1()) cycles = 0x100 - (timer_ & 0xff);
        else cycles = 0x8000 - timer_;
        timer_ = 0;
        xpos_ += ((((int(dvx_ >> 3) ^ int(xdac_xor_)) - 0x200) * cycles * (scale_ ^ 0xff)) >> 4);
        ypos_ -= ((((int(dvy_ >> 3) ^ int(ydac_xor_)) - 0x200) * cycles * (scale_ ^ 0xff)) >> 4);
    }
    if (op2()) {
        cycles = 0x8000 - timer_;
        timer_ = 0;
        xpos_ = xcenter_;
        ypos_ = ycenter_;
        vg_add_point(int(xpos_), int(ypos_), 0, 0);
    }
    return cycles;
}

int AvgStarwars::handler_7() {
    const int cycles = common_strobe3();
    if (!op0() && !op2()) {
        vg_add_point(int(xpos_), int(ypos_), color111(color_),
                     ((int(int_latch_) >> 1) * int(intensity_)) >> 3);
    }
    return cycles;
}

}  // namespace dsp
