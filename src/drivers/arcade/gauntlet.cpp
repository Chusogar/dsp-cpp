#include "drivers/arcade/gauntlet.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/rom_loader.h"

extern "C" {
#include "lean/gauntlet/gauntlet_core.h"
}

namespace dsp {
namespace {

// Revision 14 (the current MAME parent set) and revision 9, which only differ in
// the program ROMs outside of the SLAPSTIC protected area.
const std::vector<std::vector<RomEntry>> kMainRomSets = {
    {
        {"136037-1307.9a", 0x8000, 0x00000, 0x46fe8743},
        {"136037-1308.9b", 0x8000, 0x00001, 0x276e15c4},
        {"136037-205.10a", 0x4000, 0x38000, 0x6d99ed51},
        {"136037-206.10b", 0x4000, 0x38001, 0x545ead91},
        {"136037-1409.7a", 0x8000, 0x40000, 0x6fb8419c},
        {"136037-1410.7b", 0x8000, 0x40001, 0x931bd2a0},
    },
    {
        {"136041-507.9a", 0x8000, 0x00000, 0x8784133f},
        {"136041-508.9b", 0x8000, 0x00001, 0x2843bde3},
        {"136037-205.10a", 0x4000, 0x38000, 0x6d99ed51},
        {"136037-206.10b", 0x4000, 0x38001, 0x545ead91},
        {"136041-609.7a", 0x8000, 0x40000, 0x5b4ee415},
        {"136041-610.7b", 0x8000, 0x40001, 0x41f5c9e2},
    },
};

const std::vector<RomEntry> kSoundRoms = {
    {"136037-120.16r", 0x4000, 0x4000, 0x6ee7f3cc},
    {"136037-119.16s", 0x8000, 0x8000, 0xfa19861f},
};

// The character ROM is 8K on the later revisions and 16K on the earlier ones.
// SLAPSTIC number used by each main ROM set: the 4 player parent uses a 104 and
// the 2 player version (136041-xxx) a 107.
const std::array<int, 2> kSlapsticTypes = {104, 107};

const std::vector<std::vector<RomEntry>> kCharRomSets = {
    {{"136037-104.6p", 0x2000, 0x0000, 0x9e2a5b59}},
    {{"136037-104.6p", 0x4000, 0x0000, 0x6c276a1d}},
};

const std::vector<RomEntry> kTileRoms = {
    {"136037-111.1a", 0x8000, 0x00000, 0x91700f33},
    {"136037-112.1b", 0x8000, 0x08000, 0x869330be},
    {"136037-113.1l", 0x8000, 0x10000, 0xd497d0a8},
    {"136037-114.1mn", 0x8000, 0x18000, 0x29ef9882},
    {"136037-115.2a", 0x8000, 0x20000, 0x9510b898},
    {"136037-116.2b", 0x8000, 0x28000, 0x11e0ac5b},
    {"136037-117.2l", 0x8000, 0x30000, 0x29a5db41},
    {"136037-118.2mn", 0x8000, 0x38000, 0x8bf3b263},
};

// Prepared images (write_prepared): one file each, read in place on boards.
const char* const kPreparedMain = "lean_main.bin";
const char* const kPreparedSound = "lean_sound.bin";
const char* const kPreparedChars = "lean_chars.bin";
const char* const kPreparedTiles = "lean_tiles.bin";
const char* const kPreparedInfo = "lean_info.bin";   // SLAPSTIC type, 16 bit little endian
constexpr uint32_t kMainBytes = 0x80000, kSoundBytes = 0xc000, kCharBytes = 0x4000;
constexpr uint32_t kTileBytes = GT_TILES * 8 * 4;

// A prepared image: in place when the board maps it, else read into `own`.
const uint8_t* prepared(RomLoader& loader, const char* name, uint32_t size, std::vector<uint8_t>& own) {
    const RomEntry entry{name, size, 0, 0};
    if (const uint8_t* mapped = loader.map(entry)) return mapped;
    const std::vector<std::string> names = loader.filenames();
    if (std::find(names.begin(), names.end(), name) == names.end()) return nullptr;
    std::string ignored;
    own.assign(size, 0);
    if (!loader.load({entry}, own, &ignored)) {
        own.clear();
        return nullptr;
    }
    return own.data();
}

bool write_file(const std::string& path, const void* data, size_t size, std::string* error) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr || std::fwrite(data, 1, size, f) != size) {
        if (f) std::fclose(f);
        if (error) *error = "cannot write " + path;
        return false;
    }
    return std::fclose(f) == 0;
}

}  // namespace

struct Gauntlet::Lean {
    gt_core core;
    gt_renderer renderer;             // framebuffer()
    gt_renderer rows_renderer;        // pen_rows() (maybe on the other core)
};

Gauntlet::Gauntlet() : lean_(std::make_unique<Lean>()) {
    std::memset(lean_.get(), 0, sizeof(Lean));
    frame_audio_.assign(4096, 0);
}

Gauntlet::~Gauntlet() {
    if (ready_) gt_release(&lean_->core);
    for (gt_renderer* r : {&lean_->renderer, &lean_->rows_renderer}) {
        std::free(r->mobuf);
        if (r->mo.list != r->mo.active_list) std::free(r->mo.list);
    }
}

bool Gauntlet::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    gt_roms roms{main_, sound_, chars_, tiles_, slapstic_};
    gt_init(&lean_->core, &roms, kSampleRate);
    lean_->core.audio = frame_audio_.data();
    lean_->core.audio_cap = int(frame_audio_.size());
    lean_->core.on_frame = &Gauntlet::on_vblank;
    lean_->core.user = this;
    lean_->core.idle_skip = true;
    ready_ = true;
    reset();
    return true;
}

bool Gauntlet::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // A prepared set (tools/lean_roms): the images as they are.
    main_ = prepared(loader, kPreparedMain, kMainBytes, main_rom_);
    if (main_ != nullptr) {
        sound_ = prepared(loader, kPreparedSound, kSoundBytes, sound_rom_);
        chars_ = prepared(loader, kPreparedChars, kCharBytes, char_rom_);
        std::vector<uint8_t> tiles_own;
        const uint8_t* tiles = prepared(loader, kPreparedTiles, kTileBytes, tiles_own);
        const uint8_t* info = prepared(loader, kPreparedInfo, 2, info_);
        if (!sound_ || !chars_ || !tiles || !info) {
            if (error) *error = "incomplete prepared Gauntlet set (lean_*.bin)";
            return false;
        }
        if (!tiles_own.empty() || (reinterpret_cast<uintptr_t>(tiles) & 3) != 0) {
            tile_rows_.assign(GT_TILES * 8, 0);
            std::memcpy(tile_rows_.data(), tiles, kTileBytes);
            tiles_ = tile_rows_.data();
        } else {
            tiles_ = reinterpret_cast<const uint32_t*>(tiles);
        }
        slapstic_ = info[0] | (info[1] << 8);
        return true;
    }

    // Main ROMs are 16 bit: even offsets hold the high byte of each word.
    std::vector<uint8_t> temp(0x80000, 0);
    bool loaded = false;
    for (size_t set_index = 0; set_index < kMainRomSets.size(); set_index++) {
        const std::vector<RomEntry>& set = kMainRomSets[set_index];
        std::string attempt;
        std::vector<std::vector<uint8_t>> data;
        bool complete = true;
        for (const RomEntry& entry : set) {
            data.emplace_back(entry.length, 0);
            RomEntry single{entry.name, entry.length, 0, entry.crc};
            if (!loader.load({single}, data.back(), &attempt)) {
                complete = false;
                break;
            }
        }
        if (!complete) {
            if (error) *error = attempt;
            continue;
        }
        for (size_t index = 0; index < set.size(); index++) {
            const RomEntry& entry = set[index];
            for (uint32_t byte = 0; byte < entry.length; byte++) {
                temp[(entry.offset & ~1u) + byte * 2 + (entry.offset & 1u)] = data[index][byte];
            }
        }
        slapstic_ = kSlapsticTypes[set_index];
        loaded = true;
        break;
    }
    if (!loaded) return false;
    // The two 32K halves of each 64K program block are swapped on the board;
    // the SLAPSTIC banks ($38000) stay as loaded.
    main_rom_.assign(kMainBytes, 0);
    for (uint32_t base : {0x00000u, 0x40000u, 0x50000u, 0x60000u, 0x70000u}) {
        std::memcpy(&main_rom_[base], &temp[base + 0x8000], 0x8000);
        std::memcpy(&main_rom_[base + 0x8000], &temp[base], 0x8000);
    }
    std::memcpy(&main_rom_[0x38000], &temp[0x38000], 0x8000);
    main_ = main_rom_.data();

    std::vector<uint8_t> sound(0x10000, 0);
    if (!loader.load(kSoundRoms, sound, error)) return false;
    sound_rom_.assign(sound.begin() + 0x4000, sound.end());
    sound_ = sound_rom_.data();

    loaded = false;
    for (const std::vector<RomEntry>& set : kCharRomSets) {
        std::vector<uint8_t> chars(set[0].length, 0);
        if (loader.load(set, chars, error)) {
            char_rom_.assign(kCharBytes, 0);
            std::copy(chars.begin(), chars.end(), char_rom_.begin());
            loaded = true;
            break;
        }
    }
    if (!loaded) return false;
    chars_ = char_rom_.data();

    std::vector<uint8_t> tile_rom(0x40000, 0);
    if (!loader.load(kTileRoms, tile_rom, error)) return false;
    tile_rows_.assign(size_t(GT_TILES) * 8, 0);
    gt_pack_tiles(tile_rom.data(), tile_rows_.data());
    tiles_ = tile_rows_.data();
    warnings_ = loader.warnings();
    return true;
}

bool Gauntlet::write_prepared(const std::string& dir, std::string* error) const {
    if (!ready_) {
        if (error) *error = "no ROM set loaded";
        return false;
    }
    const uint8_t info[2] = {uint8_t(slapstic_), uint8_t(slapstic_ >> 8)};
    return write_file(dir + "/" + kPreparedMain, main_, kMainBytes, error) &&
           write_file(dir + "/" + kPreparedSound, sound_, kSoundBytes, error) &&
           write_file(dir + "/" + kPreparedChars, chars_, kCharBytes, error) &&
           write_file(dir + "/" + kPreparedTiles, tiles_, kTileBytes, error) &&
           write_file(dir + "/" + kPreparedInfo, info, sizeof info, error);
}

void Gauntlet::reset() {
    if (!ready_) return;
    gt_reset(&lean_->core);
    audio_.clear();
}

void Gauntlet::run_frame() {
    if (!ready_) return;
    gt_run_frame(&lean_->core);
    audio_.insert(audio_.end(), frame_audio_.begin(), frame_audio_.begin() + lean_->core.audio_n);
}

// At the start of the vertical blank: the palette of the frame, and its
// picture when the frame buffer is in use.
void Gauntlet::on_vblank(void* user, gt_core* core) {
    Gauntlet* self = static_cast<Gauntlet*>(user);
    for (int i = 0; i < 0x400; i++) self->palette_[i] = 0xff000000u | gt_palette_rgb(&core->v, i);
    if (self->draw_ && !self->framebuffer_.empty()) self->compose();
}

void Gauntlet::compose() const {
    const gt_core& c = lean_->core;
    uint16_t lines[16 * kScreenWidth];
    for (int y = 0; y < kScreenHeight; y += 16) {
        gt_compose(&lean_->renderer, &c.roms, &c.v, y, y + 16, lines);
        uint32_t* out = &framebuffer_[size_t(y) * kScreenWidth];
        for (int i = 0; i < 16 * kScreenWidth; i++) out[i] = palette_[lines[i] & 0x3ff];
    }
}

const uint32_t* Gauntlet::framebuffer() const {
    if (framebuffer_.empty()) {
        framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
        if (ready_) compose();
    }
    return framebuffer_.data();
}

void Gauntlet::rows(const void* source, int y, int n, uint16_t* pens) {
    const Gauntlet* self = static_cast<const Gauntlet*>(source);
    const gt_core& c = self->lean_->core;
    gt_compose(&self->lean_->rows_renderer, &c.roms, &c.v, y, y + n, pens);
}

Machine::PenRows Gauntlet::pen_rows() const {
    if (!ready_) return {};
    return {&Gauntlet::rows, this, palette_, 0x400};
}

void Gauntlet::set_inputs(const MachineInputs& inputs) {
    if (!ready_) return;
    gt_inputs in{};
    in.coin[0] = inputs.coin1;
    in.coin[1] = inputs.coin2;
    in.service = service_ || inputs.service;
    const InputState* players[2] = {&inputs.player1, &inputs.player2};
    for (int p = 0; p < 2; p++) {
        in.p[p].up = players[p]->up;
        in.p[p].down = players[p]->down;
        in.p[p].left = players[p]->left;
        in.p[p].right = players[p]->right;
        in.p[p].fire = players[p]->button1;
        in.p[p].magic = players[p]->button2;
    }
    gt_set_inputs(&lean_->core, &in);
}

void Gauntlet::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) service_ = (value & 0x08) == 0;  // (the self test switch, active low)
}

void Gauntlet::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

uint32_t Gauntlet::debug_skipped_main() const { return lean_->core.skipped; }
uint32_t Gauntlet::debug_skipped_sound() const { return lean_->core.snd_skipped; }

}  // namespace dsp
