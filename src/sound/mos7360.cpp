#include "sound/mos7360.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr int kVRefreshInLines = 28;
constexpr int kTedYPos = 40;
constexpr int kXPos = 8;
constexpr int kYPos = 8;
constexpr int kNoiseBufferSizeSec = 5;

int rasterline_2_c16(int a, int lines) {
    return (a + lines - kTedYPos - 5) % lines;
}
int c16_2_rasterline(int a, int lines) {
    return (a + kTedYPos + 5) % lines;
}

}  // namespace

// Digitized TED palette from MAME mos7360 (16 hues × 8 luminances).
const uint32_t Mos7360::kPalette[128] = {
    0xFF060103, 0xFF2B2B2B, 0xFF670E0F, 0xFF003F42, 0xFF57006D, 0xFF004E00, 0xFF191C94, 0xFF383800,
    0xFF562000, 0xFF4B2800, 0xFF164800, 0xFF69072F, 0xFF004626, 0xFF062A80, 0xFF2A149B, 0xFF0B4900,

    0xFF000302, 0xFF3D3D3D, 0xFF751E20, 0xFF00504F, 0xFF6A1078, 0xFF045C00, 0xFF2A2AA3, 0xFF4C4700,
    0xFF692F00, 0xFF593800, 0xFF265600, 0xFF751541, 0xFF00583D, 0xFF153D8F, 0xFF3922AE, 0xFF195900,

    0xFF000304, 0xFF424242, 0xFF7B2820, 0xFF025659, 0xFF6F1A82, 0xFF0A6509, 0xFF3034A7, 0xFF505100,
    0xFF6E3600, 0xFF654000, 0xFF2C5C00, 0xFF7D1E45, 0xFF016145, 0xFF1C4599, 0xFF422DAD, 0xFF1D6200,

    0xFF050002, 0xFF56555A, 0xFF903C3B, 0xFF176D72, 0xFF872D99, 0xFF1F7B15, 0xFF4649C1, 0xFF666300,
    0xFF844C0D, 0xFF735500, 0xFF407200, 0xFF91335E, 0xFF19745C, 0xFF3259AE, 0xFF593FC3, 0xFF327600,

    0xFF020106, 0xFF847E85, 0xFFBB6768, 0xFF459696, 0xFFAF58C3, 0xFF4AA73E, 0xFF7373EC, 0xFF928D11,
    0xFFAF7832, 0xFFA18020, 0xFF6C9E12, 0xFFBA5F89, 0xFF469F83, 0xFF6185DD, 0xFF846CEF, 0xFF5DA329,

    0xFF02000A, 0xFFB2ACB3, 0xFFE99292, 0xFF6CC3C1, 0xFFD986F0, 0xFF79D176, 0xFF9DA1FF, 0xFFBDBE40,
    0xFFDCA261, 0xFFD1A94C, 0xFF93C83D, 0xFFE98AB1, 0xFF6FCDAB, 0xFF8AB4FF, 0xFFB29AFF, 0xFF88CB59,

    0xFF02000A, 0xFFC7CAC9, 0xFFFFACAC, 0xFF85D8E0, 0xFFF39CFF, 0xFF92EA8A, 0xFFB7BAFF, 0xFFD6D35B,
    0xFFF3BE79, 0xFFE6C565, 0xFFB0E057, 0xFFFFA4CF, 0xFF89E5C8, 0xFFA4CAFF, 0xFFCAB3FF, 0xFFA2E57A,

    0xFF010101, 0xFFFFFFFF, 0xFFFFF6F2, 0xFFD1FFFF, 0xFFFFE9FF, 0xFFDBFFD3, 0xFFFDFFFF, 0xFFFFFFA3,
    0xFFFFFFC1, 0xFFFFFFB2, 0xFFFCFFA2, 0xFFFFEEFF, 0xFFD1FFFF, 0xFFEBFFFF, 0xFFFFF8FF, 0xFFEDFFBC,
};

Mos7360::Mos7360(Variant variant) : variant_(variant) {
    if (variant_ == Variant::Pal) {
        clock_ = kPalClock;
        total_lines_ = kPalLines;
    } else {
        clock_ = kNtscClock;
        total_lines_ = kNtscLines;
    }
    // CPU cycles per scanline ≈ cpu_clock / (lines * refresh).
    const int refresh = (variant_ == Variant::Pal) ? 50 : 60;
    cycles_per_line_ = int(cpu_clock() / uint32_t(total_lines_ * refresh));
    if (cycles_per_line_ < 1) cycles_per_line_ = 1;

    bitmap_.assign(size_t(kVisW * kVisH), kPalette[0]);
    sound_start();
    reset();
}

void Mos7360::sound_start() {
    const int noise_freq_max = int(clock_ / 8);
    noisesize_ = noise_freq_max * kNoiseBufferSizeSec;
    if (noisesize_ < 1) noisesize_ = 1;
    noise_.assign(size_t(noisesize_), 0);
    int noiseshift = 0x7ffff8;
    for (int i = 0; i < noisesize_; i++) {
        uint8_t data = 0;
        if (noiseshift & 0x400000) data = uint8_t(data | 0x80);
        if (noiseshift & 0x100000) data = uint8_t(data | 0x40);
        if (noiseshift & 0x010000) data = uint8_t(data | 0x20);
        if (noiseshift & 0x002000) data = uint8_t(data | 0x10);
        if (noiseshift & 0x000800) data = uint8_t(data | 0x08);
        if (noiseshift & 0x000080) data = uint8_t(data | 0x04);
        if (noiseshift & 0x000010) data = uint8_t(data | 0x02);
        if (noiseshift & 0x000004) data = uint8_t(data | 0x01);
        noise_[size_t(i)] = data;
        if (((noiseshift & 0x400000) == 0) != ((noiseshift & 0x002000) == 0))
            noiseshift = (noiseshift << 1) | 1;
        else
            noiseshift <<= 1;
    }
}

void Mos7360::reset() {
    reg_.fill(0);
    last_data_ = 0;
    rom_ = 1;
    total_lines_ = (variant_ == Variant::Pal) ? kPalLines : kNtscLines;
    chargenaddr_ = 0;
    bitmapaddr_ = 0;
    videoaddr_ = 0;
    timer_active_.fill(false);
    timer_count_.fill(0);
    cursor1_ = 0;
    rasterline_ = 0;
    lastline_ = 0;
    raster_col_ = 0;
    frame_count_ = 0;
    x_begin_ = x_end_ = 0;
    y_begin_ = y_end_ = 0;
    std::fill(std::begin(c16_bitmap_), std::end(c16_bitmap_), 0);
    std::fill(std::begin(bitmapmulti_), std::end(bitmapmulti_), 0);
    std::fill(std::begin(mono_), std::end(mono_), 0);
    std::fill(std::begin(monoinversed_), std::end(monoinversed_), 0);
    std::fill(std::begin(multi_), std::end(multi_), 0);
    std::fill(std::begin(ecmcolor_), std::end(ecmcolor_), 0);
    std::fill(std::begin(colors_), std::end(colors_), 0);
    tone1pos_ = tone2pos_ = 0;
    tone1samples_ = tone2samples_ = 1;
    noisepos_ = 0;
    noisesamples_ = 1;
    std::fill(bitmap_.begin(), bitmap_.end(), kPalette[0]);
    if (irq_cb_) irq_cb_(false);
}

void Mos7360::set_interrupt(int mask) {
    reg_[9] = uint8_t(reg_[9] | mask);
    if ((reg_[0xa] & reg_[9] & 0x5e) != 0) {
        if ((reg_[9] & 0x80) == 0) {
            reg_[9] = uint8_t(reg_[9] | 0x80);
            if (irq_cb_) irq_cb_(true);
        }
    }
    reg_[9] = uint8_t(reg_[9] | mask);
}

void Mos7360::clear_interrupt(int mask) {
    reg_[9] = uint8_t(reg_[9] & ~mask);
    if ((reg_[9] & 0x80) != 0 && (reg_[9] & reg_[0xa] & 0x5e) == 0) {
        reg_[9] = uint8_t(reg_[9] & ~0x80);
        if (irq_cb_) irq_cb_(false);
    }
}

uint8_t Mos7360::read_ram(uint16_t offset) {
    last_data_ = mem_ ? mem_(offset, false) : uint8_t(0);
    return last_data_;
}

uint8_t Mos7360::read_rom(uint16_t offset) {
    last_data_ = mem_ ? mem_(offset, true) : uint8_t(0);
    return last_data_;
}

void Mos7360::put_pix(int y, int x, uint32_t argb) {
    if (y < 0 || y >= kVisH || x < 0 || x >= kVisW) return;
    bitmap_[size_t(y * kVisW + x)] = argb;
}

void Mos7360::draw_character(int ybegin, int yend, int ch, int yoff, int xoff,
                             const uint16_t* color) {
    const bool inrom = (reg_[0x12] & 0x04) != 0;
    for (int y = ybegin; y <= yend; y++) {
        const int code = inrom ? int(read_rom(uint16_t(chargenaddr_ + ch * 8 + y)))
                               : int(read_ram(uint16_t(chargenaddr_ + ch * 8 + y)));
        for (int b = 0; b < 8; b++) {
            const int bit = (code >> (7 - b)) & 1;
            put_pix(y + yoff, b + xoff, kPalette[color[bit] & 0x7f]);
        }
    }
}

void Mos7360::draw_character_multi(int ybegin, int yend, int ch, int yoff, int xoff) {
    const bool inrom = (reg_[0x12] & 0x04) != 0;
    for (int y = ybegin; y <= yend; y++) {
        const int code = inrom ? int(read_rom(uint16_t(chargenaddr_ + ch * 8 + y)))
                               : int(read_ram(uint16_t(chargenaddr_ + ch * 8 + y)));
        for (int p = 0; p < 4; p++) {
            const int idx = (code >> (6 - 2 * p)) & 3;
            const uint32_t c = kPalette[multi_[idx] & 0x7f];
            put_pix(y + yoff, p * 2 + xoff, c);
            put_pix(y + yoff, p * 2 + 1 + xoff, c);
        }
    }
}

void Mos7360::draw_bitmap(int ybegin, int yend, int ch, int yoff, int xoff) {
    for (int y = ybegin; y <= yend; y++) {
        const int code = int(read_ram(uint16_t(bitmapaddr_ + ch * 8 + y)));
        for (int b = 0; b < 8; b++) {
            const int bit = (code >> (7 - b)) & 1;
            put_pix(y + yoff, b + xoff, kPalette[c16_bitmap_[bit] & 0x7f]);
        }
    }
}

void Mos7360::draw_bitmap_multi(int ybegin, int yend, int ch, int yoff, int xoff) {
    for (int y = ybegin; y <= yend; y++) {
        const int code = int(read_ram(uint16_t(bitmapaddr_ + ch * 8 + y)));
        for (int p = 0; p < 4; p++) {
            const int idx = (code >> (6 - 2 * p)) & 3;
            const uint32_t c = kPalette[bitmapmulti_[idx] & 0x7f];
            put_pix(y + yoff, p * 2 + xoff, c);
            put_pix(y + yoff, p * 2 + 1 + xoff, c);
        }
    }
}

void Mos7360::draw_cursor(int ybegin, int yend, int yoff, int xoff, int color) {
    const uint32_t c = kPalette[color & 0x7f];
    for (int y = ybegin; y <= yend; y++) {
        for (int x = 0; x < 8; x++) put_pix(y + yoff, x + xoff, c);
    }
}

void Mos7360::recalc_display() {
    const bool lines25 = (reg_[6] & 0x08) != 0;
    const bool cols40 = (reg_[7] & 0x08) != 0;
    const bool reverseon = (reg_[7] & 0x80) == 0;
    const bool hires = (reg_[6] & 0x20) != 0;
    const bool multi = (reg_[7] & 0x10) != 0;

    if (lines25) {
        y_begin_ = 0;
        y_end_ = 200;
    } else {
        y_begin_ = 4;
        y_end_ = 196;
    }
    if (cols40) {
        x_begin_ = 0;
        x_end_ = 320;
    } else {
        x_begin_ = int(reg_[7] & 0x07);
        x_end_ = x_begin_ + 320;
    }

    if (reverseon && !hires && !multi)
        chargenaddr_ = (int(reg_[0x13]) & 0xfc) << 8;
    else
        chargenaddr_ = (int(reg_[0x13]) & 0xf8) << 8;
    bitmapaddr_ = (int(reg_[0x12]) & 0x38) << 10;
    videoaddr_ = (int(reg_[0x14]) & 0xf8) << 8;
}

void Mos7360::drawlines(int first, int last) {
    lastline_ = last;

    first -= kTedYPos;
    last -= kTedYPos;
    if (first >= last || last <= 0) return;
    if (first < 0) first = 0;

    const int framecolor = int(reg_[0x19] & 0x7f);
    const bool screenon = (reg_[6] & 0x10) != 0;

    if (!screenon) {
        for (int line = first; line < last && line < kVisH; line++) {
            for (int x = 0; x < kVisW; x++) put_pix(line, x, kPalette[framecolor]);
        }
        return;
    }

    const bool cols40 = (reg_[7] & 0x08) != 0;
    const int xbegin = cols40 ? kXPos : (kXPos + 7);
    const int xend = xbegin + (cols40 ? 320 : 304);

    const bool lines25 = (reg_[6] & 0x08) != 0;
    const int lines = lines25 ? 25 : 24;
    const int ysize = lines * 8;
    (void)ysize;

    int end = (last < y_begin_) ? last : (y_begin_ + kYPos);
    int line = first;
    for (; line < end && line < kVisH; line++) {
        for (int x = 0; x < kVisW; x++) put_pix(line, x, kPalette[framecolor]);
    }

    const int vertpos = int(reg_[6] & 0x07);
    int vline = lines25 ? (line - y_begin_ - kYPos) : (line - y_begin_ - kYPos + 8 - vertpos);

    end = (last < y_end_ + kYPos) ? last : (y_end_ + kYPos);

    const bool hires = (reg_[6] & 0x20) != 0;
    const bool ecm = (reg_[6] & 0x40) != 0;
    const bool multicolor = (reg_[7] & 0x10) != 0;
    const bool reverseon = (reg_[7] & 0x80) == 0;
    const int cursor1pos = int(reg_[0xd] | ((reg_[0xc] & 0x03) << 8));

    for (int ybegin, yend; line < end; vline = (vline + 8) & ~7, line = line + 1 + yend - ybegin) {
        int offs = (vline >> 3) * 40;
        ybegin = vline & 7;
        const int yoff = line - ybegin;
        yend = (yoff + 7 < end) ? 7 : (end - yoff - 1);

        for (int xoff = x_begin_ + kXPos; xoff < x_end_ + kXPos; xoff += 8, offs++) {
            if (hires) {
                const int ch = int(read_ram(uint16_t((videoaddr_ | 0x400) + offs)));
                const int attr = int(read_ram(uint16_t(videoaddr_ + offs)));
                const int c1 = ((ch >> 4) & 0xf) | (attr << 4);
                const int c2 = (ch & 0xf) | (attr & 0x70);
                bitmapmulti_[1] = c16_bitmap_[1] = uint16_t(c1 & 0x7f);
                bitmapmulti_[2] = c16_bitmap_[0] = uint16_t(c2 & 0x7f);
                if (multicolor)
                    draw_bitmap_multi(ybegin, yend, offs, yoff, xoff);
                else
                    draw_bitmap(ybegin, yend, offs, yoff, xoff);
            } else {
                const int ch = int(read_ram(uint16_t((videoaddr_ | 0x400) + offs)));
                const int attr = int(read_ram(uint16_t(videoaddr_ + offs)));
                if (ecm) {
                    const int e = ch >> 6;
                    ecmcolor_[0] = colors_[e];
                    ecmcolor_[1] = uint16_t(attr & 0x7f);
                    draw_character(ybegin, yend, ch & ~0xc0, yoff, xoff, ecmcolor_);
                } else if (multicolor) {
                    if (attr & 8) {
                        multi_[3] = uint16_t(attr & 0x77);
                        draw_character_multi(ybegin, yend, ch, yoff, xoff);
                    } else {
                        mono_[1] = uint16_t(attr & 0x7f);
                        draw_character(ybegin, yend, ch, yoff, xoff, mono_);
                    }
                } else if (cursor1_ && offs == cursor1pos) {
                    draw_cursor(ybegin, yend, yoff, xoff, attr & 0x7f);
                } else if (reverseon && (ch & 0x80)) {
                    monoinversed_[0] = uint16_t(attr & 0x7f);
                    if (cursor1_ && (attr & 0x80))
                        draw_cursor(ybegin, yend, yoff, xoff, monoinversed_[0]);
                    else
                        draw_character(ybegin, yend, ch & ~0x80, yoff, xoff, monoinversed_);
                } else {
                    mono_[1] = uint16_t(attr & 0x7f);
                    if (cursor1_ && (attr & 0x80))
                        draw_cursor(ybegin, yend, yoff, xoff, mono_[0]);
                    else
                        draw_character(ybegin, yend, ch, yoff, xoff, mono_);
                }
            }
        }

        for (int i = ybegin; i <= yend; i++) {
            for (int x = 0; x < xbegin; x++) put_pix(yoff + i, x, kPalette[framecolor]);
            for (int x = xend; x < kVisW; x++) put_pix(yoff + i, x, kPalette[framecolor]);
        }
    }

    end = (last < kVisH) ? last : kVisH;
    for (; line < end; line++) {
        for (int x = 0; x < kVisW; x++) put_pix(line, x, kPalette[framecolor]);
    }
}

int Mos7360::timer_period(int id) const {
    int v = 0;
    switch (id) {
    case 0: v = int(reg_[0] | (reg_[1] << 8)); break;
    case 1: v = int(reg_[2] | (reg_[3] << 8)); break;
    case 2: v = int(reg_[4] | (reg_[5] << 8)); break;
    default: break;
    }
    return v ? v : 0x10000;
}

void Mos7360::start_timer(int id) {
    timer_count_[size_t(id)] = timer_period(id);
    timer_active_[size_t(id)] = true;
}

void Mos7360::stop_timer(int id) {
    timer_active_[size_t(id)] = false;
}

void Mos7360::tick_timers(int cycles) {
    static constexpr uint8_t kIrq[3] = {0x08, 0x10, 0x40};
    if (cycles <= 0) return;
    for (int id = 0; id < 3; id++) {
        if (!timer_active_[size_t(id)]) continue;
        int& c = timer_count_[size_t(id)];
        c -= cycles;
        while (c <= 0) {
            set_interrupt(kIrq[id]);
            // Timer 1 reloads latched period; 2/3 reload 0x10000 (MAME behaviour).
            const int reload = (id == 0) ? timer_period(0) : 0x10000;
            c += reload;
            if (reload <= 0) break;
        }
    }
}

void Mos7360::soundport_w(int offset, uint8_t data) {
    auto tone_freq = [this](int reg) -> double {
        const int div = 1024 - reg;
        if (div <= 0) return 1.0;
        return double(clock_ / 4u / 8u) / double(div);
    };

    switch (offset) {
    case 0x0e:
    case 0x12:
        if (offset == 0x12)
            reg_[offset] = uint8_t((reg_[offset] & ~3) | (data & 3));
        else
            reg_[offset] = data;
        {
            const int t1 = int(reg_[0x0e] | ((reg_[0x12] & 3) << 8));
            const double f = tone_freq(t1);
            tone1samples_ = std::max(1, int(double(kSampleRate) / f));
        }
        break;
    case 0x0f:
    case 0x10:
        reg_[offset] = data;
        {
            const int t2 = int(reg_[0x0f] | ((reg_[0x10] & 3) << 8));
            const double f = tone_freq(t2);
            tone2samples_ = std::max(1, int(double(kSampleRate) / f));
            const double noise_f = double(clock_ / 8u) / double(std::max(1, 1024 - t2));
            noisesamples_ = std::max(
                1, int(double(clock_ / 8u) * double(kSampleRate) * kNoiseBufferSizeSec / noise_f));
            if ((reg_[0x11] & 0x40) == 0 || double(noisepos_) / double(noisesamples_) >= 1.0)
                noisepos_ = 0;
        }
        break;
    case 0x11:
        reg_[offset] = data;
        if ((data & 0x80) != 0 || (data & 0x10) == 0) tone1pos_ = 0;
        if ((data & 0x80) != 0 || (data & 0x20) == 0) tone2pos_ = 0;
        if ((data & 0x80) != 0 || (data & 0x40) == 0) noisepos_ = 0;
        break;
    default:
        break;
    }
}

int Mos7360::cs0_r(uint16_t offset) const {
    if (rom_ && offset >= 0x8000 && offset < 0xc000) return 0;
    return 1;
}

int Mos7360::cs1_r(uint16_t offset) const {
    if (rom_ && ((offset >= 0xc000 && offset < 0xfd00) || offset >= 0xff20)) return 0;
    return 1;
}

uint8_t Mos7360::read(uint16_t offset, int* cs0, int* cs1) {
    uint8_t val = last_data_;
    if (cs0) *cs0 = cs0_r(offset);
    if (cs1) *cs1 = cs1_r(offset);

    switch (offset) {
    case 0xff00:
        val = timer_active_[0] ? uint8_t(timer_count_[0] & 0xff) : reg_[0];
        break;
    case 0xff01:
        val = timer_active_[0] ? uint8_t((timer_count_[0] >> 8) & 0xff) : reg_[1];
        break;
    case 0xff02:
        val = timer_active_[1] ? uint8_t(timer_count_[1] & 0xff) : reg_[2];
        break;
    case 0xff03:
        val = timer_active_[1] ? uint8_t((timer_count_[1] >> 8) & 0xff) : reg_[3];
        break;
    case 0xff04:
        val = timer_active_[2] ? uint8_t(timer_count_[2] & 0xff) : reg_[4];
        break;
    case 0xff05:
        val = timer_active_[2] ? uint8_t((timer_count_[2] >> 8) & 0xff) : reg_[5];
        break;
    case 0xff07:
        val = uint8_t(reg_[7] & ~0x40);
        if (variant_ == Variant::Ntsc) val = uint8_t(val | 0x40);
        break;
    case 0xff13:
        val = uint8_t(reg_[0x13] & ~1);
        if (rom_) val = uint8_t(val | 1);
        break;
    case 0xff1c:
        drawlines(lastline_, rasterline_);
        val = uint8_t(((rasterline_2_c16(rasterline_, total_lines_) & 0x100) >> 8) | 0xfe);
        break;
    case 0xff1d:
        drawlines(lastline_, rasterline_);
        val = uint8_t(rasterline_2_c16(rasterline_, total_lines_) & 0xff);
        break;
    case 0xff1e:
        val = uint8_t(raster_col_ / 2);
        break;
    case 0xff1f:
        val = uint8_t(((rasterline_ & 7) << 4) | (reg_[0x1f] & 0x0f));
        break;
    case 0xff06:
    case 0xff08:
    case 0xff09:
    case 0xff0a:
    case 0xff0b:
    case 0xff0c:
    case 0xff0d:
    case 0xff0e:
    case 0xff0f:
    case 0xff10:
    case 0xff11:
    case 0xff12:
    case 0xff14:
    case 0xff15:
    case 0xff16:
    case 0xff17:
    case 0xff18:
    case 0xff19:
    case 0xff1a:
    case 0xff1b:
        val = reg_[offset & 0x1f];
        break;
    default:
        break;
    }
    return val;
}

void Mos7360::write(uint16_t offset, uint8_t data, int* cs0, int* cs1) {
    if (cs0) *cs0 = cs0_r(offset);
    if (cs1) *cs1 = cs1_r(offset);

    switch (offset) {
    case 0xff0e:
    case 0xff0f:
    case 0xff10:
    case 0xff11:
    case 0xff12:
        soundport_w(int(offset & 0x1f), data);
        break;
    default:
        break;
    }

    switch (offset) {
    case 0xff00:
        reg_[0] = data;
        stop_timer(0);
        break;
    case 0xff01:
        reg_[1] = data;
        start_timer(0);
        break;
    case 0xff02:
        reg_[2] = data;
        stop_timer(1);
        break;
    case 0xff03:
        reg_[3] = data;
        start_timer(1);
        break;
    case 0xff04:
        reg_[4] = data;
        stop_timer(2);
        break;
    case 0xff05:
        reg_[5] = data;
        start_timer(2);
        break;
    case 0xff06:
        if (reg_[6] != data) {
            drawlines(lastline_, rasterline_);
            reg_[6] = data;
            recalc_display();
        }
        break;
    case 0xff07:
        if (reg_[7] != data) {
            drawlines(lastline_, rasterline_);
            reg_[7] = data;
            recalc_display();
        }
        break;
    case 0xff08:
        reg_[8] = key_ ? key_(data) : uint8_t(0xff);
        break;
    case 0xff09:
        if (data & 0x08) clear_interrupt(0x08);
        if (data & 0x10) clear_interrupt(0x10);
        if (data & 0x40) clear_interrupt(0x40);
        if (data & 0x02) clear_interrupt(0x02);
        break;
    case 0xff0a:
        reg_[0xa] = uint8_t(data | 0xa0);
        break;
    case 0xff0b:
        if (data != reg_[0xb]) {
            drawlines(lastline_, rasterline_);
            reg_[0xb] = data;
        }
        break;
    case 0xff0c:
    case 0xff0d:
        if (reg_[offset & 0x1f] != data) {
            drawlines(lastline_, rasterline_);
            reg_[offset & 0x1f] = data;
        }
        break;
    case 0xff12:
        if (reg_[0x12] != data) {
            drawlines(lastline_, rasterline_);
            // soundport_w already stored low 2 bits; merge high bits.
            reg_[0x12] = data;
            recalc_display();
        }
        break;
    case 0xff13:
        if (reg_[0x13] != data) {
            drawlines(lastline_, rasterline_);
            reg_[0x13] = data;
            recalc_display();
        }
        break;
    case 0xff14:
        if (reg_[0x14] != data) {
            drawlines(lastline_, rasterline_);
            reg_[0x14] = data;
            recalc_display();
        }
        break;
    case 0xff15:
        if (reg_[0x15] != data) {
            drawlines(lastline_, rasterline_);
            reg_[0x15] = data;
            monoinversed_[1] = mono_[0] = bitmapmulti_[0] = multi_[0] = colors_[0] =
                uint16_t(data & 0x7f);
        }
        break;
    case 0xff16:
        if (reg_[0x16] != data) {
            drawlines(lastline_, rasterline_);
            reg_[0x16] = data;
            bitmapmulti_[3] = multi_[1] = colors_[1] = uint16_t(data & 0x7f);
        }
        break;
    case 0xff17:
        if (reg_[0x17] != data) {
            drawlines(lastline_, rasterline_);
            reg_[0x17] = data;
            multi_[2] = colors_[2] = uint16_t(data & 0x7f);
        }
        break;
    case 0xff18:
        if (reg_[0x18] != data) {
            drawlines(lastline_, rasterline_);
            reg_[0x18] = data;
            colors_[3] = uint16_t(data & 0x7f);
        }
        break;
    case 0xff19:
        if (reg_[0x19] != data) {
            drawlines(lastline_, rasterline_);
            reg_[0x19] = data;
            colors_[4] = uint16_t(data & 0x7f);
        }
        break;
    case 0xff1c:
        reg_[0x1c] = data;
        break;
    case 0xff1f:
        reg_[0x1f] = data;
        break;
    case 0xff3e:
        rom_ = 1;
        break;
    case 0xff3f:
        rom_ = 0;
        break;
    case 0xff1a:
    case 0xff1b:
    case 0xff1d:
    case 0xff1e:
        reg_[offset & 0x1f] = data;
        break;
    default:
        break;
    }
}

void Mos7360::update_line(int line) {
    if (line < 0) return;

    if (line == 0) {
        drawlines(lastline_, total_lines_);
        lastline_ = 0;
        // Frame / cursor blink.
        if ((reg_[0x1f] & 0x0f) >= 0x0f) {
            cursor1_ ^= 1;
            reg_[0x1f] = uint8_t(reg_[0x1f] & ~0x0f);
            frame_count_ = 0;
        } else {
            reg_[0x1f] = uint8_t(reg_[0x1f] + 1);
        }
    }

    rasterline_ = line;
    raster_col_ = 0;
    tick_timers(cycles_per_line_);

    const int want = c16_2_rasterline(int(reg_[0xb] | ((reg_[0xa] & 1) << 8)), total_lines_);
    if (rasterline_ == want) {
        drawlines(lastline_, rasterline_);
        set_interrupt(2);
    }

    if (line + 1 >= total_lines_) {
        drawlines(lastline_, total_lines_);
        lastline_ = 0;
    }
}

void Mos7360::blit_visible(uint32_t* dst) const {
    if (!dst) return;
    std::memcpy(dst, bitmap_.data(), size_t(kVisW * kVisH) * sizeof(uint32_t));
}

int16_t Mos7360::update() {
    int v = 0;
    const bool tone_on = (reg_[0x11] & 0x80) == 0;
    const bool t1 = (reg_[0x11] & 0x10) != 0;
    const bool t2 = (reg_[0x11] & 0x20) != 0;
    const bool noise_on = (reg_[0x11] & 0x40) != 0;

    if (t1) {
        if (tone1pos_ <= tone1samples_ / 2 || !tone_on) v += 0x2ff;
        tone1pos_++;
        if (tone1pos_ > tone1samples_) tone1pos_ = 0;
    }
    if (t2 || noise_on) {
        if (t2) {
            if (tone2pos_ <= tone2samples_ / 2 || !tone_on) v += 0x2ff;
            tone2pos_++;
            if (tone2pos_ > tone2samples_) tone2pos_ = 0;
        } else if (!noise_.empty() && noisesamples_ > 0) {
            const int idx = int(double(noisepos_) * double(noisesize_) / double(noisesamples_));
            v += int(noise_[size_t(std::clamp(idx, 0, noisesize_ - 1))]);
            noisepos_++;
            if (double(noisepos_) / double(noisesamples_) >= 1.0) noisepos_ = 0;
        }
    }

    int a = int(reg_[0x11] & 0x0f);
    if (a > 8) a = 8;
    v = v * a;
    if (v > 32767) v = 32767;
    return int16_t(v);
}

}  // namespace dsp
