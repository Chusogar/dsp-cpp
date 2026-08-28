#include "video/k051960.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

constexpr uint8_t kXOffset[8] = {0, 1, 4, 5, 16, 17, 20, 21};
constexpr uint8_t kYOffset[8] = {0, 2, 8, 10, 32, 34, 40, 42};
constexpr uint8_t kWidth[8] = {1, 2, 1, 2, 4, 2, 4, 8};
constexpr uint8_t kHeight[8] = {1, 1, 2, 2, 2, 4, 4, 8};

}  // namespace

K051960::K051960(SpriteCallback cb, std::vector<uint8_t> rom, int /*bpp*/)
    : callback_(std::move(cb)), rom_(std::move(rom)) {
    sprite_mask_ = rom_.empty() ? 0 : uint32_t(rom_.size() / 128) - 1;
    GfxLayout layout;
    layout.width = 16;
    layout.height = 16;
    layout.total = int(rom_.size() / 128);
    layout.planes = 4;
    layout.char_increment = 8 * 128;
    // Pascal tipo=2 (Ajax): gfx_set_desc_data(4,0,8*128,0,8,16,24) + ps_x/ps_y
    layout.plane_offsets = {0, 8, 16, 24};
    layout.x_offsets = {0, 1, 2, 3, 4, 5, 6, 7,
                        8 * 32 + 0, 8 * 32 + 1, 8 * 32 + 2, 8 * 32 + 3,
                        8 * 32 + 4, 8 * 32 + 5, 8 * 32 + 6, 8 * 32 + 7};
    // y: 0*32..7*32, then 16*32..23*32 (not linear i*64)
    for (int i = 0; i < 8; i++) layout.y_offsets.push_back(i * 32);
    for (int i = 16; i < 24; i++) layout.y_offsets.push_back(i * 32);
    if (!rom_.empty()) gfx_.decode(layout, rom_);
    sorted_list_.fill(-1);
}

void K051960::reset() {
    ram_.fill(0);
    k051937_.fill(0);
    spriterombank_.fill(0);
    sorted_list_.fill(-1);
    irq_enabled_ = false;
    nmi_enabled_ = false;
    counter_ = 0;
    spriteflip_ = false;
    readroms_ = false;
}

uint8_t K051960::read(uint16_t offset) {
    return ram_[offset & 0x3ff];
}

void K051960::write(uint16_t offset, uint8_t value) {
    ram_[offset & 0x3ff] = value;
}

uint8_t K051960::k051937_read(uint8_t offset) {
    offset &= 7;
    // Pascal k051960.pas: games need bit0 to pulse on each read of reg 0.
    if (offset == 0) {
        const uint8_t bit = uint8_t(counter_ & 1);
        counter_ = uint8_t(counter_ + 1);
        return bit;
    }
    if (readroms_ && offset >= 4) {
        // ROM read path not required for Ajax/Aliens boot; return 0.
        return 0;
    }
    return k051937_[offset];
}

void K051960::k051937_write(uint8_t offset, uint8_t value) {
    offset &= 7;
    if (offset == 0) {
        // MAME: bit0 = IRQ enable; ACK clears IRQ on 1→0 transition.
        if ((k051937_[0] & 0x01) != 0 && (value & 0x01) == 0 && irq_cb_) irq_cb_(false);
        if ((k051937_[0] & 0x02) != 0 && (value & 0x02) == 0 && firq_cb_) firq_cb_(false);
        k051937_[0] = value;
        irq_enabled_ = (value & 0x01) != 0;
        nmi_enabled_ = (value & 0x04) != 0;
        spriteflip_ = (value & 0x08) != 0;
        readroms_ = (value & 0x20) != 0;
    } else if (offset >= 2 && offset < 5) {
        k051937_[offset] = value;
        spriterombank_[offset - 2] = value;
    } else {
        k051937_[offset] = value;
    }
}

void K051960::update_sprites() {
    sorted_list_.fill(-1);
    for (int f = 0; f < kNumSprites; f++) {
        if ((ram_[size_t(f * 8)] & 0x80) != 0) {
            sorted_list_[ram_[size_t(f * 8)] & 0x7f] = f * 8;
        }
    }
}

void K051960::update_line(int line) {
    // Match k051960.pas: NMI every 32 lines if enabled; VBlank IRQ every frame at line 240.
    // (MAME gates on enable bit; Pascal always asserts — Ajax needs the latter.)
    if ((line % 32) == 0 && nmi_enabled_ && nmi_cb_) nmi_cb_(true);
    if (line == 240 && irq_cb_) irq_cb_(true);
}

void K051960::draw_sprites(int pri, uint16_t* dest, int dest_w, int dest_h, int crop_x,
                           int crop_y) const {
    if (!dest) return;

    for (int pri_code = 0; pri_code < kNumSprites; pri_code++) {
        const int offs = sorted_list_[size_t(pri_code)];
        if (offs < 0) continue;

        uint16_t nchar =
            uint16_t(ram_[size_t(offs + 2)] | ((ram_[size_t(offs + 1)] & 0x1f) << 8));
        uint16_t color = ram_[size_t(offs + 3)];
        uint16_t priority = 0;
        uint16_t shadow = uint16_t(color & 0x80);
        if (callback_) callback_(nchar, color, priority, shadow);
        if (int(priority) != pri) continue;

        const uint8_t size = uint8_t((ram_[size_t(offs + 1)] & 0xe0) >> 5);
        const int w = kWidth[size];
        const int h = kHeight[size];
        if (w >= 2) nchar &= 0xfffe;
        if (h >= 2) nchar &= 0xfffd;
        if (w >= 4) nchar &= 0xfffb;
        if (h >= 4) nchar &= 0xfff7;
        if (w >= 8) nchar &= 0xffef;
        if (h >= 8) nchar &= 0xffdf;

        int ox = (256 * ram_[size_t(offs + 6)] + ram_[size_t(offs + 7)]) & 0x1ff;
        int oy = 256 - ((256 * ram_[size_t(offs + 4)] + ram_[size_t(offs + 5)]) & 0x1ff);
        bool flipx = (ram_[size_t(offs + 6)] & 0x02) != 0;
        bool flipy = (ram_[size_t(offs + 4)] & 0x02) != 0;

        const int zoom_x = (ram_[size_t(offs + 6)] & 0xfc) >> 2;
        const int zoom_y = (ram_[size_t(offs + 4)] & 0xfc) >> 2;
        const float zx = float(0x100 - zoom_x) / 256.f;
        const float zy = float(0x100 - zoom_y) / 256.f;

        if (spriteflip_) {
            ox = 512 - int(std::lround(zx * w * 16)) - ox;
            oy = 256 - int(std::lround(zy * h * 16)) - oy;
            flipx = !flipx;
            flipy = !flipy;
        }

        // Color is already bank-adjusted by callback (e.g. 16+(c&0xf)); pen = color*16 + pix
        const uint16_t color_base = uint16_t(color << 4);

        for (int y = 0; y < h; y++) {
            const int sy = oy + int(std::lround(zy * y * 16)) - crop_y;
            for (int x = 0; x < w; x++) {
                uint16_t c = nchar;
                const int sx = ox + int(std::lround(zx * x * 16)) - crop_x;
                if (flipx) c = uint16_t(c + kXOffset[w - 1 - x]);
                else c = uint16_t(c + kXOffset[x]);
                if (flipy) c = uint16_t(c + kYOffset[h - 1 - y]);
                else c = uint16_t(c + kYOffset[y]);

                const uint8_t* pixels = gfx_.element(int(c & sprite_mask_));
                if (!pixels) continue;

                // Approximate zoom by nearest-neighbour scale of the 16x16 cell
                const int dw = std::max(1, int(std::lround(16 * zx)));
                const int dh = std::max(1, int(std::lround(16 * zy)));
                for (int py = 0; py < dh; py++) {
                    const int src_y = flipy ? (15 - (py * 16 / dh)) : (py * 16 / dh);
                    const int screen_y = sy + py;
                    if (screen_y < 0 || screen_y >= dest_h) continue;
                    for (int px = 0; px < dw; px++) {
                        const int src_x = flipx ? (15 - (px * 16 / dw)) : (px * 16 / dw);
                        const int screen_x = sx + px;
                        if (screen_x < 0 || screen_x >= dest_w) continue;
                        const uint8_t pen = pixels[size_t(src_y * 16 + src_x)];
                        if (pen) {
                            dest[size_t(screen_y * dest_w + screen_x)] =
                                uint16_t(color_base + pen);
                        }
                    }
                }
            }
        }
    }
}

}  // namespace dsp
