#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Atari motion object hardware, ported from arcade/misc/atari_mo.pas.
class AtariMotionObjects {
public:
    using Entry = std::array<uint16_t, 4>;
    struct DualEntry {
        Entry lower;
        Entry upper;
    };

    struct Config {
        int tile_width = 8;
        int tile_height = 8;
        uint8_t bankcount = 1;
        bool linked = true;
        bool split = true;
        bool reverse = false;
        bool swapxy = false;
        bool nextneighbor = false;
        uint16_t slipheight = 0;
        uint16_t maxperline = 0;
        uint16_t palettebase = 0;

        Entry link_entry{};
        DualEntry code_entry{};
        DualEntry color_entry{};
        Entry xpos_entry{};
        Entry ypos_entry{};
        Entry width_entry{};
        Entry height_entry{};
        Entry hflip_entry{};
        Entry vflip_entry{};
        Entry priority_entry{};
        Entry neighbor_entry{};
        Entry absolute_entry{};
        Entry special_entry{};
        uint16_t specialvalue = 0;
    };

    // Called once per 8x8 (or tile sized) piece of a motion object.
    // `gfx` is the Atari System 1 graphics bank (0 when the driver has one tileset).
    // `priority` is the raw priority field from sprite RAM (System 1 uses bit 0).
    using DrawTile =
        std::function<void(int code, int color, bool hflip, bool vflip, int x, int y, int gfx,
                           int priority)>;

    AtariMotionObjects(const Config& config, const uint16_t* slip_ram, const uint16_t* sprite_ram,
                       int xmax, int ymax);

    void set_bank(uint32_t bank) { bank_ = bank; }
    uint32_t bank() const { return bank_; }
    // `prio` of -1 renders every priority.
    void draw(int xscroll, int yscroll, int prio, const DrawTile& draw_tile);

    // Lookup tables, exposed so drivers can apply their own code/colour mangling.
    std::vector<uint16_t>& code_lookup() { return code_lookup_; }
    std::vector<uint16_t>& color_lookup() { return color_lookup_; }
    std::vector<uint8_t>& gfx_lookup() { return gfx_lookup_; }

private:
    class SpriteParameter {
    public:
        void set(const Entry& input);
        uint16_t extract(const uint16_t* data) const {
            return uint16_t((data[word_] >> shift_) & mask_);
        }
        uint16_t shift() const { return shift_; }
        uint16_t mask() const { return mask_; }

    private:
        uint16_t word_ = 0;
        uint16_t shift_ = 0;
        uint16_t mask_ = 0;
    };

    class DualSpriteParameter {
    public:
        void set(const DualEntry& input);
        uint16_t extract(const uint16_t* data) const {
            return uint16_t(lower_.extract(data) | (upper_.extract(data) << uppershift_));
        }
        uint16_t mask() const { return uint16_t(lower_.mask() | (upper_.mask() << uppershift_)); }

    private:
        SpriteParameter lower_, upper_;
        uint16_t uppershift_ = 0;
    };

    void build_active_list(uint16_t link);
    void render_object(const uint16_t* entry, int xscroll, int yscroll, int prio,
                       const DrawTile& draw_tile);

    static constexpr int kMaxPerBank = 1024;

    Config config_;
    const uint16_t* slip_ram_;
    const uint16_t* sprite_ram_;
    int xmax_, ymax_;
    uint32_t bank_ = 0;

    SpriteParameter linkmask_, xposmask_, yposmask_, widthmask_, heightmask_;
    SpriteParameter hflipmask_, vflipmask_, prioritymask_, neighbormask_, absolutemask_;
    SpriteParameter specialmask_;
    DualSpriteParameter codemask_, colormask_;

    int tilexshift_ = 0, tileyshift_ = 0;
    int bitmapxmask_ = 0, bitmapymask_ = 0;
    int entrybits_ = 0;
    int slipshift_ = 0;
    int sliprammask_ = 0;

    std::vector<uint16_t> code_lookup_, color_lookup_;
    std::vector<uint8_t> gfx_lookup_;
    std::vector<uint16_t> active_list_;
    size_t active_last_ = 0;

    uint32_t last_xpos_ = 0;
    uint32_t next_xpos_ = 0;
};

}  // namespace dsp
