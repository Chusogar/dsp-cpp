#include "drivers/arcade/atari_system1_lean.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/fs_compat.h"
#include "core/rom_loader.h"

extern "C" {
#include "lean/atarisy1/atarisy1_core.h"
}

namespace dsp {
namespace {

// (The same sets as AtariSystem1's.)
const std::vector<RomEntry> kBiosRoms = {
    {"136032.205.l13|136032.205", 0x4000, 0x0, 0x88d0be26},
    {"136032.206.l12|136032.206", 0x4000, 0x1, 0x3c79ef05},
};
const std::vector<RomEntry> kCharRoms = {
    {"136032.104.f5|136032.104|136032.107.b2", 0x2000, 0x0, 0x7a29dc07},
};
const std::vector<RomEntry> kIndyRom = {
    {"136036.432", 0x8000, 0x00000, 0xd888cdf1},
    {"136036.431", 0x8000, 0x00001, 0xb7ac7431},
    {"136036.434", 0x8000, 0x10000, 0x802495fd},
    {"136036.433", 0x8000, 0x10001, 0x3a914e5c},
    {"136036.456", 0x4000, 0x20000, 0xec146b09},
    {"136036.457", 0x4000, 0x20001, 0x6628de01},
    {"136036.358", 0x4000, 0x28000, 0xd9351106},
    {"136036.359", 0x4000, 0x28001, 0xe731caea},
};
const std::vector<RomEntry> kIndySound = {
    {"136036.153", 0x4000, 0x4000, 0x95294641},
    {"136036.154", 0x4000, 0x8000, 0xcbfc6adb},
    {"136036.155", 0x4000, 0xc000, 0x4c8233ac},
};
const std::vector<RomEntry> kIndyBack = {
    {"136036.135", 0x8000, 0x000000, 0xffa8749c},
    {"136036.139", 0x8000, 0x010000, 0xb682bfca},
    {"136036.143", 0x8000, 0x020000, 0x7697da26},
    {"136036.147", 0x8000, 0x030000, 0x4e9d664c},
    {"136036.136", 0x8000, 0x080000, 0xb2b403aa},
    {"136036.140", 0x8000, 0x090000, 0xec0c19ca},
    {"136036.144", 0x8000, 0x0a0000, 0x4407df98},
    {"136036.148", 0x8000, 0x0b0000, 0x70dce06d},
    {"136036.137", 0x8000, 0x100000, 0x3f352547},
    {"136036.141", 0x8000, 0x110000, 0x9cbdffd0},
    {"136036.145", 0x8000, 0x120000, 0xe828e64b},
    {"136036.149", 0x8000, 0x130000, 0x81503a23},
    {"136036.138", 0x8000, 0x180000, 0x48c4d79d},
    {"136036.142", 0x8000, 0x190000, 0x7faae75f},
    {"136036.146", 0x8000, 0x1a0000, 0x8ae5a7b5},
    {"136036.150", 0x8000, 0x1b0000, 0xa10c4bd9},
};
const std::vector<RomEntry> kIndyProms = {
    {"136036.152", 0x200, 0x000, 0x4f96e57c},
    {"136036.151", 0x200, 0x200, 0x7daf351f},
};
const std::vector<RomEntry> kRoadRom = {
    {"136040-228.11c|136040.228|136040-228", 0x8000, 0x10000, 0xb66c629a},
    {"136040-229.11a|136040.229|136040-229", 0x8000, 0x10001, 0x5638959f},
    {"136040-230.13c|136040.230|136040-230", 0x8000, 0x20000, 0xcd7956a3},
    {"136040-231.13a|136040.231|136040-231", 0x8000, 0x20001, 0x722f2d3b},
    {"136040-134.12c|136040.134|136040-134", 0x8000, 0x50000, 0x18f431fe},
    {"136040-135.12a|136040.135|136040-135", 0x8000, 0x50001, 0xcb06f9ab},
    {"136040-136.14c|136040.136|136040-136", 0x8000, 0x60000, 0x8050bce4},
    {"136040-137.14a|136040.137|136040-137", 0x8000, 0x60001, 0x3372a5cf},
    {"136040-138.16c|136040.138|136040-138", 0x8000, 0x70000, 0xa83155ee},
    {"136040-139.16a|136040.139|136040-139", 0x8000, 0x70001, 0x23aead1c},
    {"136040-140.17c|136040.140|136040-140", 0x4000, 0x80000, 0xd1464c88},
    {"136040-141.17a|136040.141|136040-141", 0x4000, 0x80001, 0xf8f2acdf},
};
const std::vector<RomEntry> kRoadSound = {
    {"136040-143.15e|136040.143|136040-143", 0x4000, 0x8000, 0x62b9878e},
    {"136040-144.17e|136040.144|136040-144", 0x4000, 0xc000, 0x6ef1b804},
};
const std::vector<RomEntry> kRoadBack = {
    {"136040-101.4b|136040.101|136040-101", 0x8000, 0x000000, 0x26d9f29c},
    {"136040-107.9b|136040.107|136040-107", 0x8000, 0x010000, 0x8aac0ba4},
    {"136040-113.4f|136040.113|136040-113", 0x8000, 0x020000, 0x48b74c52},
    {"136040-119.9f|136040.119|136040-119", 0x8000, 0x030000, 0x17a6510c},
    {"136040-102.3b|136040.102|136040-102", 0x8000, 0x080000, 0xae88f54b},
    {"136040-108.8b|136040.108|136040-108", 0x8000, 0x090000, 0xa2ac13d4},
    {"136040-114.3f|136040.114|136040-114", 0x8000, 0x0a0000, 0xc91c3fcb},
    {"136040-120.8f|136040.120|136040-120", 0x8000, 0x0b0000, 0x42d25859},
    {"136040-103.2b|136040.103|136040-103", 0x8000, 0x100000, 0xf2d7ef55},
    {"136040-109.7b|136040.109|136040-109", 0x8000, 0x110000, 0x11a843dc},
    {"136040-115.2f|136040.115|136040-115", 0x8000, 0x120000, 0x8b1fa5bc},
    {"136040-121.7f|136040.121|136040-121", 0x8000, 0x130000, 0xecf278f2},
    {"136040-104.1b|136040.104|136040-104", 0x8000, 0x180000, 0x0203d89c},
    {"136040-110.6b|136040.110|136040-110", 0x8000, 0x190000, 0x64801601},
    {"136040-116.1f|136040.116|136040-116", 0x8000, 0x1a0000, 0x52b23a36},
    {"136040-122.6f|136040.122|136040-122", 0x8000, 0x1b0000, 0xb1137a9d},
    {"136040-105.4d|136040.105|136040-105", 0x8000, 0x200000, 0x398a36f8},
    {"136040-111.9d|136040.111|136040-111", 0x8000, 0x210000, 0xf08b418b},
    {"136040-117.2d|136040.117|136040-117", 0x8000, 0x220000, 0xc4394834},
    {"136040-123.7d|136040.123|136040-123", 0x8000, 0x230000, 0xdafd3dbe},
    {"136040-106.3d|136040.106|136040-106", 0x8000, 0x280000, 0x36a77bc5},
    {"136040-112.8d|136040.112|136040-112", 0x8000, 0x290000, 0xb6624f3c},
    {"136040-118.1d|136040.118|136040-118", 0x8000, 0x2a0000, 0xf489a968},
    {"136040-124.6d|136040.124|136040-124", 0x8000, 0x2b0000, 0x524d65f7},
};
const std::vector<RomEntry> kRoadProms = {
    {"136040-126.7a|136040.126|136040-126", 0x200, 0x000, 0x1713c0cd},
    {"136040-125.5a|136040.125|136040-125", 0x200, 0x200, 0xa9ca8795},
};

bool try_open_loader(RomLoader& loader, const std::string& path) {
    std::string ignored;
    return loader.open(path, &ignored);
}

// Prepared images (write_prepared): one file each, read in place on boards.
const char* const kPreparedMain = "lean_main.bin";
const char* const kPreparedSound = "lean_sound.bin";
const char* const kPreparedChars = "lean_chars.bin";
const char* const kPreparedProms = "lean_proms.bin";
constexpr uint32_t kSoundBytes = 0xc000, kCharBytes = 0x2000, kPromBytes = 0x400;
constexpr uint32_t kBankBytes = AS1_TILES_PER_BANK * 8 * 4;
std::string prepared_bank_name(int bank) { return "lean_tiles" + std::to_string(bank) + ".bin"; }

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

void free_mo_list(as1_mo_list* list) {
    if (list == nullptr) return;
    if (list->mo.list != list->mo.active_list) std::free(list->mo.list);
    std::free(list);
}

}  // namespace

struct AtariSystem1Lean::Lean {
    as1_core core;
};

AtariSystem1Lean::AtariSystem1Lean(Game game) : game_(game), lean_(std::make_unique<Lean>()) {
    std::memset(lean_.get(), 0, sizeof(Lean));
    frame_audio_.assign(4096, 0);
}

AtariSystem1Lean::~AtariSystem1Lean() {
    if (ready_) as1_release(&lean_->core);
    // (Road Runner: the core's frame_mo is one of the three.)
    for (as1_mo_list*& list : mo_) free_mo_list(list);
}

const char* AtariSystem1Lean::title() const {
    return game_ == Game::RoadRunner ? "Road Runner" : "Indiana Jones and the Temple of Doom";
}

bool AtariSystem1Lean::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    as1_roms roms{};
    roms.game = game_ == Game::RoadRunner ? AS1_ROADRUNN : AS1_INDYTEMP;
    roms.main = main_;
    roms.sound = sound_;
    roms.chars = chars_;
    roms.proms = proms_;
    for (int bank = 0; bank < AS1_GFX_BANKS; bank++) roms.tiles[bank] = tiles_[bank];
    as1_core& c = lean_->core;
    as1_init(&c, &roms, kSampleRate);
    c.idle_skip = true;
    c.audio = frame_audio_.data();
    c.audio_cap = int(frame_audio_.size());
    c.on_frame = &AtariSystem1Lean::on_line_239;
    c.user = this;
    for (int i = 0; i < 3; i++) {
        mo_[i] = (i == 0 && c.frame_mo) ? c.frame_mo : static_cast<as1_mo_list*>(std::calloc(1, sizeof(as1_mo_list)));
        if (mo_[i] == nullptr) {
            if (error) *error = "out of memory";
            return false;
        }
    }
    mo_build_ = 0;
    if (c.frame_mo) c.frame_mo = mo_[0];
    ready_ = true;
    reset();
    return true;
}

bool AtariSystem1Lean::load_roms(const std::string& rom_path, std::string* error) {
    const bool rr = game_ == Game::RoadRunner;
    const as1_game g = rr ? AS1_ROADRUNN : AS1_INDYTEMP;
    RomLoader game_loader;
    if (!game_loader.open(rom_path, error)) return false;

    // A prepared set (tools/lean_roms): the images as they are.
    main_ = prepared(game_loader, kPreparedMain, AS1_MAIN_IMAGE, main_rom_);
    if (main_ != nullptr) {
        sound_ = prepared(game_loader, kPreparedSound, kSoundBytes, sound_rom_);
        chars_ = prepared(game_loader, kPreparedChars, kCharBytes, char_rom_);
        proms_ = prepared(game_loader, kPreparedProms, kPromBytes, prom_data_);
        if (!sound_ || !chars_ || !proms_) {
            if (error) *error = "incomplete prepared System 1 set (lean_*.bin)";
            return false;
        }
        tile_rows_.assign(AS1_GFX_BANKS, {});
        for (int bank = 1; bank < AS1_GFX_BANKS; bank++) {
            std::vector<uint8_t> own;
            const std::string name = prepared_bank_name(bank);
            const uint8_t* data = prepared(game_loader, name.c_str(), kBankBytes, own);
            if (data == nullptr) continue;
            if (!own.empty() || (reinterpret_cast<uintptr_t>(data) & 3) != 0) {
                tile_rows_[size_t(bank)].assign(kBankBytes / 4, 0);
                std::memcpy(tile_rows_[size_t(bank)].data(), data, kBankBytes);
                tiles_[bank] = tile_rows_[size_t(bank)].data();
            } else {
                tiles_[bank] = reinterpret_cast<const uint32_t*>(data);
            }
        }
        return true;
    }

    // The System 1 BIOS: in the game's set or in atarisy1.zip beside it.
    RomLoader bios_loader;
    namespace fs = dsp::fsx;
    const fs::path given(rom_path);
    const fs::path sibling = given.has_parent_path() ? given.parent_path() / "atarisy1.zip"
                                                     : fs::path("atarisy1.zip");
    const bool bios_separate = try_open_loader(bios_loader, sibling.string());

    auto load = [&](const RomEntry& entry, std::vector<uint8_t>& data) {
        const RomEntry single{entry.name, entry.length, 0, entry.crc};
        data.assign(entry.length, 0);
        std::string attempt;
        if (game_loader.load({single}, data, &attempt)) return true;
        if (bios_separate && bios_loader.load({single}, data, &attempt)) return true;
        if (error) *error = attempt;
        return false;
    };
    // 68000 code: the entries' offsets are byte addresses of the image, the
    // low bit choosing the high (even) or low (odd) byte of each word.
    auto load_words = [&](const std::vector<RomEntry>& entries, std::vector<uint8_t>& image) {
        std::vector<uint8_t> data;
        for (const RomEntry& entry : entries) {
            if (!load(entry, data)) return false;
            for (uint32_t k = 0; k < entry.length; k++)
                image[(entry.offset & ~1u) + 2 * k + (entry.offset & 1u)] = data[k];
        }
        return true;
    };

    main_rom_.assign(AS1_MAIN_IMAGE, 0);
    if (!load_words(kBiosRoms, main_rom_)) return false;
    if (rr) {
        if (!load_words(kRoadRom, main_rom_)) return false;   // at its absolute offsets
    } else {
        // Indy: program at $10000 (0x28000 bytes), the SLAPSTIC banks at $80000.
        std::vector<uint8_t> game(0x30000, 0);
        if (!load_words(kIndyRom, game)) return false;
        std::memcpy(&main_rom_[0x10000], game.data(), 0x28000);
        std::memcpy(&main_rom_[0x80000], &game[0x28000], 0x8000);
    }
    main_ = main_rom_.data();

    std::vector<uint8_t> data;
    sound_rom_.assign(kSoundBytes, 0);
    for (const RomEntry& entry : rr ? kRoadSound : kIndySound) {
        if (!load(entry, data)) return false;
        std::memcpy(&sound_rom_[entry.offset - 0x4000], data.data(), entry.length);
    }
    sound_ = sound_rom_.data();
    if (!load(kCharRoms[0], char_rom_)) return false;
    chars_ = char_rom_.data();
    prom_data_.assign(kPromBytes, 0);
    for (const RomEntry& entry : rr ? kRoadProms : kIndyProms) {
        if (!load(entry, data)) return false;
        std::memcpy(&prom_data_[entry.offset], data.data(), entry.length);
    }
    proms_ = prom_data_.data();

    // Graphics: 16 KB chunks of the ROM image, packed to rows of 4bpp pixels
    // per bank.
    std::vector<std::vector<uint8_t>> back;
    std::vector<const uint8_t*> chunks(0x300000 / 0x4000, nullptr);
    for (const RomEntry& entry : rr ? kRoadBack : kIndyBack) {
        back.emplace_back();
        if (!load(entry, back.back())) return false;
        for (uint32_t o = 0; o < entry.length; o += 0x4000)
            if ((entry.offset + o) / 0x4000 < chunks.size()) chunks[(entry.offset + o) / 0x4000] = back.back().data() + o;
    }
    auto chunk_of = [](void* u, int i) -> const uint8_t* {
        const auto& list = *static_cast<std::vector<const uint8_t*>*>(u);
        return i >= 0 && size_t(i) < list.size() ? list[size_t(i)] : nullptr;
    };
    tile_rows_.assign(AS1_GFX_BANKS, {});
    for (int bank = 1; bank < AS1_GFX_BANKS && 0x80000u * uint32_t(bank - 1) < as1_gfx_bytes(g); bank++) {
        std::vector<uint32_t>& rows = tile_rows_[size_t(bank)];
        rows.assign(size_t(AS1_TILES_PER_BANK) * 8, 0);
        if (as1_pack_bank(chunk_of, &chunks, bank, rows.data())) tiles_[bank] = rows.data();
        else rows.clear();
    }

    warnings_ = game_loader.warnings();
    if (bios_separate)
        warnings_.insert(warnings_.end(), bios_loader.warnings().begin(), bios_loader.warnings().end());
    return true;
}

bool AtariSystem1Lean::write_prepared(const std::string& dir, std::string* error) const {
    if (!ready_) {
        if (error) *error = "no ROM set loaded";
        return false;
    }
    if (!write_file(dir + "/" + kPreparedMain, main_, AS1_MAIN_IMAGE, error) ||
        !write_file(dir + "/" + kPreparedSound, sound_, kSoundBytes, error) ||
        !write_file(dir + "/" + kPreparedChars, chars_, kCharBytes, error) ||
        !write_file(dir + "/" + kPreparedProms, proms_, kPromBytes, error))
        return false;
    for (int bank = 1; bank < AS1_GFX_BANKS; bank++)
        if (tiles_[bank] && !write_file(dir + "/" + prepared_bank_name(bank), tiles_[bank], kBankBytes, error))
            return false;
    return true;
}

void AtariSystem1Lean::reset() {
    if (!ready_) return;
    as1_reset(&lean_->core);
    audio_.clear();
}

void AtariSystem1Lean::run_frame() {
    if (!ready_) return;
    as1_run_frame(&lean_->core);
    audio_.insert(audio_.end(), frame_audio_.begin(), frame_audio_.begin() + lean_->core.audio_n);
}

void AtariSystem1Lean::set_audio_enabled(bool enabled) {
    lean_->core.audio_enabled = enabled;
}

// Line 239: the palette and the motion objects of the frame (Road Runner's
// were taken by the core as the RAM changed, Indy's are taken now), and its
// picture when the frame buffer is in use.
void AtariSystem1Lean::on_line_239(void* user, as1_core* core) {
    AtariSystem1Lean* self = static_cast<AtariSystem1Lean*>(user);
    for (int i = 0; i < 0x400; i++) self->palette_[i] = 0xff000000u | as1_palette_rgb(&core->v, i);
    if (core->frame_mo == nullptr) as1_build_mo_list(&core->gfx, &core->v, self->mo_[self->mo_build_]);
    const int finished = self->mo_build_;
    self->mo_ready_.store(finished);
    int next = 0;
    while (next == finished || next == self->mo_used_.load()) next++;
    self->mo_build_ = next;
    if (core->frame_mo) core->frame_mo = self->mo_[next];   // (emptied when the next frame starts)
    if (self->draw_ && !self->framebuffer_.empty()) self->compose();
}

void AtariSystem1Lean::compose() const {
    const as1_core& c = lean_->core;
    const int ready = mo_ready_.load();
    if (ready < 0) return;
    constexpr int kBand = 16;
    uint16_t lines[kBand * kScreenWidth];
    for (int y = 0; y < kScreenHeight; y += kBand) {
        as1_compose(&c.gfx, &c.v, mo_[ready], y, y + kBand, lines);
        uint32_t* out = &framebuffer_[size_t(y) * kScreenWidth];
        for (int i = 0; i < kBand * kScreenWidth; i++) out[i] = palette_[lines[i] & 0x3ff];
    }
}

const uint32_t* AtariSystem1Lean::framebuffer() const {
    if (framebuffer_.empty()) {
        framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
        if (ready_) compose();
    }
    return framebuffer_.data();
}

void AtariSystem1Lean::rows(const void* source, int y, int n, uint16_t* pens) {
    const AtariSystem1Lean* self = static_cast<const AtariSystem1Lean*>(source);
    if (y <= self->rows_y_) {   // a new picture: the last finished list
        self->mo_used_.store(self->mo_ready_.load());
        const int used = self->mo_used_.load();
        self->mo_draw_ = used >= 0 ? self->mo_[used] : nullptr;
    }
    self->rows_y_ = y;
    if (self->mo_draw_ == nullptr) {
        std::memset(pens, 0, sizeof(uint16_t) * size_t(n) * kScreenWidth);
        return;
    }
    as1_compose(&self->lean_->core.gfx, &self->lean_->core.v, self->mo_draw_, y, y + n, pens);
}

Machine::PenRows AtariSystem1Lean::pen_rows() const {
    if (!ready_) return {};
    return {&AtariSystem1Lean::rows, this, palette_, 0x400};
}

void AtariSystem1Lean::set_inputs(const MachineInputs& inputs) {
    if (!ready_) return;
    as1_inputs in{};
    in.coin1 = inputs.coin1;
    in.coin2 = inputs.coin2;
    in.start = inputs.player1.start || inputs.player2.start;
    in.button1 = inputs.player1.button1;
    in.button2 = inputs.player1.button2;
    in.up = inputs.player1.up;
    in.down = inputs.player1.down;
    in.left = inputs.player1.left;
    in.right = inputs.player1.right;
    as1_set_inputs(&lean_->core, &in);
}

void AtariSystem1Lean::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

uint32_t AtariSystem1Lean::debug_skipped_main() const { return lean_->core.skipped; }
uint32_t AtariSystem1Lean::debug_skipped_sound() const { return lean_->core.snd_skipped; }

}  // namespace dsp
