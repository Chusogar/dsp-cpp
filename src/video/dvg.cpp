#include "video/dvg.h"

#include <algorithm>
#include <cstring>

namespace dsp {

Dvg::Dvg(uint16_t membase, uint16_t x_desp) : membase_(membase), x_desp_(x_desp) {}

void Dvg::set_prom(const uint8_t* data, size_t size) {
    prom_.fill(0);
    if (data == nullptr || size == 0) return;
    std::memcpy(prom_.data(), data, std::min(size, prom_.size()));
}

void Dvg::reset() {
    state_latch_ = 0;
    op_ = 0;
    halt_ = 1;
    pc_ = 0;
    data_ = 0;
    sp_ = 0;
    stack_.fill(0);
    dvy_ = 0;
    dvx_ = 0;
    scale_ = 0;
    intensity_ = 0;
    xpos_ = 0;
    ypos_ = 0;
    sync_halt_ = 1;
    nvect_ = 0;
    display_.clear();
}

uint8_t Dvg::state_addr() const {
    uint8_t addr = uint8_t(((((state_latch_ >> 4) ^ 1) & 1) << 7) | (state_latch_ & 0x0f));
    if (op3()) addr |= uint8_t((op_ & 7) << 4);
    return addr;
}

void Dvg::update_databus() {
    if (!read_) {
        data_ = 0;
        return;
    }
    data_ = read_(uint16_t(membase_ + (pc_ << 1) + (state_latch_ & 1)));
}

void Dvg::vg_add_point(int x, int y, int intensity) {
    if (nvect_ >= kMaxVect) return;
    vectbuf_[size_t(nvect_)].x = x;
    vectbuf_[size_t(nvect_)].y = y;
    vectbuf_[size_t(nvect_)].intensity = intensity;
    nvect_++;
}

void Dvg::draw_to(int x, int y, int intensity) {
    if (((x | y) & 0x400) == 0) vg_add_point(x, y, intensity);
}

int Dvg::handler_dmapush() {
    if (!op0()) {
        sp_ = uint8_t((sp_ + 1) & 0x0f);
        stack_[sp_ & 3] = pc_;
    }
    return 0;
}

int Dvg::handler_dmald() {
    if (op0()) {
        pc_ = stack_[sp_ & 3];
        sp_ = uint8_t((sp_ - 1) & 0x0f);
    } else {
        pc_ = dvy_;
    }
    return 0;
}

int Dvg::handler_gostrobe() {
    int scale;
    if (op_ == 0x0f) {
        scale = (scale_ + (((dvy_ & 0x800) >> 11) | (((dvx_ & 0x800) ^ 0x800) >> 10) |
                           ((dvx_ & 0x800) >> 9))) &
                0x0f;
        dvy_ &= 0x0f00;
        dvx_ &= 0x0f00;
    } else {
        scale = (scale_ + op_) & 0x0f;
    }

    int fin = 0x0fff - (((2 << scale) & 0x07ff) ^ 0x0fff);
    const int dx = (dvx_ & 0x400) ? -1 : 1;
    const int dy = (dvy_ & 0x400) ? -1 : 1;
    const int mx = (dvx_ << 2) & 0x0fff;
    const int my = (dvy_ << 2) & 0x0fff;
    const int cycles = 8 * fin;
    int c = 0;

    while (fin--) {
        int countx = 0;
        int county = 0;
        for (int bit = 0; bit < 12; bit++) {
            if ((c & ((1 << (bit + 1)) - 1)) == ((1 << bit) - 1)) {
                if (mx & (1 << (11 - bit))) countx = 1;
                if (my & (1 << (11 - bit))) county = 1;
            }
        }
        c = (c + 1) & 0x0fff;

        if (countx) {
            if (!(ypos_ & 0x400) && ((xpos_ ^ (xpos_ + dx)) & 0x400)) {
                if ((xpos_ + dx) & 0x400) {
                    draw_to(xpos_, ypos_, intensity_);
                } else {
                    draw_to((xpos_ + dx) & 0x0fff, ypos_, 0);
                }
            }
            xpos_ = (xpos_ + dx) & 0x0fff;
        }
        if (county) {
            if (!(xpos_ & 0x400) && ((ypos_ ^ (ypos_ + dy)) & 0x400)) {
                if ((ypos_ + dy) & 0x400) {
                    draw_to(xpos_, ypos_, intensity_);
                } else {
                    draw_to(xpos_, (ypos_ + dy) & 0x0fff, 0);
                }
            }
            ypos_ = (ypos_ + dy) & 0x0fff;
        }
    }

    draw_to(xpos_, ypos_, intensity_);
    return cycles;
}

int Dvg::handler_haltstrobe() {
    halt_ = op0();
    if (!op0()) {
        xpos_ = dvx_ & 0x0fff;
        ypos_ = dvy_ & 0x0fff;
        draw_to(xpos_, ypos_, 0);
    }
    return 0;
}

int Dvg::handler_latch0() {
    dvy_ &= 0x0f00;
    if (op_ == 0x0f) {
        handler_latch3();
    } else {
        dvy_ = uint16_t((dvy_ & 0x0f00) | data_);
    }
    pc_++;
    return 0;
}

int Dvg::handler_latch1() {
    dvy_ = uint16_t((dvy_ & 0xff) | ((data_ & 0x0f) << 8));
    op_ = uint8_t(data_ >> 4);
    if (op_ == 0x0f) {
        dvx_ &= 0x0f00;
        dvy_ &= 0x0f00;
    }
    return 0;
}

int Dvg::handler_latch2() {
    dvx_ &= 0x0f00;
    if (op_ != 0x0f) dvx_ = uint16_t((dvx_ & 0x0f00) | data_);
    if (op1() && op3()) scale_ = intensity_;
    pc_++;
    return 0;
}

int Dvg::handler_latch3() {
    dvx_ = uint16_t((dvx_ & 0xff) | ((data_ & 0x0f) << 8));
    intensity_ = uint8_t(data_ >> 4);
    return 0;
}

int Dvg::map_x(int x) {
    if (x > 1024) return 400;
    if (x < 0) return 0;
    return int(x / 2.56);
}

int Dvg::map_y(int y) {
    if (y > 1024) return 400;
    if (y < 0) return 0;
    return int((1024 - y) / 2.56);
}

void Dvg::vg_flush() {
    display_.clear();
    if (nvect_ <= 0) return;

    int xs = vectbuf_[0].x;
    int ys = vectbuf_[0].y;
    const int y_off = int(x_desp_);
    for (int i = 0; i < nvect_; i++) {
        const int xe = vectbuf_[size_t(i)].x;
        const int ye = vectbuf_[size_t(i)].y;
        if (vectbuf_[size_t(i)].intensity > 0) {
            Line line;
            line.x0 = map_x(xs);
            line.y0 = map_y(ys) - y_off;
            line.x1 = map_x(xe);
            line.y1 = map_y(ye) - y_off;
            line.intensity = vectbuf_[size_t(i)].intensity;
            display_.push_back(line);
        }
        xs = xe;
        ys = ye;
    }
    nvect_ = 0;
}

void Dvg::run_until_halt() {
    int extra = 0;
    for (int step = 0; step < 200000; step++) {
        state_latch_ = uint8_t((state_latch_ & 0x10) | (prom_[state_addr()] & 0x0f));
        if (state_latch_ & 0x08) {
            update_databus();
            switch (state_latch_ & 7) {
                case 0:
                    handler_dmapush();
                    break;
                case 1:
                    handler_dmald();
                    break;
                case 2:
                    handler_gostrobe();
                    break;
                case 3:
                    handler_haltstrobe();
                    break;
                case 4:
                    handler_latch0();
                    break;
                case 5:
                    handler_latch1();
                    break;
                case 6:
                    handler_latch2();
                    break;
                case 7:
                    handler_latch3();
                    break;
            }
        }
        if (halt_ && (state_latch_ & 0x10) == 0) sync_halt_ = 1;
        state_latch_ = uint8_t((halt_ << 4) | (state_latch_ & 0x0f));
        if (sync_halt_) {
            extra++;
            if (extra > 24) break;
        }
    }
}

void Dvg::go() {
    dvy_ = 0;
    op_ = 0;
    nvect_ = 0;
    halt_ = 0;
    sync_halt_ = 0;
    run_until_halt();
    vg_flush();
}

}  // namespace dsp
