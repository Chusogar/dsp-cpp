#include "video/atari_mo.h"

namespace dsp {
namespace {

constexpr uint32_t kNoHold = 123456;

int compute_log(int value) {
    if (value == 0) return -1;
    int log = 0;
    while ((value & 1) == 0) {
        log++;
        value >>= 1;
    }
    return value != 1 ? -1 : log;
}

int round_to_powerof2(int value) {
    if (value == 0) return 1;
    int log = 0;
    value >>= 1;
    while (value != 0) {
        log++;
        value >>= 1;
    }
    return 1 << (log + 1);
}

}  // namespace

void AtariMotionObjects::SpriteParameter::set(const Entry& input) {
    word_ = 0xffff;
    for (uint16_t f = 0; f < 4; ++f) {
        if (input[f] != 0) {
            if (word_ != 0xffff) return;  // only one word may hold the parameter
            word_ = f;
        }
    }
    if (word_ == 0xffff) {  // an all-zero entry is valid
        word_ = 0;
        shift_ = 0;
        mask_ = 0;
        return;
    }
    shift_ = 0;
    uint16_t temp = input[word_];
    while ((temp & 1) == 0) {
        shift_++;
        temp = uint16_t(temp >> 1);
    }
    mask_ = temp;
}

void AtariMotionObjects::DualSpriteParameter::set(const DualEntry& input) {
    lower_.set(input.lower);
    upper_.set(input.upper);
    uint16_t temp = lower_.mask();
    uppershift_ = 0;
    while (temp != 0) {
        uppershift_++;
        temp = uint16_t(temp >> 1);
    }
}

AtariMotionObjects::AtariMotionObjects(const Config& config, const uint16_t* slip_ram,
                                       const uint16_t* sprite_ram, int xmax, int ymax)
    : config_(config), slip_ram_(slip_ram), sprite_ram_(sprite_ram), xmax_(xmax), ymax_(ymax) {
    linkmask_.set(config_.link_entry);
    codemask_.set(config_.code_entry);
    colormask_.set(config_.color_entry);
    xposmask_.set(config_.xpos_entry);
    yposmask_.set(config_.ypos_entry);
    widthmask_.set(config_.width_entry);
    heightmask_.set(config_.height_entry);
    hflipmask_.set(config_.hflip_entry);
    vflipmask_.set(config_.vflip_entry);
    prioritymask_.set(config_.priority_entry);
    neighbormask_.set(config_.neighbor_entry);
    absolutemask_.set(config_.absolute_entry);
    specialmask_.set(config_.special_entry);

    tilexshift_ = compute_log(config_.tile_width);
    tileyshift_ = compute_log(config_.tile_height);

    const int bitmapwidth = round_to_powerof2(xposmask_.mask());
    const int bitmapheight = round_to_powerof2(yposmask_.mask());
    bitmapxmask_ = bitmapwidth - 1;
    bitmapymask_ = bitmapheight - 1;

    const int entrycount = round_to_powerof2(linkmask_.mask());
    entrybits_ = compute_log(entrycount);
    slipshift_ = config_.slipheight != 0 ? compute_log(config_.slipheight) : 0;
    sliprammask_ = (bitmapheight >> slipshift_) - 1;
    if (config_.maxperline == 0) config_.maxperline = kMaxPerBank;

    code_lookup_.resize(size_t(round_to_powerof2(codemask_.mask())));
    for (size_t i = 0; i < code_lookup_.size(); ++i) code_lookup_[i] = uint16_t(i);
    color_lookup_.resize(size_t(round_to_powerof2(colormask_.mask())));
    for (size_t i = 0; i < color_lookup_.size(); ++i) color_lookup_[i] = uint16_t(i);
    // Pascal: gfxsize := codesize div 256, filled with config.gfxindex.
    gfx_lookup_.assign(std::max<size_t>(1, code_lookup_.size() / 256), 0);

    active_list_.resize(size_t(kMaxPerBank) * 40);
}

void AtariMotionObjects::build_active_list(uint16_t link) {
    const uint16_t* bankbase = sprite_ram_ + (size_t(bank_) << (entrybits_ + 2));
    size_t current = 0;
    std::vector<bool> visited(size_t(kMaxPerBank), false);

    for (int f = 0; f < config_.maxperline; ++f) {
        const size_t entry_start = current;
        if (!config_.split) {
            const uint16_t* srcdata = bankbase + size_t(link) * 4;
            for (int i = 0; i < 4; ++i) active_list_[current++] = srcdata[i];
        } else {
            const uint16_t* srcdata = bankbase + link;
            for (int i = 0; i < 4; ++i) active_list_[current++] = srcdata[size_t(i) << entrybits_];
        }
        visited[link] = true;
        link = config_.linked ? linkmask_.extract(&active_list_[entry_start])
                              : uint16_t((link + 1) & linkmask_.mask());
        if (visited[link]) break;
    }
    active_last_ = current;
}

void AtariMotionObjects::draw(int xscroll, int yscroll, int prio, const DrawTile& draw_tile) {
    const int stopband = slipshift_ == 0 ? 0 : ((512 / config_.slipheight) >> 1) - 1;

    for (int band = 0; band <= stopband; ++band) {
        draw_band(band, xscroll, yscroll, prio, draw_tile);
    }
}

void AtariMotionObjects::draw_band(int band, int xscroll, int yscroll, int prio,
                                   const DrawTile& draw_tile) {
    uint16_t link = 0;
    if (slipshift_ != 0) {
        link = uint16_t((slip_ram_[band & sliprammask_] >> linkmask_.shift()) & linkmask_.mask());
    }
    build_active_list(link);
    next_xpos_ = kNoHold;
    if (active_last_ < 4) return;

    if (config_.reverse) {
        for (size_t current = active_last_ - 4;; current -= 4) {
            render_object(&active_list_[current], xscroll, yscroll, prio, draw_tile);
            if (current == 0) break;
        }
    } else {
        for (size_t current = 0;; current += 4) {
            render_object(&active_list_[current], xscroll, yscroll, prio, draw_tile);
            if (current + 4 >= active_last_) break;
        }
    }
}

void AtariMotionObjects::render_object(const uint16_t* entry, int xscroll, int yscroll, int prio,
                                       const DrawTile& draw_tile) {
    const int priority = prioritymask_.extract(entry);
    if (priority != prio && prio != -1) return;

    const uint16_t rawcode = codemask_.extract(entry);
    int code = code_lookup_[rawcode];
    int color = color_lookup_[colormask_.extract(entry)] << 4;
    int gfx = 0;
    if (!gfx_lookup_.empty()) gfx = gfx_lookup_[(rawcode >> 8) % gfx_lookup_.size()];
    int xpos = xposmask_.extract(entry);
    int ypos = -int(yposmask_.extract(entry));
    const bool hflip = hflipmask_.extract(entry) != 0;
    const bool vflip = vflipmask_.extract(entry) != 0;
    const int width = widthmask_.extract(entry) + 1;
    const int height = heightmask_.extract(entry) + 1;

    color += config_.palettebase;

    if (absolutemask_.extract(entry) == 0) {
        xpos -= xscroll;
        ypos -= yscroll;
    }
    ypos -= height << tileyshift_;

    if (next_xpos_ != kNoHold) xpos = int(next_xpos_);
    next_xpos_ = kNoHold;

    if (neighbormask_.extract(entry) != 0) {
        if (!config_.nextneighbor) {
            xpos = int(last_xpos_) + config_.tile_width;
        } else {
            next_xpos_ = uint32_t(xpos + config_.tile_width);
        }
    }
    last_xpos_ = uint32_t(xpos);

    xpos &= bitmapxmask_;
    ypos &= bitmapymask_;

    if (specialmask_.mask() != 0 && specialmask_.extract(entry) == config_.specialvalue) return;

    int xadv = config_.tile_width;
    if (hflip) {
        xpos += (width - 1) << tilexshift_;
        xadv = -xadv;
    }
    int yadv = config_.tile_height;
    if (vflip) {
        ypos += (height - 1) << tileyshift_;
        yadv = -yadv;
    }

    auto visible = [this](int sx, int sy) {
        return (sx < xmax_ || sx > 503) && (sy < ymax_ || sy > 503);
    };

    if (!config_.swapxy) {
        int sy = ypos;
        for (int y = 0; y < height; ++y) {
            int sx = xpos;
            for (int x = 0; x < width; ++x) {
                if (visible(sx, sy)) draw_tile(code, color, hflip, vflip, sx, sy, gfx, priority);
                sx += xadv;
                code++;
            }
            sy += yadv;
        }
    } else {
        int sx = xpos;
        for (int x = 0; x < width; ++x) {
            int sy = ypos;
            for (int y = 0; y < height; ++y) {
                if (visible(sx, sy)) draw_tile(code, color, hflip, vflip, sx, sy, gfx, priority);
                sy += yadv;
                code++;
            }
            sx += xadv;
        }
    }
}

}  // namespace dsp
