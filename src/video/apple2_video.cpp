#include "video/apple2_video.h"

#include <algorithm>

namespace dsp {
namespace {

constexpr uint32_t kBlack = 0xFF000000;
constexpr uint32_t kWhite = 0xFFFFFFFF;
constexpr uint32_t kGreen = 0xFF20C000;
constexpr uint32_t kPurple = 0xFFC000FF;
constexpr uint32_t kOrange = 0xFFF06000;
constexpr uint32_t kBlue = 0xFF2020FF;

const uint32_t kLores[16] = {
    0xFF000000, 0xFF9D09AA, 0xFF2B2BE3, 0xFFDB42FF, 0xFF119D00, 0xFF808080, 0xFF0EA1FF, 0xFFAAAAFF,
    0xFF885500, 0xFFFF6600, 0xFFAAAAAA, 0xFFFF99FF, 0xFF22FF00, 0xFFFFFF00, 0xFF44FF99, 0xFFFFFFFF,
};

}  // namespace

uint16_t Apple2Video::text_address(int row, int col, bool page2) {
    const uint16_t base = page2 ? 0x0800 : 0x0400;
    return static_cast<uint16_t>(base + ((row & 7) << 7) + ((row >> 3) * 40) + col);
}

uint16_t Apple2Video::hires_address(int y, int col, bool page2) {
    const uint16_t base = page2 ? 0x4000 : 0x2000;
    return static_cast<uint16_t>(base + ((y & 7) << 10) + (((y >> 3) & 7) << 7) + ((y >> 6) * 40) +
                                 col);
}

void Apple2Video::put(uint32_t* dest, int x, int y, uint32_t color, int xscale) const {
    const int dx = x * xscale;
    const int dy = y * 2;
    if (dx < 0 || dy < 0) {
        return;
    }
    for (int sy = 0; sy < 2; sy++) {
        const int py = dy + sy;
        if (py >= kHeight) {
            continue;
        }
        uint32_t* row = dest + py * kWidth;
        for (int sx = 0; sx < xscale; sx++) {
            const int px = dx + sx;
            if (px >= 0 && px < kWidth) {
                row[px] = color;
            }
        }
    }
}

uint8_t Apple2Video::glyph_row(const uint8_t* chargen, int chargen_size, uint8_t code, int row,
                               bool* invert) const {
    *invert = false;
    if (chargen == nullptr || chargen_size <= 0) {
        return 0x7F;
    }

    int index = code;
    const bool flash = ((flash_phase / 15) & 1) != 0;
    if (!iie || chargen_size < 0x1000) {
        if (code < 0x40) {
            *invert = true;
            index = code & 0x3F;
        } else if (code < 0x80) {
            *invert = flash && !altcharset;
            index = code & 0x3F;
        } else {
            index = code & 0x3F;
        }
        const int addr = (index * 8 + (row & 7)) % chargen_size;
        return chargen[addr];
    }

    // IIe 4K character generator: two 2K banks (primary / MouseText).
    if (code < 0x40) {
        *invert = true;
        index = code;
    } else if (code < 0x80) {
        if (altcharset) {
            index = code;  // MouseText in the upper 2K, no flash
        } else {
            *invert = flash;
            index = code & 0x3F;
        }
    } else {
        index = code;
    }
    int addr = (index & 0xFF) * 8 + (row & 7);
    if (altcharset && code >= 0x40 && code < 0x80) {
        addr += 0x800;
    }
    if (addr >= chargen_size) {
        addr %= chargen_size;
    }
    return chargen[addr];
}

void Apple2Video::draw_text_row(uint32_t* dest, int row, const uint8_t* main, const uint8_t* aux,
                                const uint8_t* chargen, int chargen_size, bool use80) const {
    const int cols = use80 ? kTextCols80 : kTextCols40;
    const int xscale = use80 ? 1 : 2;
    for (int col = 0; col < cols; col++) {
        uint8_t code = 0xA0;
        if (use80) {
            const int pair = col / 2;
            const uint16_t addr = text_address(row, pair, page2);
            if ((col & 1) == 0) {
                code = (aux != nullptr) ? aux[addr] : 0xA0;
            } else {
                code = main[addr];
            }
        } else {
            code = main[text_address(row, col, page2)];
        }
        for (int y = 0; y < 8; y++) {
            bool invert = false;
            const uint8_t bits = glyph_row(chargen, chargen_size, code, y, &invert);
            for (int x = 0; x < 7; x++) {
                const bool on = ((bits >> x) & 1) != 0;
                const uint32_t color = (on ^ invert) ? kWhite : kBlack;
                put(dest, col * 7 + x, row * 8 + y, color, xscale);
            }
        }
    }
}

void Apple2Video::draw_lores(uint32_t* dest, const uint8_t* ram, int rows) const {
    const int text_rows = (rows + 7) / 8;
    for (int row = 0; row < text_rows; row++) {
        for (int col = 0; col < kTextCols40; col++) {
            const uint8_t cell = ram[text_address(row, col, page2)];
            const uint32_t top = kLores[cell & 0x0F];
            const uint32_t bot = kLores[(cell >> 4) & 0x0F];
            for (int y = 0; y < 8; y++) {
                if (row * 8 + y >= rows) {
                    break;
                }
                const uint32_t color = (y < 4) ? top : bot;
                for (int x = 0; x < 7; x++) {
                    put(dest, col * 7 + x, row * 8 + y, color, 2);
                }
            }
        }
    }
}

void Apple2Video::draw_hires(uint32_t* dest, const uint8_t* ram, int rows) const {
    for (int y = 0; y < rows; y++) {
        int prev = 0;
        for (int col = 0; col < 40; col++) {
            const uint8_t b = ram[hires_address(y, col, page2)];
            const int palette = (b >> 7) & 1;
            for (int bit = 0; bit < 7; bit++) {
                const int pix = (b >> bit) & 1;
                const int next_bit = (bit < 6) ? ((b >> (bit + 1)) & 1)
                                               : ((col < 39) ? (ram[hires_address(y, col + 1, page2)] & 1) : 0);
                const int x = col * 7 + bit;
                uint32_t color = kBlack;
                if (pix) {
                    if (prev || next_bit) {
                        color = kWhite;
                    } else if (palette) {
                        color = (x & 1) ? kBlue : kOrange;
                    } else {
                        color = (x & 1) ? kPurple : kGreen;
                    }
                }
                put(dest, x, y, color, 2);
                prev = pix;
            }
        }
    }
}

void Apple2Video::draw_dhires(uint32_t* dest, const uint8_t* main, const uint8_t* aux, int rows) const {
    static const uint32_t kDhgr[16] = {
        0xFF000000, 0xFFDD0033, 0xFF000099, 0xFFDD22DD, 0xFF007722, 0xFF555555, 0xFF2222FF, 0xFF66AAFF,
        0xFF885500, 0xFFFF6600, 0xFFAAAAAA, 0xFFFF99AA, 0xFF00DD00, 0xFFFFFF00, 0xFF44FF99, 0xFFFFFFFF,
    };
    for (int y = 0; y < rows; y++) {
        int bits = 0;
        int count = 0;
        auto push = [&](uint8_t value) {
            for (int i = 0; i < 7; i++) {
                bits |= ((value >> i) & 1) << count;
                count++;
            }
        };
        int x = 0;
        for (int col = 0; col < 40; col++) {
            const uint16_t addr = hires_address(y, col, page2);
            push(aux != nullptr ? aux[addr] : 0);
            push(main[addr]);
            while (count >= 4 && x < 560) {
                const int nibble = bits & 0x0F;
                bits >>= 4;
                count -= 4;
                put(dest, x, y, kDhgr[nibble], 1);
                x++;
                if (x < 560) {
                    put(dest, x, y, kDhgr[nibble], 1);
                    x++;
                }
            }
        }
    }
}

void Apple2Video::render(uint32_t* dest, const uint8_t* main, const uint8_t* aux,
                         const uint8_t* chargen, int chargen_size) const {
    std::fill(dest, dest + kWidth * kHeight, kBlack);
    if (main == nullptr) {
        return;
    }

    const int mixed_text_from = mixed ? 20 : 24;
    int gfx_rows = 192;
    if (text) {
        gfx_rows = 0;
    } else if (mixed) {
        gfx_rows = 160;
    }

    if (text) {
        for (int row = 0; row < kTextRows; row++) {
            draw_text_row(dest, row, main, aux, chargen, chargen_size, col80);
        }
        return;
    }

    if (hires) {
        if (dhires && col80 && iie) {
            draw_dhires(dest, main, aux, gfx_rows);
        } else {
            draw_hires(dest, main, gfx_rows);
        }
    } else {
        draw_lores(dest, main, gfx_rows);
    }

    if (mixed) {
        for (int row = mixed_text_from; row < kTextRows; row++) {
            draw_text_row(dest, row, main, aux, chargen, chargen_size, col80);
        }
    }
}

}  // namespace dsp
