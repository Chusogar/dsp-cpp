#include "drivers/neogeo.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

void byteswap_words(std::vector<uint8_t>& data) {
    for (size_t i = 0; i + 1 < data.size(); i += 2) std::swap(data[i], data[i + 1]);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

bool has_part(const std::string& name, const std::string& part) {
    // Match MAME names such as 201-p1.p1, 201-p1.bin, p1.bin, 242-p2.sp2.
    if (name.size() < part.size()) return false;
    if (name == part || name == part + ".bin" || name == part + ".rom") return true;
    if (name.size() >= part.size() + 1 && name.compare(name.size() - part.size(), part.size(), part) == 0) {
        const char prev = name[name.size() - part.size() - 1];
        if (prev == '.' || prev == '-' || prev == '_') return true;
    }
    const std::string tagged = "-" + part + ".";
    const std::string tagged2 = "." + part + ".";
    return contains(name, tagged) || contains(name, tagged2);
}

bool is_bios_name(const std::string& name) {
    static const char* kNames[] = {
        "sp-s2.sp1", "sp-s.sp1", "sp-e.sp1", "sp-j2.sp1", "sp-j3.bin", "sp1-u4.bin",
        "usa_2slt.bin", "asia-s3.rom", "vs-bios.rom", "sp-s3.sp1", "uni-bios.rom",
        "uni-bios_4_0.rom", "uni-bios_3_3.rom", "uni-bios_3_2.rom", "uni-bios_3_1.rom",
        "uni-bios_3_0.rom", "uni-bios_2_3.rom", "uni-bios_2_2.rom", "uni-bios_2_1.rom",
        "uni-bios_2_0.rom", "uni-bios_1_3.rom", "uni-bios_1_2.rom", "uni-bios_1_1.rom",
        "uni-bios_1_0.rom", "neogeo.rom", "neo-geo.rom", "sp1.sp1",
    };
    for (const char* known : kNames) {
        if (name == known) return true;
    }
    return contains(name, "uni-bios") || contains(name, "vs-bios") || contains(name, "asia-s3") ||
           (contains(name, "sp-s") && contains(name, "sp1")) || contains(name, "sp-e.sp1");
}

int p_rank(const std::string& name) {
    if (has_part(name, "p1") || has_part(name, "pg1")) return 1;
    if (has_part(name, "p2") || has_part(name, "pg2")) return 2;
    if (has_part(name, "sp2")) return 3;
    if (has_part(name, "p3")) return 4;
    if (has_part(name, "p4")) return 5;
    return 9;
}

int c_rank(const std::string& name) {
    for (int i = 8; i >= 1; i--) {
        if (has_part(name, "c" + std::to_string(i))) return i;
    }
    return 9;
}

int v_rank(const std::string& name) {
    for (int i = 4; i >= 1; i--) {
        if (has_part(name, "v" + std::to_string(i))) return i;
    }
    return 9;
}

std::string title_for(const std::string& game) {
    static const std::map<std::string, const char*> kTitles = {
        {"neogeo", "NeoGeo MVS"},
        {"mvs", "NeoGeo MVS"},
        {"nam1975", "NAM-1975"},
        {"maglord", "Magician Lord"},
        {"bstars", "Baseball Stars Professional"},
        {"tpgolf", "Top Player's Golf"},
        {"ridhero", "Riding Hero"},
        {"alpham2", "Alpha Mission II"},
        {"ncombat", "Ninja Combat"},
        {"cyberlip", "Cyber-Lip"},
        {"superspy", "The Super Spy"},
        {"mutnat", "Mutation Nation"},
        {"kotm", "King of the Monsters"},
        {"sengoku", "Sengoku"},
        {"burningf", "Burning Fight"},
        {"lbowling", "League Bowling"},
        {"gpilots", "Ghost Pilots"},
        {"joyjoy", "Puzzled"},
        {"bjourney", "Blue's Journey"},
        {"lresort", "Last Resort"},
        {"eightman", "Eight Man"},
        {"legendos", "Legend of Success Joe"},
        {"2020bb", "2020 Super Baseball"},
        {"socbrawl", "Soccer Brawl"},
        {"roboarmy", "Robo Army"},
        {"fatfury", "Fatal Fury"},
        {"fatfury1", "Fatal Fury"},
        {"fbfrenzy", "Football Frenzy"},
        {"kotm2", "King of the Monsters 2"},
        {"sengoku2", "Sengoku 2"},
        {"bstars2", "Baseball Stars 2"},
        {"3countb", "3 Count Bout"},
        {"aof", "Art of Fighting"},
        {"samsho", "Samurai Shodown"},
        {"tophuntr", "Top Hunter"},
        {"fatfury2", "Fatal Fury 2"},
        {"janshin", "Janshin Densetsu"},
        {"androdun", "Andro Dunos"},
        {"aodk", "Aggressors of Dark Kombat"},
        {"whp", "World Heroes Perfect"},
        {"kof94", "The King of Fighters '94"},
        {"kof95", "The King of Fighters '95"},
        {"kof96", "The King of Fighters '96"},
        {"kof97", "The King of Fighters '97"},
        {"kof98", "The King of Fighters '98"},
        {"lastblad", "The Last Blade"},
        {"lastbld2", "The Last Blade 2"},
        {"mslug", "Metal Slug"},
        {"mslug2", "Metal Slug 2"},
        {"mslugx", "Metal Slug X"},
        {"mslug3", "Metal Slug 3"},
        {"garou", "Garou: Mark of the Wolves"},
        {"rbff1", "Real Bout Fatal Fury"},
        {"rbff2", "Real Bout Fatal Fury 2"},
        {"rbffspec", "Real Bout Fatal Fury Special"},
        {"samsho2", "Samurai Shodown II"},
        {"samsho3", "Samurai Shodown III"},
        {"samsho4", "Samurai Shodown IV"},
        {"aof2", "Art of Fighting 2"},
        {"aof3", "Art of Fighting 3"},
        {"fatfury3", "Fatal Fury 3"},
        {"pulstar", "Pulstar"},
        {"blazstar", "Blazing Star"},
        {"breakers", "Breakers"},
        {"pbobblen", "Puzzle Bobble"},
        {"pbobbl2n", "Puzzle Bobble 2"},
        {"wakuwak7", "Waku Waku 7"},
        {"twinspri", "Twinkle Star Sprites"},
        {"viewpoin", "Viewpoint"},
        {"ncommand", "Ninja Commando"},
        {"trally", "Thrash Rally"},
        {"crsword", "Crossed Swords"},
        {"ctomaday", "Captain Tomaday"},
        {"ganryu", "Ganryu"},
        {"bangbead", "Bang Bead"},
        {"preisle2", "Prehistoric Isle 2"},
        {"sengoku3", "Sengoku 3"},
        {"zupapa", "Zupapa!"},
    };
    auto it = kTitles.find(game);
    return it == kTitles.end() ? "NeoGeo" : it->second;
}

uint8_t pad_bits(const InputState& pad) {
    uint8_t value = 0xff;
    if (pad.up) value &= ~0x01;
    if (pad.down) value &= ~0x02;
    if (pad.left) value &= ~0x04;
    if (pad.right) value &= ~0x08;
    if (pad.button1) value &= ~0x10;  // A
    if (pad.button2) value &= ~0x20;  // B
    if (pad.button3) value &= ~0x40;  // C
    if (pad.button4) value &= ~0x80;  // D
    return value;
}

bool try_open(RomLoader& loader, const std::string& path) {
    std::string ignored;
    return loader.open(path, &ignored);
}

void append_file(std::vector<uint8_t>& dest, const std::vector<uint8_t>& src) {
    dest.insert(dest.end(), src.begin(), src.end());
}

std::vector<uint8_t> read_named(RomLoader& loader, const std::string& name) {
    std::vector<uint8_t> data;
    loader.try_read(name, data);
    return data;
}

uint64_t packed_rtc_time() {
    // uPD4990A 52-bit BCD stream, LSB first: sec, min, hour, day, weekday, month, year.
    const uint64_t sec = 0x00;
    const uint64_t min = 0x00;
    const uint64_t hour = 0x21;
    const uint64_t day = 0x24;
    const uint64_t weekday = 0x01;
    const uint64_t month = 0x08;
    const uint64_t year = 0x26;
    return sec | (min << 8) | (hour << 16) | (day << 24) | (weekday << 32) | (month << 36) |
           (year << 40);
}

bool neo_geo_header_at(const std::vector<uint8_t>& rom, size_t offset) {
    static const uint8_t kMark[] = {'N', 'E', 'O', '-', 'G', 'E', 'O'};
    if (offset + sizeof(kMark) > rom.size()) return false;
    return std::memcmp(rom.data() + offset, kMark, sizeof(kMark)) == 0;
}

}  // namespace

bool NeoGeo::is_game_name(const std::string& name) {
    if (name == "neogeo" || name == "mvs" || name == "aes") return true;
    return title_for(name) != std::string("NeoGeo") || name.find("kof") == 0 ||
           name.find("mslug") == 0 || name.find("fatfury") == 0 || name.find("samsho") == 0;
}

NeoGeo::NeoGeo(std::string game_name)
    : game_name_(lower(std::move(game_name))), title_(title_for(game_name_)) {
    framebuffer_.assign(size_t(NeoGeoVideo::kScreenWidth) * NeoGeoVideo::kScreenHeight, 0xff000000u);

    m68k_.set_memory_handlers([this](uint32_t a) { return read_word(a); },
                              [this](uint32_t a, uint16_t v) { write_word(a, v); });
    m68k_.set_byte_handlers([this](uint32_t a) { return read_byte(a); },
                            [this](uint32_t a, uint8_t v) { write_byte(a, v); });
    m68k_.set_cycle_handler([this](int cycles) { on_m68k_cycles(cycles); });

    z80_.set_memory_handlers([this](uint16_t a) { return z80_read(a); },
                             [this](uint16_t a, uint8_t v) { z80_write(a, v); });
    z80_.set_io_handlers([this](uint16_t p) { return z80_in(p); },
                         [this](uint16_t p, uint8_t v) { z80_out(p, v); });
}

bool NeoGeo::init(const std::string& rom_path, std::string* error) {
    if (!load_roms(rom_path, error)) return false;
    reset();
    return true;
}

bool NeoGeo::load_synthetic(std::vector<uint8_t> bios, std::vector<uint8_t> sfix,
                            std::vector<uint8_t> sm1, std::vector<uint8_t> p_rom,
                            std::vector<uint8_t> s_rom, std::vector<uint8_t> m_rom,
                            std::vector<uint8_t> c_rom, std::vector<uint8_t> v_rom,
                            std::string* error) {
    (void)error;
    bios_ = std::move(bios);
    sfix_ = std::move(sfix);
    sm1_ = std::move(sm1);
    p_rom_ = std::move(p_rom);
    s_rom_ = std::move(s_rom);
    m_rom_ = std::move(m_rom);
    c_rom_ = std::move(c_rom);
    v_rom_ = std::move(v_rom);
    bios_present_ = bios_.size() >= 8;
    finish_load(false);
    reset();
    return true;
}

void NeoGeo::finish_load(bool byteswap_program) {
    if (byteswap_program) {
        byteswap_words(bios_);
        byteswap_words(p_rom_);
    }
    if (bios_.size() & 1) bios_.push_back(0);
    if (p_rom_.size() & 1) p_rom_.push_back(0);
    normalize_program_rom();
    if (p_rom_.size() > 0x100000) cart_bank_ = 0x100000;
    else cart_bank_ = 0;

    video_.set_fix_roms(s_rom_.data(), s_rom_.size(), sfix_.data(), sfix_.size());
    video_.set_sprite_rom(c_rom_.data(), c_rom_.size());
    video_.set_lo_rom(lo_.data(), lo_.size());
    video_.decode_graphics();

    ym_.set_adpcm_a_rom(v_rom_);
    ym_.set_adpcm_b_rom(v_rom_);
}

void NeoGeo::normalize_program_rom() {
    // Several 2 MiB P1 dumps (Metal Slug, KOF 94, ...) store the fixed 1 MiB
    // bank in the second half of the file. MAME loads that with ROM_CONTINUE.
    if (p_rom_.size() < 0x200000) return;
    if (neo_geo_header_at(p_rom_, 0x100)) return;
    if (!neo_geo_header_at(p_rom_, 0x100100)) return;
    std::vector<uint8_t> rotated(p_rom_.size());
    std::copy(p_rom_.begin() + 0x100000, p_rom_.begin() + 0x200000, rotated.begin());
    std::copy(p_rom_.begin(), p_rom_.begin() + 0x100000, rotated.begin() + 0x100000);
    if (p_rom_.size() > 0x200000) {
        std::copy(p_rom_.begin() + 0x200000, p_rom_.end(), rotated.begin() + 0x200000);
    }
    p_rom_ = std::move(rotated);
}

bool NeoGeo::load_roms(const std::string& rom_path, std::string* error) {
    RomLoader game;
    if (!game.open(rom_path, error)) return false;

    RomLoader bios_loader;
    bool have_bios_zip = false;
    namespace fs = std::filesystem;
    const fs::path given(rom_path);
    const fs::path parent = given.has_parent_path() ? given.parent_path() : fs::path(".");
    const fs::path siblings[] = {parent / "neogeo.zip", fs::path("neogeo.zip"), given};
    for (const fs::path& path : siblings) {
        if (try_open(bios_loader, path.string())) {
            have_bios_zip = true;
            break;
        }
    }

    auto load_from = [&](RomLoader& loader, const char* const* names, std::vector<uint8_t>& dest) -> bool {
        for (int i = 0; names[i] != nullptr; i++) {
            if (loader.try_read(names[i], dest) && !dest.empty()) return true;
        }
        return false;
    };

    static const char* kBios[] = {
        "sp-s2.sp1", "sp-s.sp1", "sp-e.sp1", "usa_2slt.bin", "asia-s3.rom", "vs-bios.rom",
        "uni-bios.rom", "uni-bios_4_0.rom", "uni-bios_3_3.rom", "sp-j3.bin", "neo-geo.rom",
        "neogeo.rom", nullptr};
    static const char* kSfix[] = {"sfix.sfix", "sfix.rom", "sf.sfix", nullptr};
    static const char* kSm1[] = {"sm1.sm1", "sm1.rom", nullptr};
    static const char* kLo[] = {"000-lo.lo", "lo.lo", "000-lo.rom", nullptr};

    auto try_bios_piece = [&](const char* const* names, std::vector<uint8_t>& dest) {
        if (load_from(game, names, dest)) return true;
        if (have_bios_zip && load_from(bios_loader, names, dest)) return true;
        return false;
    };

    try_bios_piece(kBios, bios_);
    try_bios_piece(kSfix, sfix_);
    try_bios_piece(kSm1, sm1_);
    try_bios_piece(kLo, lo_);

    std::vector<std::string> files = game.filenames();
    std::vector<std::string> p_files, s_files, m_files, c_files, v_files;
    for (const std::string& name : files) {
        if (is_bios_name(name) || contains(name, "sfix") || contains(name, "sm1") ||
            contains(name, "000-lo") || contains(name, "000-hi")) {
            continue;
        }
        if (has_part(name, "s1") || has_part(name, "s2")) s_files.push_back(name);
        else if (has_part(name, "m1")) m_files.push_back(name);
        else if (c_rank(name) < 9) c_files.push_back(name);
        else if (v_rank(name) < 9) v_files.push_back(name);
        else if (p_rank(name) < 9) p_files.push_back(name);
    }

    std::sort(p_files.begin(), p_files.end(), [](const std::string& a, const std::string& b) {
        const int ra = p_rank(a), rb = p_rank(b);
        if (ra != rb) return ra < rb;
        return a < b;
    });
    std::sort(c_files.begin(), c_files.end(), [](const std::string& a, const std::string& b) {
        const int ra = c_rank(a), rb = c_rank(b);
        if (ra != rb) return ra < rb;
        return a < b;
    });
    std::sort(v_files.begin(), v_files.end(), [](const std::string& a, const std::string& b) {
        const int ra = v_rank(a), rb = v_rank(b);
        if (ra != rb) return ra < rb;
        return a < b;
    });

    p_rom_.clear();
    for (const std::string& name : p_files) append_file(p_rom_, read_named(game, name));
    s_rom_.clear();
    for (const std::string& name : s_files) append_file(s_rom_, read_named(game, name));
    m_rom_.clear();
    for (const std::string& name : m_files) append_file(m_rom_, read_named(game, name));
    v_rom_.clear();
    for (const std::string& name : v_files) append_file(v_rom_, read_named(game, name));

    c_rom_.clear();
    for (size_t i = 0; i < c_files.size(); i += 2) {
        const std::vector<uint8_t> even = read_named(game, c_files[i]);
        const std::vector<uint8_t> odd =
            i + 1 < c_files.size() ? read_named(game, c_files[i + 1]) : std::vector<uint8_t>(even.size(), 0);
        const size_t n = std::max(even.size(), odd.size());
        const size_t offset = c_rom_.size();
        c_rom_.resize(offset + n * 2, 0);
        for (size_t b = 0; b < n; b++) {
            if (b < even.size()) c_rom_[offset + b * 2] = even[b];
            if (b < odd.size()) c_rom_[offset + b * 2 + 1] = odd[b];
        }
    }

    if (bios_.empty() && p_rom_.empty()) {
        if (error) *error = "no NeoGeo BIOS or program ROM found (need neogeo.zip plus a cart zip)";
        return false;
    }
    bios_present_ = !bios_.empty();
    if (bios_.empty()) {
        warnings_.emplace_back("NeoGeo BIOS not found; place neogeo.zip next to the game zip");
        bios_.assign(0x20000, 0);
    }
    if (p_rom_.empty()) {
        // BIOS-only: mirror the BIOS into the cart program area so reset works.
        p_rom_ = bios_;
        p_rom_.resize(0x100000, 0);
    }
    if (s_rom_.empty()) s_rom_ = sfix_;
    if (m_rom_.empty()) m_rom_ = sm1_;

    finish_load(true);
    if (!p_files.empty() && !neo_geo_header_at(p_rom_, 0x100)) {
        warnings_.emplace_back("cartridge has no NEO-GEO header at $100; P-ROM layout may be wrong");
    }
    for (const auto& warning : game.warnings()) warnings_.push_back(warning);
    return true;
}

void NeoGeo::reset() {
    ram_.fill(0);
    z80_ram_.fill(0);
    sound_latch_ = 0;
    sound_reply_ = 0;
    sound_nmi_enabled_ = false;
    z80_bank_ = 0;
    watchdog_ = 0;
    sram_unlocked_ = false;
    bios_vectors_ = bios_present_;
    video_.reset();
    video_.set_use_bios_fix(true);
    ym_.reset();
    z80_.reset();
    m68k_.reset();
    update_irqs();
}

void NeoGeo::kick_watchdog() { watchdog_ = 0; }

uint8_t NeoGeo::p1_inputs() const { return pad_bits(inputs_.player1); }
uint8_t NeoGeo::p2_inputs() const { return pad_bits(inputs_.player2); }

uint8_t NeoGeo::status_a() const {
    uint8_t value = 0xff;
    if (inputs_.coin1) value &= ~0x01;
    if (inputs_.coin2) value &= ~0x02;
    if (service_) value &= ~0x04;
    if (rtc_pulse_) value &= ~0x40;
    if ((rtc_shift_ & 1) == 0) value &= ~0x80;
    return value;
}

uint8_t NeoGeo::status_b() const {
    // Bit 7 set = MVS. Start/select are active low. Bits 5-4 = no memory card.
    uint8_t value = 0xf0;
    if (!inputs_.player1.start) value |= 0x01;
    if (!inputs_.player1.select) value |= 0x02;
    if (!inputs_.player2.start) value |= 0x04;
    if (!inputs_.player2.select) value |= 0x08;
    return value;
}

void NeoGeo::rtc_write(uint8_t value) {
    const uint8_t prev = rtc_ctrl_;
    rtc_ctrl_ = value;
    const bool clk = (value & 0x02) != 0;
    const bool stb = (value & 0x04) != 0;
    if (clk && (prev & 0x02) == 0) {
        rtc_command_ = uint8_t((rtc_command_ >> 1) | ((value & 1) << 3));
        rtc_bits_++;
        rtc_shift_ >>= 1;
    }
    if (stb && (prev & 0x04) == 0) {
        // Rising strobe latches the 4-bit command. 3 = time read.
        if ((rtc_command_ & 0x0f) == 0x03 || rtc_bits_ >= 4) {
            if ((rtc_command_ & 0x0f) == 0x03) rtc_shift_ = packed_rtc_time();
        }
        rtc_bits_ = 0;
    }
}

void NeoGeo::set_inputs(const MachineInputs& inputs) { inputs_ = inputs; }

void NeoGeo::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = value;
    if (bank == 1) service_ = (value & 1) != 0;
}

const uint8_t* NeoGeo::z80_rom() const {
    if (video_.use_bios_fix() && !sm1_.empty()) return sm1_.data();
    if (!m_rom_.empty()) return m_rom_.data();
    if (!sm1_.empty()) return sm1_.data();
    return nullptr;
}

uint8_t NeoGeo::z80_read(uint16_t address) {
    if (address < 0x8000) {
        const uint8_t* rom = z80_rom();
        if (rom == nullptr) return 0xff;
        const size_t size = video_.use_bios_fix() && !sm1_.empty() ? sm1_.size() : m_rom_.empty() ? sm1_.size() : m_rom_.size();
        if (size == 0) return 0xff;
        return rom[address % size];
    }
    if (address < 0xc000) {
        const uint8_t* rom = z80_rom();
        const size_t size = video_.use_bios_fix() && !sm1_.empty() ? sm1_.size() : m_rom_.empty() ? sm1_.size() : m_rom_.size();
        if (rom == nullptr || size == 0) return 0xff;
        const uint32_t offset = (uint32_t(z80_bank_ & 0x0f) * 0x4000 + (address - 0x8000)) % size;
        return rom[offset];
    }
    return z80_ram_[address & 0x1fff];
}

void NeoGeo::z80_write(uint16_t address, uint8_t value) {
    if (address >= 0xc000) z80_ram_[address & 0x1fff] = value;
}

uint8_t NeoGeo::z80_in(uint16_t port) {
    switch (port & 0xff) {
        case 0x00:
            return sound_latch_;
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
            return ym_.read(port & 3);
        default:
            return 0;
    }
}

void NeoGeo::z80_out(uint16_t port, uint8_t value) {
    const uint8_t low = uint8_t(port & 0xff);
    switch (low) {
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
            ym_.write(low & 3, value);
            return;
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
            sound_nmi_enabled_ = true;
            z80_bank_ = uint8_t((port >> 8) & 0x0f);
            return;
        case 0x0c:
            sound_reply_ = value;
            return;
        case 0x18:
            sound_nmi_enabled_ = false;
            return;
        default:
            if ((low & 0xf0) != 0 && (low & 0x0f) == 0x08) {
                z80_bank_ = uint8_t((low >> 4) & 0x0f);
            }
            return;
    }
}

uint8_t NeoGeo::read_byte(uint32_t address) {
    address &= 0xffffff;
    const uint16_t word = read_word(address & ~1u);
    return (address & 1) ? uint8_t(word) : uint8_t(word >> 8);
}

void NeoGeo::write_byte(uint32_t address, uint8_t value) {
    address &= 0xffffff;
    if (address >= 0x300000 && address < 0x3c0000) {
        if ((address & 0xfe0000) == 0x300000 && (address & 1) != 0) {
            kick_watchdog();
            return;
        }
        if ((address & 0xfe0000) == 0x320000 && (address & 1) == 0) {
            sound_latch_ = value;
            if (sound_nmi_enabled_) z80_.set_nmi(IrqLine::Pulse);
            return;
        }
        if ((address & 0xfe0000) == 0x380000 && (address & 1) != 0) {
            if ((address & 0x70) == 0x50) rtc_write(value);
            return;
        }
        if ((address & 0xfe0000) == 0x3a0000 && (address & 1) != 0) {
            const int latch = (address >> 1) & 7;
            const bool bit = (address & 0x10) != 0;
            switch (latch) {
                case 1:
                    bios_vectors_ = !bit;
                    break;
                case 5:
                    video_.set_use_bios_fix(!bit);
                    break;
                case 6:
                    sram_unlocked_ = bit;
                    break;
                case 7:
                    // $3A000F selects bank 1, $3A001F selects bank 0.
                    video_.set_palette_bank(bit ? 0 : 1);
                    break;
                default:
                    break;
            }
            return;
        }
    }
    const uint32_t aligned = address & ~1u;
    uint16_t word = read_word(aligned);
    if (address & 1) word = uint16_t((word & 0xff00) | value);
    else word = uint16_t((word & 0x00ff) | (uint16_t(value) << 8));
    write_word(aligned, word);
}

uint16_t NeoGeo::read_word(uint32_t address) {
    address &= 0xfffffe;
    if (address < 0x80 && bios_vectors_ && bios_.size() >= 8) {
        if (address + 1 < bios_.size()) {
            return uint16_t((bios_[address] << 8) | bios_[address + 1]);
        }
    }
    if (address < 0x100000) {
        if (p_rom_.empty()) return 0xffff;
        const uint32_t offset = address % uint32_t(p_rom_.size());
        return uint16_t((p_rom_[offset] << 8) | p_rom_[offset + 1]);
    }
    if (address < 0x200000) {
        const uint32_t offset = address & 0xffff;
        return uint16_t((ram_[offset] << 8) | ram_[offset + 1]);
    }
    if (address < 0x300000) {
        if (address >= 0x2ffff0) return 0xffff;
        if (p_rom_.empty()) return 0xffff;
        uint32_t offset = cart_bank_ + (address - 0x200000);
        if (offset + 1 >= p_rom_.size()) offset %= uint32_t(p_rom_.size());
        return uint16_t((p_rom_[offset] << 8) | p_rom_[offset + 1]);
    }
    if (address < 0x320000) {
        return uint16_t((uint16_t(p1_inputs()) << 8) | dsw_);
    }
    if (address < 0x340000) {
        return uint16_t((uint16_t(sound_reply_) << 8) | status_a());
    }
    if (address < 0x360000) {
        return uint16_t(uint16_t(p2_inputs()) << 8);
    }
    if (address < 0x3a0000) {
        return uint16_t(uint16_t(status_b()) << 8);
    }
    if (address >= 0x3c0000 && address < 0x3e0000) {
        return video_.read_register(address);
    }
    if (address >= 0x400000 && address < 0x402000) {
        return video_.read_palette(address);
    }
    if (address >= 0x800000 && address < 0x801000) {
        return uint16_t(0xff00 | memcard_[(address >> 1) & 0x7ff]);
    }
    if (address >= 0xc00000 && address < 0xc20000) {
        if (bios_.empty()) return 0xffff;
        const uint32_t offset = (address - 0xc00000) % uint32_t(bios_.size());
        return uint16_t((bios_[offset] << 8) | bios_[offset + 1]);
    }
    if (address >= 0xd00000 && address < 0xd10000) {
        const uint32_t offset = address & 0xffff;
        return uint16_t((sram_[offset] << 8) | sram_[offset + 1]);
    }
    return 0xffff;
}

void NeoGeo::write_word(uint32_t address, uint16_t value) {
    address &= 0xfffffe;
    if (address >= 0x100000 && address < 0x200000) {
        const uint32_t offset = address & 0xffff;
        ram_[offset] = uint8_t(value >> 8);
        ram_[offset + 1] = uint8_t(value);
        return;
    }
    if (address >= 0x200000 && address < 0x300000) {
        if (address >= 0x2ffff0) {
            cart_bank_ = uint32_t(value & 0xff) * 0x100000;
            if (p_rom_.size() > 0x100000) {
                const uint32_t max_bank = uint32_t((p_rom_.size() - 1) / 0x100000) * 0x100000;
                if (cart_bank_ > max_bank) cart_bank_ = max_bank;
            } else {
                cart_bank_ = 0;
            }
        }
        return;
    }
    if ((address & 0xfe0000) == 0x300000) {
        kick_watchdog();
        return;
    }
    if ((address & 0xfe0000) == 0x320000) {
        sound_latch_ = uint8_t(value >> 8);
        if (sound_nmi_enabled_) z80_.set_nmi(IrqLine::Pulse);
        return;
    }
    if ((address & 0xfe0000) == 0x3a0000) {
        write_byte(address | 1, uint8_t(value));
        return;
    }
    if (address >= 0x3c0000 && address < 0x3e0000) {
        video_.write_register(address, value);
        update_irqs();
        return;
    }
    if (address >= 0x400000 && address < 0x402000) {
        video_.write_palette(address, value);
        return;
    }
    if (address >= 0x800000 && address < 0x801000) {
        memcard_[(address >> 1) & 0x7ff] = uint8_t(value);
        return;
    }
    if (address >= 0xd00000 && address < 0xd10000 && sram_unlocked_) {
        const uint32_t offset = address & 0xffff;
        sram_[offset] = uint8_t(value >> 8);
        sram_[offset + 1] = uint8_t(value);
        return;
    }
}

void NeoGeo::update_irqs() {
    m68k_.set_irq(3, video_.irq_reset() ? IrqLine::Assert : IrqLine::Clear);
    m68k_.set_irq(2, video_.irq_vblank() ? IrqLine::Assert : IrqLine::Clear);
    m68k_.set_irq(1, video_.irq_timer() ? IrqLine::Assert : IrqLine::Clear);
}

void NeoGeo::on_m68k_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= kMainClock) {
        audio_accumulator_ -= kMainClock;
        int32_t sample = ym_.update();
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        audio_.push_back(int16_t(sample));
    }
}

void NeoGeo::run_frame() {
    watchdog_++;
    // Real NEO-B1 watchdog is about eight frames. Give the BIOS a little extra
    // room for the first SRAM / calendar pass after a cold start.
    if (watchdog_ > 16) {
        reset();
        watchdog_ = 0;
    }
    rtc_pulse_ = !rtc_pulse_;

    const int m68k_per_line = std::max(1, int(double(kMainClock) / kFramesPerSecond / kScanlines));
    const int z80_per_line = std::max(1, int(double(kZ80Clock) / kFramesPerSecond / kScanlines));

    video_.begin_frame();
    for (int line = 0; line < kScanlines; line++) {
        video_.end_scanline(line);
        update_irqs();
        m68k_.run(m68k_per_line);
        z80_.run(z80_per_line);
    }
    video_.render_frame(framebuffer_.data());
}

void NeoGeo::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
