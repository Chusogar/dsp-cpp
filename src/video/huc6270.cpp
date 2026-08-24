#include "video/huc6270.h"

#include <algorithm>

namespace dsp {
namespace {

constexpr uint16_t kVramMask = HuC6270::kVramWords - 1;

// Sprite geometry from the CGX/CGY fields of the attribute word.
int sprite_width(uint16_t attr) { return ((attr >> 8) & 1) != 0 ? 32 : 16; }

int sprite_height(uint16_t attr) {
    switch ((attr >> 12) & 3) {
        case 0: return 16;
        case 1: return 32;
        default: return 64;
    }
}

}  // namespace

void HuC6270::reset() {
    vram_.fill(0);
    sat_.fill(0);
    regs_.fill(0);
    reg_index_ = 0;
    write_latch_ = 0;
    status_ = 0;
    irq_asserted_ = false;
    mawr_ = marr_ = cr_ = rcr_ = bxr_ = byr_ = mwr_ = 0;
    vpr_ = 0x0f02;
    vdw_ = 0x00ef;
    vcr_ = 0x0003;
    dcr_ = 0;
    hsr_ = 0x0202;
    hdr_ = 0x031f;
    sour_ = desr_ = lenr_ = dvssr_ = 0;
    bg_y_ = 0;
    display_line_ = 0;
    satb_pending_ = false;
    if (irq_handler_) irq_handler_(false);
}

uint16_t HuC6270::increment() const {
    switch ((cr_ >> 11) & 3) {
        case 0: return 1;
        case 1: return 32;
        case 2: return 64;
        default: return 128;
    }
}

void HuC6270::raise_irq(uint8_t flag) {
    status_ |= flag;
    update_irq();
}

void HuC6270::update_irq() {
    uint8_t enabled = 0;
    if ((cr_ & 0x01) != 0) enabled |= kStatusCollision;
    if ((cr_ & 0x02) != 0) enabled |= kStatusOverflow;
    if ((cr_ & 0x04) != 0) enabled |= kStatusRaster;
    if ((cr_ & 0x08) != 0) enabled |= kStatusVblank;
    if ((dcr_ & 0x01) != 0) enabled |= kStatusSatbDone;
    if ((dcr_ & 0x02) != 0) enabled |= kStatusDmaDone;
    const bool assert = (status_ & enabled) != 0;
    if (assert == irq_asserted_) return;
    irq_asserted_ = assert;
    if (irq_handler_) irq_handler_(assert);
}

uint8_t HuC6270::read(uint8_t offset) {
    switch (offset & 3) {
        case 0: {
            const uint8_t value = status_;
            status_ = 0;
            update_irq();
            return value;
        }
        case 2:
            return uint8_t(vram_[marr_ & kVramMask] & 0xff);
        case 3: {
            const uint8_t value = uint8_t(vram_[marr_ & kVramMask] >> 8);
            marr_ = uint16_t(marr_ + increment());
            return value;
        }
        default:
            return 0;
    }
}

void HuC6270::write(uint8_t offset, uint8_t value) {
    switch (offset & 3) {
        case 0:
            reg_index_ = value & 0x1f;
            break;
        case 2:
            write_latch_ = value;
            register_w(reg_index_, uint16_t((regs_[reg_index_] & 0xff00) | value), false);
            break;
        case 3:
            register_w(reg_index_, uint16_t((uint16_t(value) << 8) | write_latch_), true);
            break;
        default:
            break;
    }
}

void HuC6270::register_w(int index, uint16_t value, bool high_byte) {
    regs_[size_t(index)] = value;
    switch (index) {
        case 0x00:
            mawr_ = value;
            break;
        case 0x01:
            marr_ = value;
            break;
        case 0x02:
            // VWR only commits once the high byte arrives.
            if (high_byte) {
                vram_[mawr_ & kVramMask] = value;
                mawr_ = uint16_t(mawr_ + increment());
            }
            break;
        case 0x05:
            cr_ = value;
            update_irq();
            break;
        case 0x06:
            rcr_ = value & 0x3ff;
            break;
        case 0x07:
            bxr_ = value & 0x3ff;
            break;
        case 0x08:
            byr_ = value & 0x1ff;
            bg_y_ = byr_;
            break;
        case 0x09:
            mwr_ = value;
            break;
        case 0x0a:
            hsr_ = value;
            break;
        case 0x0b:
            hdr_ = value;
            break;
        case 0x0c:
            vpr_ = value;
            break;
        case 0x0d:
            vdw_ = value;
            break;
        case 0x0e:
            vcr_ = value;
            break;
        case 0x0f:
            dcr_ = value;
            update_irq();
            break;
        case 0x10:
            sour_ = value;
            break;
        case 0x11:
            desr_ = value;
            break;
        case 0x12:
            lenr_ = value;
            if (high_byte) vram_dma();
            break;
        case 0x13:
            dvssr_ = value;
            satb_pending_ = true;
            break;
        default:
            break;
    }
}

void HuC6270::vram_dma() {
    const int source_step = (dcr_ & 0x04) != 0 ? -1 : 1;
    const int dest_step = (dcr_ & 0x08) != 0 ? -1 : 1;
    int length = int(lenr_) + 1;
    while (length-- > 0) {
        vram_[desr_ & kVramMask] = vram_[sour_ & kVramMask];
        sour_ = uint16_t(sour_ + source_step);
        desr_ = uint16_t(desr_ + dest_step);
    }
    lenr_ = 0xffff;
    raise_irq(kStatusDmaDone);
}

void HuC6270::satb_dma() {
    for (int i = 0; i < int(sat_.size()); i++) {
        sat_[size_t(i)] = vram_[(dvssr_ + i) & kVramMask];
    }
    satb_pending_ = (dcr_ & 0x10) != 0;
    raise_irq(kStatusSatbDone);
}

bool HuC6270::scanline(int line, uint16_t* out, int width) {
    const int start = display_start();
    const int height = display_height();
    if (line == start) bg_y_ = byr_;
    if (line < start || line >= start + height) {
        if (line == start + height) {
            raise_irq(kStatusVblank);
            if (satb_pending_) satb_dma();
        }
        return false;
    }
    display_line_ = line - start;
    if (int(rcr_ & 0x3ff) - 64 == display_line_) raise_irq(kStatusRaster);
    width = std::min(width, kMaxWidth);
    render_background(out, width);
    render_sprites(display_line_, out, width);
    bg_y_ = uint16_t(bg_y_ + 1);
    return true;
}

void HuC6270::end_frame() {
    if (satb_pending_) satb_dma();
}

void HuC6270::render_background(uint16_t* out, int width) {
    if ((cr_ & 0x80) == 0) {
        std::fill(out, out + width, uint16_t(0));
        return;
    }
    int map_width = 32;
    switch ((mwr_ >> 4) & 3) {
        case 0: map_width = 32; break;
        case 1: map_width = 64; break;
        default: map_width = 128; break;
    }
    const int map_height = ((mwr_ >> 6) & 1) != 0 ? 64 : 32;
    const int y = bg_y_ & (map_height * 8 - 1);
    const int row = y & 7;
    const int tile_row = (y >> 3) * map_width;
    for (int px = 0; px < width; px++) {
        const int x = (bxr_ + px) & (map_width * 8 - 1);
        const uint16_t entry = vram_[uint16_t(tile_row + (x >> 3)) & kVramMask];
        const int address = ((entry & 0xfff) * 16 + row) & kVramMask;
        const uint16_t plane01 = vram_[size_t(address)];
        const uint16_t plane23 = vram_[size_t((address + 8) & kVramMask)];
        const int bit = 7 - (x & 7);
        const int index = ((plane01 >> bit) & 1) | (((plane01 >> (bit + 8)) & 1) << 1) |
                          (((plane23 >> bit) & 1) << 2) | (((plane23 >> (bit + 8)) & 1) << 3);
        out[px] = index != 0 ? uint16_t(((entry >> 12) << 4) | index) : uint16_t(0);
    }
}

void HuC6270::render_sprites(int display_line, uint16_t* out, int width) {
    if ((cr_ & 0x40) == 0) return;
    std::fill(sprite_line_.begin(), sprite_line_.begin() + width, uint16_t(0));
    std::fill(sprite_front_.begin(), sprite_front_.begin() + width, uint8_t(0));

    // Lower numbered sprites win, so the list is drawn back to front.
    std::array<int, kSpritesPerLine> visible{};
    int count = 0;
    for (int i = 0; i < kSprites; i++) {
        const int sy = int(sat_[size_t(i * 4)] & 0x3ff) - 64;
        const int height = sprite_height(sat_[size_t(i * 4 + 3)]);
        if (display_line < sy || display_line >= sy + height) continue;
        if (count == kSpritesPerLine) {
            raise_irq(kStatusOverflow);
            break;
        }
        visible[size_t(count++)] = i;
    }

    bool collision = false;
    for (int slot = count - 1; slot >= 0; slot--) {
        const int i = visible[size_t(slot)];
        const uint16_t attr = sat_[size_t(i * 4 + 3)];
        const int sy = int(sat_[size_t(i * 4)] & 0x3ff) - 64;
        const int sx = int(sat_[size_t(i * 4 + 1)] & 0x3ff) - 32;
        const int height = sprite_height(attr);
        const int cell_width = sprite_width(attr);
        int pattern = (sat_[size_t(i * 4 + 2)] >> 1) & 0x3ff;
        if (cell_width == 32) pattern &= ~1;
        if (height == 32) pattern &= ~2;
        if (height == 64) pattern &= ~6;
        const int palette = attr & 0x0f;
        const bool front = (attr & 0x80) != 0;
        int sprite_row = display_line - sy;
        if ((attr & 0x8000) != 0) sprite_row = height - 1 - sprite_row;
        const int cell_y = sprite_row >> 4;
        const int row = sprite_row & 15;
        for (int cell_x = 0; cell_x < cell_width / 16; cell_x++) {
            const int cell = pattern + cell_y * 2 + cell_x;
            const int address = (cell * 64 + row) & kVramMask;
            const uint16_t plane0 = vram_[size_t(address)];
            const uint16_t plane1 = vram_[size_t((address + 16) & kVramMask)];
            const uint16_t plane2 = vram_[size_t((address + 32) & kVramMask)];
            const uint16_t plane3 = vram_[size_t((address + 48) & kVramMask)];
            for (int px = 0; px < 16; px++) {
                int column = cell_x * 16 + px;
                if ((attr & 0x0800) != 0) column = cell_width - 1 - column;
                const int screen_x = sx + column;
                if (screen_x < 0 || screen_x >= width) continue;
                const int bit = 15 - px;
                const int index = ((plane0 >> bit) & 1) | (((plane1 >> bit) & 1) << 1) |
                                  (((plane2 >> bit) & 1) << 2) | (((plane3 >> bit) & 1) << 3);
                if (index == 0) continue;
                // Sprite 0 is drawn last, so anything already here overlaps it.
                if (i == 0 && sprite_line_[size_t(screen_x)] != 0) collision = true;
                sprite_line_[size_t(screen_x)] = uint16_t(0x100 | (palette << 4) | index);
                sprite_front_[size_t(screen_x)] = front ? 1 : 0;
            }
        }
    }
    if (collision) raise_irq(kStatusCollision);

    for (int px = 0; px < width; px++) {
        const uint16_t sprite = sprite_line_[size_t(px)];
        if (sprite == 0) continue;
        if (sprite_front_[size_t(px)] != 0 || out[px] == 0) out[px] = sprite;
    }
}

}  // namespace dsp
