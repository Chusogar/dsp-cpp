#include "machine/lynx_suzy.h"

#include <algorithm>

namespace dsp {
namespace {

enum SpriteType {
    kBackground = 0,
    kBackgroundNoColl = 1,
    kBoundaryShadow = 2,
    kBoundary = 3,
    kNormalSprite = 4,
    kNoColl = 5,
    kXorSprite = 6,
    kShadow = 7
};

constexpr uint8_t kTiltAcumL = 0x02;
constexpr uint8_t kHoffL = 0x04;
constexpr uint8_t kVoffL = 0x06;
constexpr uint8_t kVidBasL = 0x08;
constexpr uint8_t kCollBasL = 0x0a;
constexpr uint8_t kScbNextL = 0x10;
constexpr uint8_t kSprDLineL = 0x12;
constexpr uint8_t kHPosStrtL = 0x14;
constexpr uint8_t kVPosStrtL = 0x16;
constexpr uint8_t kSprHSizL = 0x18;
constexpr uint8_t kSprVSizL = 0x1a;
constexpr uint8_t kStretchL = 0x1c;
constexpr uint8_t kTiltL = 0x1e;
constexpr uint8_t kCollOffL = 0x24;
constexpr uint8_t kVSizAcumL = 0x26;
constexpr uint8_t kHSizOffL = 0x28;
constexpr uint8_t kVSizOffL = 0x2a;
constexpr uint8_t kScbAdrL = 0x2c;
constexpr uint8_t kMathD = 0x52;
constexpr uint8_t kMathC = 0x53;
constexpr uint8_t kMathB = 0x54;
constexpr uint8_t kMathA = 0x55;
constexpr uint8_t kMathP = 0x56;
constexpr uint8_t kMathN = 0x57;
constexpr uint8_t kMathH = 0x60;
constexpr uint8_t kMathG = 0x61;
constexpr uint8_t kMathF = 0x62;
constexpr uint8_t kMathE = 0x63;
constexpr uint8_t kMathM = 0x6c;
constexpr uint8_t kMathL = 0x6d;
constexpr uint8_t kMathK = 0x6e;
constexpr uint8_t kMathJ = 0x6f;
constexpr uint8_t kSprCtl0 = 0x80;
constexpr uint8_t kSprCtl1 = 0x81;
constexpr uint8_t kSprColl = 0x82;
constexpr uint8_t kSuzyHRev = 0x88;
constexpr uint8_t kSuzyBusEn = 0x90;
constexpr uint8_t kSprGo = 0x91;
constexpr uint8_t kSprSys = 0x92;
constexpr uint8_t kJoystick = 0xb0;
constexpr uint8_t kSwitches = 0xb1;
constexpr uint8_t kRCart0 = 0xb2;
constexpr uint8_t kRCart1 = 0xb3;

constexpr uint8_t kColorMasks[4] = {0x01, 0x03, 0x07, 0x0f};
constexpr uint8_t kColorCounts[4] = {2, 4, 8, 16};

int16_t as_signed(uint16_t value) { return int16_t(value); }

}  // namespace

LynxSuzy::LynxSuzy() { reset(); }

void LynxSuzy::reset() {
    data_.fill(0);
    data_[kSuzyHRev] = 0x01;
    signed_math_ = false;
    accumulate_ = false;
    accumulate_overflow_ = false;
    sign_ab_ = 1;
    sign_cd_ = 1;
    screen_ = 0;
    colbuf_ = 0;
    colpos_ = 0;
    xoff_ = 0;
    yoff_ = 0;
    mode_ = 0;
    spr_coll_ = 0;
    spritenr_ = 0;
    x_pos_ = 0;
    y_pos_ = 0;
    width_ = 0x100;
    height_ = 0x100;
    tilt_acc_ = 0;
    height_acc_ = 0;
    width_offset_ = 0x80;
    height_offset_ = 0x80;
    stretch_ = 0;
    tilt_ = 0;
    color_.fill(0);
    bitmap_ = 0;
    use_rle_ = false;
    line_color_ = 0;
    spr_ctl0_ = 0;
    spr_ctl1_ = 0;
    scb_ = 0;
    scb_next_ = 0;
    sprite_collide_ = false;
    everon_ = false;
    fred_ = 0;
    memory_accesses_ = 0;
    no_collide_ = false;
    vstretch_ = false;
    lefthanded_ = false;
    busy_ = false;
    remaining_ = 0;
    joystick_ = 0;
    switches_ = 0;
}

uint8_t LynxSuzy::ram_read(uint16_t address) const {
    return ram_ ? ram_[address] : 0;
}

void LynxSuzy::ram_write(uint16_t address, uint8_t value) {
    if (ram_ && (address & 0xfffe) != 0xfff8) ram_[address] = value;
}

uint16_t LynxSuzy::ram_word(uint16_t address) const {
    return uint16_t(ram_read(address) | (uint16_t(ram_read(uint16_t(address + 1))) << 8));
}

void LynxSuzy::ram_write_nibble(uint16_t address, uint8_t nibble, bool high) {
    uint8_t value = ram_read(address);
    if (high) {
        value = uint8_t((value & 0x0f) | (nibble << 4));
    } else {
        value = uint8_t((value & 0xf0) | (nibble & 0x0f));
    }
    ram_write(address, value);
}

uint8_t LynxSuzy::ram_read_nibble(uint16_t address, bool high) const {
    const uint8_t value = ram_read(address);
    return high ? uint8_t(value >> 4) : uint8_t(value & 0x0f);
}

void LynxSuzy::tick(int cycles) {
    if (!busy_) return;
    remaining_ -= cycles;
    if (remaining_ <= 0) {
        remaining_ = 0;
        busy_ = false;
    }
}

void LynxSuzy::multiply() {
    uint16_t left = uint16_t(data_[kMathB] | (data_[kMathA] << 8));
    uint16_t right = uint16_t(data_[kMathD] | (data_[kMathC] << 8));
    uint32_t result = uint32_t(left) * uint32_t(right);
    accumulate_overflow_ = false;
    if (signed_math_) {
        if (sign_ab_ + sign_cd_ == 0) result = (~result) + 1;
    }
    data_[kMathH] = uint8_t(result);
    data_[kMathG] = uint8_t(result >> 8);
    data_[kMathF] = uint8_t(result >> 16);
    data_[kMathE] = uint8_t(result >> 24);
    if (accumulate_) {
        uint32_t accu = uint32_t(data_[kMathM] | (data_[kMathL] << 8) | (data_[kMathK] << 16) |
                                 (data_[kMathJ] << 24));
        uint32_t sum = accu + result;
        if (sum < result) accumulate_overflow_ = true;
        data_[kMathM] = uint8_t(sum);
        data_[kMathL] = uint8_t(sum >> 8);
        data_[kMathK] = uint8_t(sum >> 16);
        data_[kMathJ] = uint8_t(sum >> 24);
    }
}

void LynxSuzy::divide() {
    const uint32_t left = uint32_t(data_[kMathH] | (data_[kMathG] << 8) | (data_[kMathF] << 16) |
                                   (data_[kMathE] << 24));
    const uint16_t right = uint16_t(data_[kMathP] | (data_[kMathN] << 8));
    accumulate_overflow_ = false;
    uint32_t result = 0xffffffff;
    uint32_t mod = 0;
    if (right == 0) {
        accumulate_overflow_ = true;
    } else {
        result = left / right;
        mod = left % right;
    }
    data_[kMathD] = uint8_t(result);
    data_[kMathC] = uint8_t(result >> 8);
    data_[kMathB] = uint8_t(result >> 16);
    data_[kMathA] = uint8_t(result >> 24);
    data_[kMathM] = uint8_t(mod);
    data_[kMathL] = uint8_t(mod >> 8);
    data_[kMathK] = 0;
    data_[kMathJ] = 0;
}

void LynxSuzy::plot(int16_t x, int16_t y, uint8_t color) {
    everon_ = true;
    const uint16_t pixel = uint16_t(screen_ + y * 80 + x / 2);
    const uint16_t coll = uint16_t(colbuf_ + y * 80 + x / 2);
    const bool high = (x & 1) == 0;
    const bool collide = sprite_collide_ && !no_collide_;

    auto collide_nibble = [&](bool allow) {
        if (!collide || !allow) return;
        const uint8_t back = ram_read_nibble(coll, high);
        if (back > fred_) fred_ = back;
        ram_write_nibble(coll, spritenr_, high);
        memory_accesses_ += 2;
    };

    switch (mode_ & 7) {
        case kNormalSprite:
            if (color == 0) break;
            ram_write_nibble(pixel, color, high);
            memory_accesses_ += 1;
            collide_nibble(true);
            break;
        case kBoundary:
            if (color == 0) break;
            if (color != 0x0f) {
                ram_write_nibble(pixel, color, high);
                memory_accesses_ += 1;
            }
            collide_nibble(true);
            break;
        case kShadow:
            if (color == 0) break;
            ram_write_nibble(pixel, color, high);
            memory_accesses_ += 1;
            collide_nibble(color != 0x0e);
            break;
        case kBoundaryShadow:
            if (color == 0) break;
            if (color != 0x0f) {
                ram_write_nibble(pixel, color, high);
                memory_accesses_ += 1;
            }
            collide_nibble(color != 0x0e);
            break;
        case kBackground:
            ram_write_nibble(pixel, color, high);
            memory_accesses_ += 1;
            if (collide && color != 0x0e) {
                ram_write_nibble(coll, spritenr_, high);
                memory_accesses_ += 1;
            }
            break;
        case kBackgroundNoColl:
            ram_write_nibble(pixel, color, high);
            memory_accesses_ += 1;
            break;
        case kNoColl:
            if (color == 0) break;
            ram_write_nibble(pixel, color, high);
            memory_accesses_ += 1;
            break;
        case kXorSprite:
            if (color == 0) break;
            {
                uint8_t value = ram_read(pixel);
                if (high) {
                    value = uint8_t(value ^ (color << 4));
                } else {
                    value = uint8_t(value ^ color);
                }
                ram_write(pixel, value);
                memory_accesses_ += 2;
            }
            collide_nibble(color != 0x0e);
            break;
    }
}

void LynxSuzy::blit_packed(int16_t y, int xdir, int bpp, uint8_t mask) {
    const int next_line = ram_read(bitmap_);
    memory_accesses_ += 1;
    uint16_t width_acc = (xdir == 1) ? width_offset_ : 0;
    int bits = 0;
    uint16_t buffer = 0;
    int16_t xi = int16_t(x_pos_ - xoff_);
    for (int j = 1; j < next_line; j++) {
        buffer = uint16_t((buffer << 8) | ram_read(uint16_t(bitmap_ + j)));
        bits += 8;
        memory_accesses_ += 1;
        while (bits > bpp) {
            const uint8_t color = color_[(buffer >> (bits - bpp)) & mask];
            bits -= bpp;
            width_acc = uint16_t(width_acc + width_);
            for (int i = 0; i < (width_acc >> 8); i++, xi = int16_t(xi + xdir)) {
                if (xi >= 0 && xi < kScreenWidth) plot(xi, y, color);
            }
            width_acc &= 0xff;
        }
    }
}

void LynxSuzy::blit_rle(int16_t y, int xdir, int bpp, uint8_t mask) {
    const int next_line = ram_read(bitmap_);
    uint16_t width_acc = (xdir == 1) ? width_offset_ : 0;
    int bits = 0;
    int j = 0;
    uint32_t buffer = 0;
    int16_t xi = int16_t(x_pos_ - xoff_);
    for (;;) {
        if (bits < 5 + bpp) {
            j += 1;
            if (j >= next_line) return;
            bits += 8;
            buffer = (buffer << 8) | ram_read(uint16_t(bitmap_ + j));
            memory_accesses_ += 1;
        }
        const bool literal = ((buffer >> (bits - 1)) & 1) != 0;
        bits -= 1;
        int count = int((buffer >> (bits - 4)) & 0x0f);
        bits -= 4;
        if (literal) {
            for (; count >= 0; count--) {
                if (bits < bpp) {
                    j += 1;
                    if (j >= next_line) return;
                    bits += 8;
                    buffer = (buffer << 8) | ram_read(uint16_t(bitmap_ + j));
                    memory_accesses_ += 1;
                }
                const uint8_t color = color_[(buffer >> (bits - bpp)) & mask];
                bits -= bpp;
                width_acc = uint16_t(width_acc + width_);
                for (int i = 0; i < (width_acc >> 8); i++, xi = int16_t(xi + xdir)) {
                    if (xi >= 0 && xi < kScreenWidth) plot(xi, y, color);
                }
                width_acc &= 0xff;
            }
        } else {
            if (count == 0) return;
            if (bits < bpp) {
                j += 1;
                if (j >= next_line) return;
                bits += 8;
                buffer = (buffer << 8) | ram_read(uint16_t(bitmap_ + j));
                memory_accesses_ += 1;
            }
            const uint8_t color = color_[(buffer >> (bits - bpp)) & mask];
            bits -= bpp;
            for (; count >= 0; count--) {
                width_acc = uint16_t(width_acc + width_);
                for (int i = 0; i < (width_acc >> 8); i++, xi = int16_t(xi + xdir)) {
                    if (xi >= 0 && xi < kScreenWidth) plot(xi, y, color);
                }
                width_acc &= 0xff;
            }
        }
    }
}

void LynxSuzy::blit_lines() {
    everon_ = false;
    int xdir = 1;
    int ydir = 1;
    int flip = 0;
    switch (spr_ctl1_ & 3) {
        case 0:
            xdir = 1;
            ydir = 1;
            flip = 0;
            break;
        case 1:
            xdir = -1;
            ydir = 1;
            flip = 3;
            break;
        case 2:
            xdir = 1;
            ydir = -1;
            flip = 1;
            break;
        case 3:
            xdir = -1;
            ydir = -1;
            flip = 2;
            break;
    }
    if (spr_ctl0_ & 0x20) xdir = -xdir;  // HFLIP
    if (spr_ctl0_ & 0x10) ydir = -ydir;  // VFLIP
    height_acc_ = (ydir == 1) ? height_offset_ : 0;
    for (int16_t y = int16_t(y_pos_ - yoff_);;) {
        const int offset = ram_read(bitmap_);
        memory_accesses_ += 1;
        if (offset == 0) break;
        if (offset == 1) {
            switch (flip & 3) {
                case 0:
                case 2:
                    ydir = -ydir;
                    y_pos_ = int16_t(y_pos_ + ydir);
                    break;
                default:
                    xdir = -xdir;
                    x_pos_ = int16_t(x_pos_ + xdir);
                    break;
            }
            flip += 1;
            y = int16_t(y_pos_ - yoff_);
            height_acc_ = (ydir == 1) ? height_offset_ : 0;
            bitmap_ = uint16_t(bitmap_ + offset);
            continue;
        }
        height_acc_ = uint16_t(height_acc_ + height_);
        for (int j = 0; j < (height_acc_ >> 8); j++, y = int16_t(y + ydir)) {
            if (y >= 0 && y < kScreenHeight) {
                if (use_rle_) {
                    blit_rle(y, xdir, line_color_ + 1, kColorMasks[line_color_]);
                } else {
                    blit_packed(y, xdir, line_color_ + 1, kColorMasks[line_color_]);
                }
            }
            width_ = uint16_t(int16_t(width_) + stretch_);
            if (vstretch_) height_ = uint16_t(int16_t(height_) + stretch_);
            tilt_acc_ = int16_t(tilt_acc_ + tilt_);
            x_pos_ = int16_t(x_pos_ + (tilt_acc_ >> 8));
            tilt_acc_ = int16_t(tilt_acc_ & 0xff);
        }
        height_acc_ &= 0xff;
        bitmap_ = uint16_t(bitmap_ + offset);
    }
}

void LynxSuzy::blitter() {
    busy_ = true;
    memory_accesses_ = 0;
    while (scb_next_ & 0xff00) {
        stretch_ = 0;
        tilt_ = 0;
        tilt_acc_ = 0;
        scb_ = scb_next_;
        scb_next_ = ram_word(uint16_t(scb_ + 3));
        spr_ctl0_ = ram_read(scb_);
        spr_ctl1_ = ram_read(uint16_t(scb_ + 1));
        spr_coll_ = ram_read(uint16_t(scb_ + 2));
        memory_accesses_ += 5;
        const bool skip = (spr_ctl1_ & 0x04) != 0;
        if (!skip) {
            bitmap_ = ram_word(uint16_t(scb_ + 5));
            x_pos_ = as_signed(ram_word(uint16_t(scb_ + 7)));
            y_pos_ = as_signed(ram_word(uint16_t(scb_ + 9)));
            memory_accesses_ += 6;
            const int reload = (spr_ctl1_ >> 4) & 3;
            switch (reload) {
                case 3:
                    tilt_ = as_signed(ram_word(uint16_t(scb_ + 0x11)));
                    memory_accesses_ += 2;
                    [[fallthrough]];
                case 2:
                    stretch_ = as_signed(ram_word(uint16_t(scb_ + 0x0f)));
                    memory_accesses_ += 2;
                    [[fallthrough]];
                case 1:
                    width_ = ram_word(uint16_t(scb_ + 0x0b));
                    height_ = ram_word(uint16_t(scb_ + 0x0d));
                    memory_accesses_ += 4;
                    break;
            }
            if ((spr_ctl1_ & 0x08) == 0) {
                const uint8_t palette_offset =
                    reload != 0 ? uint8_t(0x0b + 2 * (reload + 1)) : uint8_t(0x0b);
                const uint8_t colors = kColorCounts[(spr_ctl0_ >> 6) & 3];
                for (int i = 0; i < colors / 2; i++) {
                    const uint8_t packed = ram_read(uint16_t(scb_ + palette_offset + i));
                    color_[size_t(i * 2)] = uint8_t(packed >> 4);
                    color_[size_t(i * 2 + 1)] = uint8_t(packed & 0x0f);
                    memory_accesses_ += 1;
                }
            }
        }
        if (!skip) {
            const uint16_t coll_off = uint16_t(data_[kCollOffL] | (data_[kCollOffL + 1] << 8));
            colpos_ = uint16_t(scb_ + coll_off);
            mode_ = uint8_t(spr_ctl0_ & 7);
            use_rle_ = (spr_ctl1_ & 0x80) == 0;
            line_color_ = uint8_t((spr_ctl0_ >> 6) & 3);
            sprite_collide_ = (spr_coll_ & 0x20) == 0;
            spritenr_ = uint8_t(spr_coll_ & 0x0f);
            fred_ = 0;
            blit_lines();
            if (sprite_collide_ && !no_collide_) {
                switch (mode_) {
                    case kBoundaryShadow:
                    case kBoundary:
                    case kNormalSprite:
                    case kXorSprite:
                    case kShadow:
                        ram_write(colpos_, fred_);
                        break;
                }
            }
            if (data_[kSprGo] & 0x04) {
                uint8_t value = ram_read(colpos_);
                if (everon_) {
                    value = uint8_t(value & 0x7f);
                } else {
                    value = uint8_t(value | 0x80);
                }
                ram_write(colpos_, value);
            }
        }
    }
    remaining_ = std::max(1, memory_accesses_);
}

uint8_t LynxSuzy::read(uint8_t offset) {
    auto word_lo = [](uint16_t value) { return uint8_t(value); };
    auto word_hi = [](uint16_t value) { return uint8_t(value >> 8); };
    switch (offset) {
        case kTiltAcumL:
            return uint8_t(tilt_acc_);
        case kTiltAcumL + 1:
            return uint8_t(uint16_t(tilt_acc_) >> 8);
        case kHoffL:
            return uint8_t(xoff_);
        case kHoffL + 1:
            return uint8_t(uint16_t(xoff_) >> 8);
        case kVoffL:
            return uint8_t(yoff_);
        case kVoffL + 1:
            return uint8_t(uint16_t(yoff_) >> 8);
        case kVidBasL:
            return word_lo(screen_);
        case kVidBasL + 1:
            return word_hi(screen_);
        case kCollBasL:
            return word_lo(colbuf_);
        case kCollBasL + 1:
            return word_hi(colbuf_);
        case kScbNextL:
            return word_lo(scb_next_);
        case kScbNextL + 1:
            return word_hi(scb_next_);
        case kSprDLineL:
            return word_lo(bitmap_);
        case kSprDLineL + 1:
            return word_hi(bitmap_);
        case kHPosStrtL:
            return uint8_t(x_pos_);
        case kHPosStrtL + 1:
            return uint8_t(uint16_t(x_pos_) >> 8);
        case kVPosStrtL:
            return uint8_t(y_pos_);
        case kVPosStrtL + 1:
            return uint8_t(uint16_t(y_pos_) >> 8);
        case kSprHSizL:
            return word_lo(width_);
        case kSprHSizL + 1:
            return word_hi(width_);
        case kSprVSizL:
            return word_lo(height_);
        case kSprVSizL + 1:
            return word_hi(height_);
        case kStretchL:
            return uint8_t(stretch_);
        case kStretchL + 1:
            return uint8_t(uint16_t(stretch_) >> 8);
        case kTiltL:
            return uint8_t(tilt_);
        case kTiltL + 1:
            return uint8_t(uint16_t(tilt_) >> 8);
        case kVSizAcumL:
            return word_lo(height_acc_);
        case kVSizAcumL + 1:
            return word_hi(height_acc_);
        case kHSizOffL:
            return word_lo(width_offset_);
        case kHSizOffL + 1:
            return word_hi(width_offset_);
        case kVSizOffL:
            return word_lo(height_offset_);
        case kVSizOffL + 1:
            return word_hi(height_offset_);
        case kScbAdrL:
            return word_lo(scb_);
        case kScbAdrL + 1:
            return word_hi(scb_);
        case kSuzyHRev:
            return 0x01;
        case kSprSys: {
            uint8_t value = 0;
            if (accumulate_overflow_) value |= 0x40;
            if (vstretch_) value |= 0x10;
            if (lefthanded_) value |= 0x08;
            if (busy_) value |= 0x01;
            return value;
        }
        case kJoystick: {
            uint8_t input = joystick_;
            if (lefthanded_) {
                uint8_t value = uint8_t(input & 0x0f);
                if (input & 0x80) value |= 0x40;
                if (input & 0x40) value |= 0x80;
                if (input & 0x20) value |= 0x10;
                if (input & 0x10) value |= 0x20;
                return value;
            }
            return input;
        }
        case kSwitches:
            return switches_;
        case kRCart0:
            return cart0_read_ ? cart0_read_() : 0;
        case kRCart1:
            return cart1_read_ ? cart1_read_() : 0;
        case kSprCtl0:
        case kSprCtl1:
        case kSprColl:
        case kSprGo:
        case kSuzyBusEn:
            return 0;
        default:
            return data_[offset];
    }
}

void LynxSuzy::write(uint8_t offset, uint8_t value) {
    data_[offset] = value;
    if (offset < 0x80 && (offset & 1) == 0) data_[offset + 1] = 0;

    auto set_lo = [&](uint16_t& word) { word = value; };
    auto set_hi = [&](uint16_t& word) { word = uint16_t((word & 0x00ff) | (uint16_t(value) << 8)); };
    auto set_slo = [&](int16_t& word) { word = int16_t(value); };
    auto set_shi = [&](int16_t& word) {
        word = int16_t((uint16_t(word) & 0x00ff) | (uint16_t(value) << 8));
    };

    switch (offset) {
        case kTiltAcumL:
            tilt_acc_ = int16_t(value);
            break;
        case kTiltAcumL + 1:
            set_shi(tilt_acc_);
            break;
        case kHoffL:
            xoff_ = int16_t(value);
            break;
        case kHoffL + 1:
            set_shi(xoff_);
            break;
        case kVoffL:
            yoff_ = int16_t(value);
            break;
        case kVoffL + 1:
            set_shi(yoff_);
            break;
        case kVidBasL:
            set_lo(screen_);
            break;
        case kVidBasL + 1:
            set_hi(screen_);
            break;
        case kCollBasL:
            set_lo(colbuf_);
            break;
        case kCollBasL + 1:
            set_hi(colbuf_);
            break;
        case kScbNextL:
            set_lo(scb_next_);
            break;
        case kScbNextL + 1:
            set_hi(scb_next_);
            break;
        case kSprDLineL:
            set_lo(bitmap_);
            break;
        case kSprDLineL + 1:
            set_hi(bitmap_);
            break;
        case kHPosStrtL:
            x_pos_ = int16_t(value);
            break;
        case kHPosStrtL + 1:
            set_shi(x_pos_);
            break;
        case kVPosStrtL:
            y_pos_ = int16_t(value);
            break;
        case kVPosStrtL + 1:
            set_shi(y_pos_);
            break;
        case kSprHSizL:
            set_lo(width_);
            break;
        case kSprHSizL + 1:
            set_hi(width_);
            break;
        case kSprVSizL:
            set_lo(height_);
            break;
        case kSprVSizL + 1:
            set_hi(height_);
            break;
        case kStretchL:
            set_slo(stretch_);
            break;
        case kStretchL + 1:
            set_shi(stretch_);
            break;
        case kTiltL:
            set_slo(tilt_);
            break;
        case kTiltL + 1:
            set_shi(tilt_);
            break;
        case kVSizAcumL:
            set_lo(height_acc_);
            break;
        case kVSizAcumL + 1:
            set_hi(height_acc_);
            break;
        case kHSizOffL:
            set_lo(width_offset_);
            break;
        case kHSizOffL + 1:
            set_hi(width_offset_);
            break;
        case kVSizOffL:
            set_lo(height_offset_);
            break;
        case kVSizOffL + 1:
            set_hi(height_offset_);
            break;
        case kScbAdrL:
            set_lo(scb_);
            break;
        case kScbAdrL + 1:
            set_hi(scb_);
            break;
        case kMathM:
            accumulate_overflow_ = false;
            break;
        case kMathC: {
            if (signed_math_) {
                uint16_t factor = uint16_t(data_[kMathD] | (data_[kMathC] << 8));
                if ((factor - 1) & 0x8000) {
                    uint16_t temp = uint16_t((factor ^ 0xffff) + 1);
                    sign_cd_ = -1;
                    data_[kMathD] = uint8_t(temp);
                    data_[kMathC] = uint8_t(temp >> 8);
                } else {
                    sign_cd_ = 1;
                }
            }
            break;
        }
        case kMathD:
            if (value) sign_cd_ = 1;
            break;
        case kMathA: {
            if (signed_math_) {
                uint16_t factor = uint16_t(data_[kMathB] | (data_[kMathA] << 8));
                if ((factor - 1) & 0x8000) {
                    uint16_t temp = uint16_t((factor ^ 0xffff) + 1);
                    sign_ab_ = -1;
                    data_[kMathB] = uint8_t(temp);
                    data_[kMathA] = uint8_t(temp >> 8);
                } else {
                    sign_ab_ = 1;
                }
            }
            multiply();
            break;
        }
        case kMathE:
            divide();
            break;
        case kSprCtl0:
            spr_ctl0_ = value;
            break;
        case kSprCtl1:
            spr_ctl1_ = value;
            break;
        case kSprColl:
            spr_coll_ = value;
            break;
        case kSprSys:
            signed_math_ = (value & 0x80) != 0;
            accumulate_ = (value & 0x40) != 0;
            no_collide_ = (value & 0x20) != 0;
            vstretch_ = (value & 0x10) != 0;
            lefthanded_ = (value & 0x08) != 0;
            break;
        case kSprGo:
            if ((value & 0x01) && data_[kSuzyBusEn]) blitter();
            break;
        case kRCart0:
            if (cart0_write_) cart0_write_(value);
            break;
        case kRCart1:
            if (cart1_write_) cart1_write_(value);
            break;
        default:
            break;
    }
}

}  // namespace dsp
