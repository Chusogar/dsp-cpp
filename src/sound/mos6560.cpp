#include "sound/mos6560.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dsp {
namespace {

constexpr int kNoiseBufferSizeSec = 5;

}  // namespace

Mos6560::Mos6560(Variant variant) : variant_(variant) {
    if (variant_ == Variant::Pal6561) {
        clock_ = kPalClock;
        total_xsize_ = kPalXSize;
        total_ysize_ = kPalYSize;
        total_lines_ = kPalLines;
        cycles_per_line_ = kPalCyclesPerLine;
        vis_x_ = kPalVisX;
        vis_y_ = kPalVisY;
        vis_w_ = kPalVisW;
        vis_h_ = kPalVisH;
    } else {
        clock_ = kNtscClock;
        total_xsize_ = kNtscXSize;
        total_ysize_ = kNtscYSize;
        total_lines_ = kNtscLines;
        cycles_per_line_ = kNtscCyclesPerLine;
        vis_x_ = kNtscVisX;
        vis_y_ = kNtscVisY;
        vis_w_ = kNtscVisW;
        vis_h_ = kNtscVisH;
    }
    bitmap_.assign(size_t(total_xsize_ * total_ysize_), kPalette[0]);
    sound_start();
    reset();
}

void Mos6560::sound_start() {
    const int noise_freq_max = int(clock_ / 32);
    noisesize_ = noise_freq_max * kNoiseBufferSizeSec;
    if (noisesize_ < 1) noisesize_ = 1;
    noise_.assign(size_t(noisesize_), 0);
    int noiseshift = 0x7ffff8;
    for (int i = 0; i < noisesize_; i++) {
        int8_t data = 0;
        if (noiseshift & 0x400000) data = int8_t(data | 0x80);
        if (noiseshift & 0x100000) data = int8_t(data | 0x40);
        if (noiseshift & 0x010000) data = int8_t(data | 0x20);
        if (noiseshift & 0x002000) data = int8_t(data | 0x10);
        if (noiseshift & 0x000800) data = int8_t(data | 0x08);
        if (noiseshift & 0x000080) data = int8_t(data | 0x04);
        if (noiseshift & 0x000010) data = int8_t(data | 0x02);
        if (noiseshift & 0x000004) data = int8_t(data | 0x01);
        noise_[size_t(i)] = data;
        if (((noiseshift & 0x400000) == 0) != ((noiseshift & 0x002000) == 0))
            noiseshift = (noiseshift << 1) | 1;
        else
            noiseshift <<= 1;
    }

    const int tone_freq_min = int(clock_ / 256 / 128);
    tonesize_ = tone_freq_min > 0 ? (kSampleRate / tone_freq_min) : 1;
    if (tonesize_ < 1) tonesize_ = 1;
    tone_.assign(size_t(tonesize_), 0);
    for (int i = 0; i < tonesize_; i++) {
        tone_[size_t(i)] =
            int16_t(std::sin(2.0 * M_PI * double(i) / double(tonesize_)) * 127.0 + 0.5);
    }
}

void Mos6560::reset() {
    rasterline_ = 0;
    lastline_ = 0;
    reg_.fill(0);
    charheight_ = 8;
    matrix8x16_ = 0;
    inverted_ = 0;
    chars_x_ = 0;
    chars_y_ = 0;
    xsize_ = 0;
    ysize_ = 0;
    xpos_ = 0;
    ypos_ = 0;
    chargenaddr_ = 0;
    videoaddr_ = 0;
    backgroundcolor_ = 0;
    framecolor_ = 0;
    helpercolor_ = 0;
    mono_[0] = mono_[1] = 0;
    monoinverted_[0] = monoinverted_[1] = 0;
    multi_[0] = multi_[1] = multi_[2] = multi_[3] = 0;
    multiinverted_[0] = multiinverted_[1] = multiinverted_[2] = multiinverted_[3] = 0;
    last_data_ = 0;
    tone1pos_ = tone2pos_ = tone3pos_ = 0;
    tone1samples_ = tone2samples_ = tone3samples_ = 1;
    noisepos_ = 0;
    noisesamples_ = 1;
    std::fill(bitmap_.begin(), bitmap_.end(), kPalette[0]);
}

void Mos6560::put_pix(int y, int x, uint32_t argb) {
    if (y < 0 || y >= total_ysize_ || x < 0 || x >= total_xsize_) return;
    bitmap_[size_t(y * total_xsize_ + x)] = argb;
}

uint8_t Mos6560::read_videoram(uint16_t offset) {
    last_data_ = mem_ ? mem_(uint16_t(offset & 0x3fff)) : 0;
    return last_data_;
}

uint8_t Mos6560::read_colorram(uint16_t offset) {
    return color_ ? color_(uint16_t(offset & 0x3fff)) : uint8_t(0);
}

void Mos6560::draw_character(int ybegin, int yend, int ch, int yoff, int xoff,
                             const uint16_t* color) {
    for (int y = ybegin; y <= yend; y++) {
        const int code =
            read_videoram(uint16_t((chargenaddr_ + ch * charheight_ + y) & 0x3fff));
        for (int b = 0; b < 8; b++) {
            const int bit = (code >> (7 - b)) & 1;
            put_pix(y + yoff, xoff + b, kPalette[color[bit] & 0xF]);
        }
    }
}

void Mos6560::draw_character_multi(int ybegin, int yend, int ch, int yoff, int xoff,
                                   const uint16_t* color) {
    for (int y = ybegin; y <= yend; y++) {
        const int code =
            read_videoram(uint16_t((chargenaddr_ + ch * charheight_ + y) & 0x3fff));
        for (int p = 0; p < 4; p++) {
            const int idx = (code >> (6 - 2 * p)) & 3;
            const uint32_t c = kPalette[color[idx] & 0xF];
            put_pix(y + yoff, xoff + p * 2, c);
            put_pix(y + yoff, xoff + p * 2 + 1, c);
        }
    }
}

void Mos6560::drawlines(int first, int last) {
    lastline_ = last;
    if (first >= last) return;

    int line = first;
    for (; (line < ypos_) && (line < last); line++) {
        for (int j = 0; j < total_xsize_; j++) put_pix(line, j, kPalette[framecolor_ & 0xF]);
    }

    for (int vline = line - ypos_; (line < last) && (line < ypos_ + ysize_);) {
        int offs, yoff, ybegin, yend;
        if (matrix8x16_) {
            offs = (vline >> 4) * chars_x_;
            yoff = (vline & ~0xf) + ypos_;
            ybegin = vline & 0xf;
            yend = (vline + 0xf < last - ypos_) ? 0xf : ((last - line) & 0xf) + ybegin;
        } else {
            offs = (vline >> 3) * chars_x_;
            yoff = (vline & ~7) + ypos_;
            ybegin = vline & 7;
            yend = (vline + 7 < last - ypos_) ? 7 : ((last - line) & 7) + ybegin;
        }

        if (xpos_ > 0) {
            for (int i = ybegin; i <= yend; i++)
                for (int j = 0; j < xpos_; j++) put_pix(yoff + i, j, kPalette[framecolor_ & 0xF]);
        }

        int xoff = xpos_;
        for (; (xoff < xpos_ + xsize_) && (xoff < total_xsize_); xoff += 8, offs++) {
            const int ch = read_videoram(uint16_t((videoaddr_ + offs) & 0x3fff));
            const int attr = read_colorram(uint16_t((videoaddr_ + offs) & 0x3fff)) & 0xf;

            if (inverted_) {
                if (attr & 8) {
                    multiinverted_[0] = uint16_t(attr & 7);
                    draw_character_multi(ybegin, yend, ch, yoff, xoff, multiinverted_);
                } else {
                    monoinverted_[0] = uint16_t(attr);
                    draw_character(ybegin, yend, ch, yoff, xoff, monoinverted_);
                }
            } else {
                if (attr & 8) {
                    multi_[2] = uint16_t(attr & 7);
                    draw_character_multi(ybegin, yend, ch, yoff, xoff, multi_);
                } else {
                    mono_[1] = uint16_t(attr);
                    draw_character(ybegin, yend, ch, yoff, xoff, mono_);
                }
            }
        }

        if (xoff < total_xsize_) {
            for (int i = ybegin; i <= yend; i++)
                for (int j = xoff; j < total_xsize_; j++)
                    put_pix(yoff + i, j, kPalette[framecolor_ & 0xF]);
        }

        if (matrix8x16_) {
            vline = (vline + 16) & ~0xf;
            line = vline + ypos_;
        } else {
            vline = (vline + 8) & ~7;
            line = vline + ypos_;
        }
    }

    for (; line < last; line++)
        for (int j = 0; j < total_xsize_; j++) put_pix(line, j, kPalette[framecolor_ & 0xF]);
}

void Mos6560::soundport_w(int offset, uint8_t data) {
    const int old = reg_[size_t(offset)];
    auto tone_freq = [this](int value_mul, uint8_t regv) -> int {
        const int value = value_mul * (128 - ((regv + 1) & 0x7f));
        if (value <= 0) return 1;
        const int f = int(clock_ / 32 / unsigned(value));
        return f > 0 ? f : 1;
    };
    auto noise_freq = [this](uint8_t regv) -> int {
        const int value = 32 * (128 - ((regv + 1) & 0x7f));
        if (value <= 0) return 1;
        const int f = int(clock_ / unsigned(value));
        return f > 0 ? f : 1;
    };

    switch (offset) {
    case 0x0a:
        reg_[size_t(offset)] = data;
        if (!(old & 0x80) && (data & 0x80)) {
            tone1pos_ = 0;
            tone1samples_ = kSampleRate / tone_freq(8, data);
            if (tone1samples_ <= 0) tone1samples_ = 1;
        }
        break;
    case 0x0b:
        reg_[size_t(offset)] = data;
        if (!(old & 0x80) && (data & 0x80)) {
            tone2pos_ = 0;
            tone2samples_ = kSampleRate / tone_freq(4, data);
            if (tone2samples_ <= 0) tone2samples_ = 1;
        }
        break;
    case 0x0c:
        reg_[size_t(offset)] = data;
        if (!(old & 0x80) && (data & 0x80)) {
            tone3pos_ = 0;
            tone3samples_ = kSampleRate / tone_freq(2, data);
            if (tone3samples_ <= 0) tone3samples_ = 1;
        }
        break;
    case 0x0d:
        reg_[size_t(offset)] = data;
        if (data & 0x80) {
            const int noise_freq_max = int(clock_ / 32);
            noisesamples_ = int(double(noise_freq_max) * double(kSampleRate) *
                                double(kNoiseBufferSizeSec) / double(noise_freq(data)));
            if (noisesamples_ <= 0) noisesamples_ = 1;
            if (double(noisepos_) / double(noisesamples_) >= 1.0) noisepos_ = 0;
        } else {
            noisepos_ = 0;
        }
        break;
    case 0x0e:
        reg_[size_t(offset)] = uint8_t((old & ~0x0f) | (data & 0x0f));
        break;
    default:
        break;
    }
}

void Mos6560::write(uint8_t offset, uint8_t data) {
    offset = uint8_t(offset & 0x0f);

    switch (offset) {
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x0d:
    case 0x0e:
        soundport_w(offset, data);
        break;
    default:
        break;
    }

    if (reg_[offset] != data) {
        switch (offset) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 5:
        case 0x0e:
        case 0x0f:
            drawlines(lastline_, rasterline_);
            break;
        default:
            break;
        }
        reg_[offset] = data;

        switch (offset) {
        case 0:
            xpos_ = (int(reg_[0]) & 0x7f) * 4;
            break;
        case 1:
            ypos_ = int(reg_[1]) * 2;
            break;
        case 2:
            chars_x_ = int(reg_[2]) & 0x7f;
            videoaddr_ = ((int(reg_[5]) & 0xf0) << 6) | ((int(reg_[2]) & 0x80) << 2);
            xsize_ = chars_x_ * 8;
            break;
        case 3:
            matrix8x16_ = data & 0x01;
            charheight_ = matrix8x16_ ? 16 : 8;
            chars_y_ = (int(reg_[3]) & 0x7e) >> 1;
            ysize_ = chars_y_ * charheight_;
            break;
        case 5:
            chargenaddr_ = (int(reg_[5]) & 0x0f) << 10;
            videoaddr_ = ((int(reg_[5]) & 0xf0) << 6) | ((int(reg_[2]) & 0x80) << 2);
            break;
        case 0x0e:
            helpercolor_ = uint16_t(reg_[0x0e] >> 4);
            multi_[3] = multiinverted_[3] = helpercolor_;
            break;
        case 0x0f:
            inverted_ = !(reg_[0x0f] & 8);
            framecolor_ = uint16_t(reg_[0x0f] & 0x07);
            backgroundcolor_ = uint16_t(reg_[0x0f] >> 4);
            multi_[1] = multiinverted_[1] = framecolor_;
            mono_[0] = monoinverted_[1] = multi_[0] = multiinverted_[2] = backgroundcolor_;
            break;
        default:
            break;
        }
    }
}

uint8_t Mos6560::read(uint8_t offset) {
    offset = uint8_t(offset & 0x0f);
    switch (offset) {
    case 3:
        return uint8_t(((rasterline_ & 1) << 7) | (reg_[offset] & 0x7f));
    case 4:
        drawlines(lastline_, rasterline_);
        return uint8_t((rasterline_ / 2) & 0xff);
    case 6:
    case 7:
        return reg_[offset];
    case 8:
        return pot_x_ ? pot_x_() : uint8_t(0xff);
    case 9:
        return pot_y_ ? pot_y_() : uint8_t(0xff);
    default:
        return reg_[offset];
    }
}

void Mos6560::update_line(int line) {
    if (line < 0) return;
    if (line == 0) {
        drawlines(lastline_, total_lines_);
        lastline_ = 0;
        rasterline_ = 0;
    }
    rasterline_ = line;
    if (line + 1 >= total_lines_) {
        drawlines(lastline_, total_lines_);
        lastline_ = 0;
    }
}

void Mos6560::blit_visible(uint32_t* dst) const {
    if (!dst) return;
    for (int y = 0; y < vis_h_; y++) {
        const int sy = vis_y_ + y;
        for (int x = 0; x < vis_w_; x++) {
            const int sx = vis_x_ + x;
            uint32_t c = kPalette[0];
            if (sy >= 0 && sy < total_ysize_ && sx >= 0 && sx < total_xsize_)
                c = bitmap_[size_t(sy * total_xsize_ + sx)];
            dst[size_t(y * vis_w_ + x)] = c;
        }
    }
}

int16_t Mos6560::update() {
    int v = 0;
    auto tone_freq = [this](int value_mul, uint8_t regv) -> int {
        const int value = value_mul * (128 - ((regv + 1) & 0x7f));
        if (value <= 0) return 1;
        const int f = int(clock_ / 32 / unsigned(value));
        return f > 0 ? f : 1;
    };

    if (reg_[0x0a] & 0x80) {
        const int idx = tone1pos_ * tonesize_ / tone1samples_;
        v += tone_[size_t(std::clamp(idx, 0, tonesize_ - 1))];
        tone1pos_++;
        if (tone1pos_ >= tone1samples_) {
            tone1pos_ = 0;
            tone1samples_ = kSampleRate / tone_freq(8, reg_[0x0a]);
            if (tone1samples_ <= 0) tone1samples_ = 1;
        }
    }
    if (reg_[0x0b] & 0x80) {
        const int idx = tone2pos_ * tonesize_ / tone2samples_;
        v += tone_[size_t(std::clamp(idx, 0, tonesize_ - 1))];
        tone2pos_++;
        if (tone2pos_ >= tone2samples_) {
            tone2pos_ = 0;
            tone2samples_ = kSampleRate / tone_freq(4, reg_[0x0b]);
            if (tone2samples_ <= 0) tone2samples_ = 1;
        }
    }
    if (reg_[0x0c] & 0x80) {
        const int idx = tone3pos_ * tonesize_ / tone3samples_;
        v += tone_[size_t(std::clamp(idx, 0, tonesize_ - 1))];
        tone3pos_++;
        if (tone3pos_ >= tone3samples_) {
            tone3pos_ = 0;
            tone3samples_ = kSampleRate / tone_freq(2, reg_[0x0c]);
            if (tone3samples_ <= 0) tone3samples_ = 1;
        }
    }
    if (reg_[0x0d] & 0x80) {
        const int idx = int(double(noisepos_) * double(noisesize_) / double(noisesamples_));
        v += noise_[size_t(std::clamp(idx, 0, noisesize_ - 1))];
        noisepos_++;
        if (double(noisepos_) / double(noisesamples_) >= 1.0) noisepos_ = 0;
    }

    v *= int(reg_[0x0e] & 0x0f);
    if (v > 8191) v = 8191;
    if (v < -8191) v = -8191;
    return int16_t(v);
}

}  // namespace dsp
