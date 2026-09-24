#include "drivers/computers/apple2gs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kRomFC = {{"341-0728", 0x20000, 0x00000, 0x8d410067}};
const std::vector<RomEntry> kChrRom = {{"apple2gs.chr", 0x1000, 0, 0x91e53cd8}};

// statereg ($C068) bits, plus internal latches above bit 7.
constexpr uint32_t kAltZp = 0x80;
constexpr uint32_t kPage2 = 0x40;
constexpr uint32_t kRamRd = 0x20;
constexpr uint32_t kRamWrt = 0x10;
constexpr uint32_t kRdRom = 0x08;
constexpr uint32_t kLcBank2 = 0x04;
constexpr uint32_t kIntCx = 0x01;
constexpr uint32_t kPrewrite = 0x100;
constexpr uint32_t kWrDefRam = 0x200;
constexpr uint32_t kIntC8 = 0x400;

constexpr uint8_t kWrIgnore = 0x04;

// ADB $C027 bits.
constexpr uint8_t kC027MouseData = 0x80;
constexpr uint8_t kC027MouseInt = 0x40;
constexpr uint8_t kC027DataValid = 0x20;
constexpr uint8_t kC027DataInt = 0x10;
constexpr uint8_t kC027KbdValid = 0x08;
constexpr uint8_t kC027MouseCoord = 0x02;
constexpr uint8_t kC027CmdFull = 0x01;
constexpr uint8_t kC027NegMask =
    uint8_t(~(kC027MouseData | kC027DataValid | kC027KbdValid | kC027MouseCoord | kC027CmdFull));

// ADB keycode -> {unshifted, shifted, control} ASCII; values >= 0x100 are
// modifier bits for $C025 (shifted by 8), -1 means no character.
struct KeyAscii {
    int normal, shifted, ctrl;
};
const KeyAscii kKeyAscii[128] = {
    {'a', 'A', 0x01}, {'s', 'S', 0x13}, {'d', 'D', 0x04}, {'f', 'F', 0x06},
    {'h', 'H', 0x08}, {'g', 'G', 0x07}, {'z', 'Z', 0x1a}, {'x', 'X', 0x18},
    {'c', 'C', 0x03}, {'v', 'V', 0x16}, {-1, -1, -1},     {'b', 'B', 0x02},
    {'q', 'Q', 0x11}, {'w', 'W', 0x17}, {'e', 'E', 0x05}, {'r', 'R', 0x12},
    {'y', 'Y', 0x19}, {'t', 'T', 0x14}, {'1', '!', -1},   {'2', '@', 0x00},
    {'3', '#', -1},   {'4', '$', -1},   {'6', '^', 0x1e}, {'5', '%', -1},
    {'=', '+', -1},   {'9', '(', -1},   {'7', '&', -1},   {'-', '_', 0x1f},
    {'8', '*', -1},   {'0', ')', -1},   {']', '}', 0x1d}, {'o', 'O', 0x0f},
    {'u', 'U', 0x15}, {'[', '{', 0x1b}, {'i', 'I', 0x09}, {'p', 'P', 0x10},
    {0x0d, 0x0d, -1}, {'l', 'L', 0x0c}, {'j', 'J', 0x0a}, {0x27, '"', -1},
    {'k', 'K', 0x0b}, {';', ':', -1},   {0x5c, '|', 0x1c}, {',', '<', -1},
    {'/', '?', 0x7f}, {'n', 'N', 0x0e}, {'m', 'M', 0x0d}, {'.', '>', -1},
    {0x09, 0x09, -1}, {' ', ' ', -1},   {'`', '~', -1},   {0x7f, 0x7f, -1},
    {-1, -1, -1},     {0x1b, 0x1b, -1}, {0x0200, 0x0200, -1}, {0x8000, 0x8000, -1},
    {0x0100, 0x0100, -1}, {0x0400, 0x0400, -1}, {0x4000, 0x4000, -1}, {0x08, 0x08, -1},
    {0x15, 0x15, -1}, {0x0a, 0x0a, -1}, {0x0b, 0x0b, -1}, {-1, -1, -1},
    // 0x40-0x7f: keypad and function keys
    {-1, -1, -1}, {0x102e, 0x102e, -1}, {-1, -1, -1}, {0x102a, 0x102a, -1},
    {-1, -1, -1}, {0x102b, 0x102b, -1}, {-1, -1, -1}, {0x1018, 0x1018, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {0x102f, 0x102f, -1},
    {0x100d, 0x100d, -1}, {-1, -1, -1}, {0x102d, 0x102d, -1}, {-1, -1, -1},
    {-1, -1, -1}, {0x103d, 0x103d, -1}, {0x1030, 0x1030, -1}, {0x1031, 0x1031, -1},
    {0x1032, 0x1032, -1}, {0x1033, 0x1033, -1}, {0x1034, 0x1034, -1}, {0x1035, 0x1035, -1},
    {0x1036, 0x1036, -1}, {0x1037, 0x1037, -1}, {-1, -1, -1}, {0x1038, 0x1038, -1},
    {0x1039, 0x1039, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {0x1072, 0x1072, -1}, {0x1073, 0x1073, -1},
    {0x1074, 0x1074, -1}, {0x1075, 0x1075, -1}, {-1, -1, -1}, {0x1077, 0x1077, -1},
    {-1, -1, -1}, {0x1079, 0x1079, -1}, {-1, -1, -1}, {-1, -1, -1},
    {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
};

// Host key -> ADB keycode.
struct HostKey {
    Key key;
    int a2code;
};
const HostKey kHostKeys[] = {
    {Key::A, 0x00}, {Key::S, 0x01}, {Key::D, 0x02}, {Key::F, 0x03}, {Key::H, 0x04},
    {Key::G, 0x05}, {Key::Z, 0x06}, {Key::X, 0x07}, {Key::C, 0x08}, {Key::V, 0x09},
    {Key::B, 0x0b}, {Key::Q, 0x0c}, {Key::W, 0x0d}, {Key::E, 0x0e}, {Key::R, 0x0f},
    {Key::Y, 0x10}, {Key::T, 0x11}, {Key::Num1, 0x12}, {Key::Num2, 0x13}, {Key::Num3, 0x14},
    {Key::Num4, 0x15}, {Key::Num6, 0x16}, {Key::Num5, 0x17}, {Key::Equals, 0x18},
    {Key::Num9, 0x19}, {Key::Num7, 0x1a}, {Key::Minus, 0x1b}, {Key::Num8, 0x1c},
    {Key::Num0, 0x1d}, {Key::Asterisk, 0x1e}, {Key::O, 0x1f}, {Key::U, 0x20},
    {Key::At, 0x21}, {Key::I, 0x22}, {Key::P, 0x23}, {Key::Enter, 0x24}, {Key::L, 0x25},
    {Key::J, 0x26}, {Key::Quote, 0x27}, {Key::K, 0x28}, {Key::Semicolon, 0x29},
    {Key::Backslash, 0x2a}, {Key::Comma, 0x2b}, {Key::Slash, 0x2c}, {Key::N, 0x2d},
    {Key::M, 0x2e}, {Key::Period, 0x2f}, {Key::Tab, 0x30}, {Key::Space, 0x31},
    {Key::Backquote, 0x32}, {Key::Backspace, 0x33}, {Key::Delete, 0x33}, {Key::F11, 0x35},
    {Key::LeftCtrl, 0x36}, {Key::RightCtrl, 0x36}, {Key::Cbm, 0x37}, {Key::LeftGui, 0x37},
    {Key::RightGui, 0x37}, {Key::LeftShift, 0x38}, {Key::RightShift, 0x38},
    {Key::CapsLock, 0x39}, {Key::RightAlt, 0x3a}, {Key::Left, 0x3b}, {Key::Right, 0x3c},
    {Key::Down, 0x3d}, {Key::Up, 0x3e}, {Key::Plus, 0x45}, {Key::Home, 0x73},
    {Key::F10, 0x7f},
};

// Apple IIGS 16-colour palette (lo-res / text / border), 4 bits per gun.
constexpr uint16_t kIIgsColors[16] = {0x000, 0xd03, 0x009, 0xd2d, 0x072, 0x555, 0x22f, 0x6af,
                                      0x850, 0xf60, 0xaaa, 0xf98, 0x1d0, 0xff0, 0x4f9, 0xfff};

uint32_t rgb12(uint16_t c) {
    uint32_t r = (c >> 8) & 0xf, g = (c >> 4) & 0xf, b = c & 0xf;
    return 0xff000000u | ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
}

// Offset of text/lores row (0-23) inside a 1K page.
uint16_t text_row_offset(int row) { return uint16_t(((row & 7) << 7) + ((row >> 3) * 40)); }

// SmartPort card ROM for slot 7 (see header comment): ProDOS block-device
// signature, boot entry, ProDOS entry at $C70A and SmartPort at $C70D, all
// funnelling into "WDM $C7" which the emulator services.
void build_slot7_rom(std::array<uint8_t, 0x100>& rom) {
    rom.fill(0);
    static const uint8_t kCode[] = {
        0xa2, 0x20,        // C700 LDX #$20
        0xa0, 0x00,        // C702 LDY #$00
        0xa2, 0x03,        // C704 LDX #$03
        0xc9, 0x00,        // C706 CMP #$00   ($C707=$00: SmartPort)
        0x80, 0x0c,        // C708 BRA $C716
        0x18,              // C70A CLC        (ProDOS entry)
        0xb8,              // C70B CLV
        0x70, 0x38,        // C70C BVS (never); $C70D=$38 SEC (SmartPort entry)
        0xb8,              // C70E CLV
        0x42, 0xc7,        // C70F WDM $C7
        0x60,              // C711 RTS
        0xea, 0xea, 0xea, 0xea,  // C712
        0xe2, 0x41,        // C716 SEP #$41   (boot: V=1)
        0x70, 0xf5,        // C718 BVS $C70F
    };
    std::memcpy(rom.data(), kCode, sizeof kCode);
    rom[0xfb] = 0x80;  // SmartPort ID: extended calls supported
    rom[0xfc] = 0x00;  // block count: use STATUS
    rom[0xfd] = 0x00;
    rom[0xfe] = 0xbf;  // status byte
    rom[0xff] = 0x0a;  // ProDOS entry offset
}

// DOS 3.3 sector order -> ProDOS block order for 140K images.
std::vector<uint8_t> dos_to_prodos(const std::vector<uint8_t>& in) {
    static const int kDos[16] = {0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15};
    static const int kPro[16] = {0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15};
    std::vector<uint8_t> out(in.size());
    for (int track = 0; track < 35; track++) {
        for (int phys = 0; phys < 16; phys++) {
            int dos_logical = -1, pro_logical = -1;
            for (int i = 0; i < 16; i++) {
                if (kDos[i] == phys) dos_logical = i;
                if (kPro[i] == phys) pro_logical = i;
            }
            std::memcpy(&out[size_t(track * 16 + pro_logical) * 256], &in[size_t(track * 16 + dos_logical) * 256], 256);
        }
    }
    return out;
}

}  // namespace

Apple2GS::Apple2GS() : cpu_(kFastClock), doc_(kMasterClock / 4) {
    fast_ram_.assign(size_t(kRamBanks) << 16, 0);
    rd_page_.assign(0x10000, nullptr);
    wr_page_.assign(0x10000, nullptr);
    wr_flags_.assign(0x10000, 0);
    for (int i = 0; i < 16; i++) palette16_[i] = rgb12(kIIgsColors[i]);
}

Apple2GS::~Apple2GS() = default;

bool Apple2GS::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // rom_ covers banks $FC-$FF. 341-0728 fills $FC-$FD; 341-0748 fills
    // $FE-$FF with its halves swapped (first half is bank $FF).
    std::vector<uint8_t> first(0x20000, 0);
    if (!loader.load(kRomFC, first, error)) return false;
    std::copy(first.begin(), first.end(), rom_.begin());

    std::vector<uint8_t> second;
    if (!loader.try_read("341-0748", second) || second.size() != 0x20000) {
        if (error) *error = "missing or wrong-size ROM file: 341-0748";
        return false;
    }
    if (crc32_of(second.data(), second.size()) != 0x18190283u) {
        warnings_.push_back("CRC mismatch for 341-0748");
    }
    std::copy(second.begin() + 0x10000, second.end(), rom_.begin() + 0x20000);
    std::copy(second.begin(), second.begin() + 0x10000, rom_.begin() + 0x30000);

    std::vector<uint8_t> chr(0x1000, 0);
    if (!loader.load(kChrRom, chr, error)) return false;
    std::copy(chr.begin(), chr.end(), chr_rom_.begin());
    if (error) error->clear();

    build_slot7_rom(slot7_rom_);

    cpu_.set_memory_handlers([this](uint32_t a) { return mem_read(a); },
                             [this](uint32_t a, uint8_t v) { mem_write(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cpu_cycles(c); });
    cpu_.set_wdm_handler([this](uint8_t sig) { wdm(sig); });
    cpu_.set_vector_handler([this](uint32_t v) { return vector_address(v); });
    doc_.set_irq_handler([this](bool on) { if (on) add_irq(kIrqDoc); else remove_irq(kIrqDoc); });

    // Static part of the page tables: fast RAM, unmapped space, ROM, slow RAM.
    for (uint32_t page = 0; page < 0x10000; page++) {
        uint32_t bank = page >> 8;
        uint8_t* p = nullptr;
        uint8_t flags = 0;
        if (bank < uint32_t(kRamBanks)) {
            p = &fast_ram_[size_t(page) << 8];
            rd_page_[page] = p;
            wr_page_[page] = p;
        } else if (bank >= 0xfc) {
            rd_page_[page] = &rom_[size_t(page - 0xfc00) << 8];
            wr_page_[page] = nullptr;
            flags = kWrIgnore;
        } else if (bank == 0xe0 || bank == 0xe1) {
            p = &slow_ram_[size_t(page - 0xe000) << 8];
            rd_page_[page] = p;
            wr_page_[page] = p;
        } else {
            rd_page_[page] = nullptr;
            wr_page_[page] = nullptr;
            flags = kWrIgnore;
        }
        wr_flags_[page] = flags;
    }

    update_clock_time();
    reset();
    return true;
}

bool Apple2GS::load_media(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open disk image";
        return false;
    }
    Disk disk;
    disk.path = path;
    disk.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    size_t size = disk.data.size();
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    auto ends_with = [&](const char* s) {
        size_t n = std::strlen(s);
        return lower.size() >= n && lower.compare(lower.size() - n, n, s) == 0;
    };

    if (size >= 64 && std::memcmp(disk.data.data(), "2IMG", 4) == 0) {
        auto rd32 = [&](size_t o) {
            return uint32_t(disk.data[o]) | (uint32_t(disk.data[o + 1]) << 8) | (uint32_t(disk.data[o + 2]) << 16) |
                   (uint32_t(disk.data[o + 3]) << 24);
        };
        uint32_t format = rd32(0x0c);
        uint32_t offset = rd32(0x18);
        uint32_t length = rd32(0x1c);
        if (format == 0 && length == 143360 && offset + length <= size) {
            std::vector<uint8_t> dos(disk.data.begin() + offset, disk.data.begin() + offset + length);
            disk.data = dos_to_prodos(dos);
            disk.offset = 0;
            disk.blocks = 280;
        } else if (format == 1 && offset + length <= size) {
            disk.offset = offset;
            disk.blocks = length / 512;
            disk.persist = true;
        } else {
            if (error) *error = "unsupported 2IMG format (only ProDOS or DOS order)";
            return false;
        }
        if (disk.data.size() > 0x13 && (rd32(0x10) & 0x80000000u)) disk.write_protect = true;
    } else if (size == 143360 && (ends_with(".dsk") || ends_with(".do"))) {
        disk.data = dos_to_prodos(disk.data);
        disk.blocks = 280;
    } else {
        if (size == 0 || (size % 512) != 0) {
            if (error) *error = "disk image size must be a multiple of 512 bytes";
            return false;
        }
        disk.blocks = size / 512;
        disk.persist = true;
    }
    if (disk.persist && !disk.write_protect) {
        std::fstream probe(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!probe) disk.write_protect = true;  // read-only file
    }
    disks_.push_back(std::move(disk));
    return true;
}

void Apple2GS::reset() {
    // Power-on: clear RAM, then behave like the RESET line.
    std::fill(fast_ram_.begin(), fast_ram_.end(), uint8_t(0));
    slow_ram_.fill(0);
    speed_ = 0x80;
    audio_.clear();
    warm_reset();
}

// The RESET line (Control-Reset): soft switches, interrupt enables and the
// I/O chips go back to their power-on state; memory is preserved.
void Apple2GS::warm_reset() {
    shadow_ = 0;
    statereg_ = kWrDefRam | kRdRom | kLcBank2 | kIntCx;
    slotromsel_ = 0;
    newvideo_ = 0x01;
    c023_ = 0;
    c041_ = 0;
    c046_ = 0;
    c02b_ = 0x08;
    text_color_ = 0xf0;
    border_ = 0;
    c034_ = 0;
    st80_ = vid80_ = altchar_ = mixed_ = hires_ = false;
    text_ = true;
    an3_ = true;
    mono_ = false;
    irq_pending_ = 0;
    clk_mode_ = ClkMode::Idle;

    doc_.reset();
    doc_ctl_ = 0;
    doc_ptr_ = 0;
    doc_saved_ = 0;
    iwm_state_ = 0;
    iwm_mode_ = 0;
    c031_ = 0;
    adb_reset();
    rebuild_map();

    cpu_.set_irq(IrqLine::Clear);
    cpu_.reset();
}

// ---------------------------------------------------------------------------
// Memory map
// ---------------------------------------------------------------------------

void Apple2GS::map_pages(uint32_t first_page, int count, uint8_t* rd, uint8_t* wr, uint8_t flags) {
    for (int i = 0; i < count; i++) {
        uint32_t page = first_page + uint32_t(i);
        rd_page_[page] = rd ? rd + size_t(i) * 256 : nullptr;
        wr_page_[page] = wr ? wr + size_t(i) * 256 : nullptr;
        wr_flags_[page] = flags;
    }
}

void Apple2GS::rebuild_map() {
    const bool altzp = statereg_ & kAltZp;
    const bool page2 = statereg_ & kPage2;
    const bool ramrd = statereg_ & kRamRd;
    const bool ramwrt = statereg_ & kRamWrt;
    const bool rdrom = statereg_ & kRdRom;
    const bool lcbank2 = statereg_ & kLcBank2;
    const bool wrdefram = statereg_ & kWrDefRam;
    const bool iolc_off = shadow_ & 0x40;
    const uint8_t sh = shadow_;

    // Shadow flag for a write landing in main (bank 0) or aux (bank 1) fast
    // memory at the given page.
    auto shadow_flag = [&](int page, bool aux) -> uint8_t {
        const uint8_t s = aux ? kWrShadowE1 : kWrShadowE0;
        if (page >= 0x04 && page < 0x08) return (sh & 0x01) ? 0 : s;
        if (page >= 0x08 && page < 0x0c) return (sh & 0x20) ? 0 : s;
        if (page >= 0x20 && page < 0x40) {
            if (!aux) return (sh & 0x02) ? 0 : s;
            return ((sh & 0x12) == 0 || (sh & 0x08) == 0) ? s : 0;
        }
        if (page >= 0x40 && page < 0x60) {
            if (!aux) return (sh & 0x04) ? 0 : s;
            return ((sh & 0x14) == 0 || (sh & 0x08) == 0) ? s : 0;
        }
        if (page >= 0x60 && page < 0xa0) return (aux && !(sh & 0x08)) ? s : 0;
        return 0;
    };

    for (int k = 0; k < 4; k++) {
        const bool slow = k >= 2;
        const bool is_aux_bank = (k & 1) != 0;
        const uint32_t bank = slow ? uint32_t(0xe0 + (k & 1)) : uint32_t(k);
        uint8_t* main = slow ? &slow_ram_[0] : &fast_ram_[0];
        uint8_t* aux = slow ? &slow_ram_[0x10000] : &fast_ram_[0x10000];
        uint8_t* self = is_aux_bank ? aux : main;
        const uint32_t base = bank << 8;

        for (int page = 0; page < 0xc0; page++) {
            bool rd_aux = is_aux_bank, wr_aux = is_aux_bank;
            if (!is_aux_bank) {
                if (page < 2) {
                    rd_aux = wr_aux = altzp;
                } else {
                    rd_aux = ramrd;
                    wr_aux = ramwrt;
                    if (st80_ && page >= 0x04 && page < 0x08) rd_aux = wr_aux = page2;
                    if (st80_ && hires_ && page >= 0x20 && page < 0x40) rd_aux = wr_aux = page2;
                }
            }
            uint32_t pg = base + uint32_t(page);
            rd_page_[pg] = (rd_aux ? aux : main) + size_t(page) * 256;
            wr_page_[pg] = (wr_aux ? aux : main) + size_t(page) * 256;
            wr_flags_[pg] = slow ? 0 : shadow_flag(page, wr_aux);
        }

        if (!slow && iolc_off) {
            // I/O and language card disabled: banks 00/01 are plain RAM.
            uint8_t* mem = self;
            map_pages(base + 0xc0, 0x10, mem + 0xd000, mem + 0xd000, 0);
            if (!is_aux_bank && altzp) mem = aux;
            map_pages(base + 0xd0, 0x10, mem + 0xc000, mem + 0xc000, 0);
            map_pages(base + 0xe0, 0x20, mem + 0xe000, mem + 0xe000, 0);
            continue;
        }

        // $C000-$CFFF: I/O and slot ROM space.
        map_pages(base + 0xc0, 0x10, nullptr, nullptr, 0);

        // Language card ($D000-$FFFF). LC bank 1 lives at physical $C000.
        uint8_t* mem = self;
        if (!is_aux_bank && altzp) mem = aux;
        uint8_t* rd_mem = mem;
        const bool wr_ignore = !slow && !wrdefram;
        const bool use_rom = !slow && rdrom;
        if (use_rom) rd_mem = &rom_[0x30000];
        map_pages(base + 0xe0, 0x20, rd_mem + 0xe000, wr_ignore ? nullptr : mem + 0xe000, wr_ignore ? kWrIgnore : 0);
        uint32_t rd_off = 0xd000, wr_off = 0xd000;
        if (!lcbank2) {
            if (!rdrom) rd_off = 0xc000;
            wr_off = 0xc000;
        }
        if (use_rom) rd_off = 0xd000;
        map_pages(base + 0xd0, 0x10, rd_mem + rd_off, wr_ignore ? nullptr : mem + wr_off, wr_ignore ? kWrIgnore : 0);
    }
}

uint32_t Apple2GS::vector_address(uint32_t vector) {
    // Interrupt and reset vectors come from ROM unless I/O shadowing is off.
    if (shadow_ & 0x40) return vector & 0xffff;
    return 0xff0000 | (vector & 0xffff);
}

uint8_t Apple2GS::mem_read(uint32_t address) {
    const uint8_t* p = rd_page_[address >> 8];
    if (p) return p[address & 0xff];
    const uint32_t bank = address >> 16;
    if (bank <= 1 || bank == 0xe0 || bank == 0xe1) {
        uint16_t off = uint16_t(address);
        if (off >= 0xc000 && off < 0xd000) return io_read(off);
    }
    return 0;
}

void Apple2GS::mem_write(uint32_t address, uint8_t value) {
    const uint32_t page = address >> 8;
    uint8_t* p = wr_page_[page];
    if (p) {
        p[address & 0xff] = value;
        const uint8_t f = wr_flags_[page];
        if (f & (kWrShadowE0 | kWrShadowE1)) {
            slow_ram_[((f & kWrShadowE1) ? 0x10000u : 0u) | (address & 0xffff)] = value;
        }
        return;
    }
    if (wr_flags_[page] & kWrIgnore) return;
    const uint32_t bank = address >> 16;
    if (bank <= 1 || bank == 0xe0 || bank == 0xe1) {
        uint16_t off = uint16_t(address);
        if (off >= 0xc000 && off < 0xd000) io_write(off, value);
    }
}

// ---------------------------------------------------------------------------
// Timing and interrupts
// ---------------------------------------------------------------------------

void Apple2GS::add_irq(uint32_t mask) {
    irq_pending_ |= mask;
    cpu_.set_irq(irq_pending_ ? IrqLine::Assert : IrqLine::Clear);
}

void Apple2GS::remove_irq(uint32_t mask) {
    irq_pending_ &= ~mask;
    cpu_.set_irq(irq_pending_ ? IrqLine::Assert : IrqLine::Clear);
}

void Apple2GS::on_cpu_cycles(int cycles) {
    const int tpc = (speed_ & 0x80) ? kTicksFast : kTicksSlow;
    const int ticks = cycles * tpc;
    ticks_ += uint64_t(ticks);
    sound_advance(ticks);
}

uint32_t Apple2GS::lines_since_vbl_counter() const {
    uint64_t t = ticks_ - frame_start_tick_;
    uint32_t line = uint32_t(t / kTicksPerLine);
    uint32_t col = uint32_t((t % kTicksPerLine) / kTicksSlow);
    if (line >= uint32_t(kLinesPerFrame)) line = kLinesPerFrame - 1;
    return (line << 8) | col;
}

bool Apple2GS::in_vbl() const {
    uint32_t v = lines_since_vbl_counter();
    return (v >> 8) >= 192;
}

uint8_t Apple2GS::read_vid_counter(bool vertical) const {
    uint32_t v = lines_since_vbl_counter();
    uint32_t line = v >> 8, col = v & 0xff;
    uint32_t vcount = line < 256 ? 0x100 + line : 0xfa + (line - 256);
    if (vertical) return uint8_t(vcount >> 1);
    uint32_t h = col == 0 ? 0 : 0x3f + col;
    return uint8_t(((vcount & 1) << 7) | (h & 0x7f));
}

void Apple2GS::start_of_line(int line) {
    line_ = line;
    if (line == 192) {
        if (c041_ & 0x08) {
            c046_ |= 0x08;
            add_irq(kIrqVbl);
        }
        if (++quarter_sec_counter_ >= 16) {
            quarter_sec_counter_ = 0;
            if (c041_ & 0x10) {
                c046_ |= 0x10;
                add_irq(kIrqQtrSec);
            }
        }
        if (++one_sec_counter_ >= 60) {
            one_sec_counter_ = 0;
            c023_ |= 0x40;
            if (c023_ & 0x04) {
                c023_ |= 0x80;
                add_irq(kIrq1Sec);
            }
            update_clock_time();
        }
        // Mouse movement interrupt.
        if (mouse_valid_ && (c027_ & kC027MouseInt)) add_irq(kIrqAdbMouse);
    }
    // Super Hi-Res scan-line interrupts (SCB bit 6), raised as the line is drawn.
    if (line < 200 && (newvideo_ & 0x80) && (slow_ram_[0x19d00 + line] & 0x40)) {
        c023_ |= 0x20;
        if (c023_ & 0x02) {
            c023_ |= 0x80;
            add_irq(kIrqScan);
        }
    }
}

void Apple2GS::run_frame() {
    for (int line = 0; line < kLinesPerFrame; line++) {
        start_of_line(line);
        const uint64_t end = frame_start_tick_ + uint64_t(line + 1) * kTicksPerLine;
        while (ticks_ < end) cpu_.step();
    }
    frame_start_tick_ += uint64_t(kLinesPerFrame) * kTicksPerLine;
    frame_count_++;
    render();
}

// ---------------------------------------------------------------------------
// I/O ($C000-$CFFF)
// ---------------------------------------------------------------------------

uint8_t Apple2GS::io_read(uint16_t address) {
    const uint8_t loc = uint8_t(address);
    auto ior = [](bool b) -> uint8_t { return b ? 0x80 : 0x00; };
    if (address >= 0xc100) {
        const int page = (address >> 8) & 0xf;
        const bool intcx = statereg_ & kIntCx;
        if (page == 3) {
            if (!(slotromsel_ & 0x08) && !(statereg_ & kIntC8)) {
                statereg_ |= kIntC8;
            }
            if (!(slotromsel_ & 0x08) || intcx) return rom_[0x30000 + address];
            return 0;
        }
        if (page < 8) {
            if (!(slotromsel_ & (1 << page)) || intcx) return rom_[0x30000 + address];
            if (page == 7) return slot7_rom_[loc];
            return 0;
        }
        uint8_t v = 0;
        if (intcx || (statereg_ & kIntC8)) v = rom_[0x30000 + address];
        if (address == 0xcfff) statereg_ &= ~kIntC8;
        return v;
    }

    if (loc < 0x10) return adb_read_c000();
    switch (loc) {
        case 0x10: return adb_access_c010();
        case 0x11: return ior(statereg_ & kLcBank2);
        case 0x12: return ior(!(statereg_ & kRdRom));
        case 0x13: return ior(statereg_ & kRamRd);
        case 0x14: return ior(statereg_ & kRamWrt);
        case 0x15: return ior(statereg_ & kIntCx);
        case 0x16: return ior(statereg_ & kAltZp);
        case 0x17: return ior(slotromsel_ & 0x08);
        case 0x18: return ior(st80_);
        case 0x19: return ior(in_vbl());
        case 0x1a: return ior(text_);
        case 0x1b: return ior(mixed_);
        case 0x1c: return ior(statereg_ & kPage2);
        case 0x1d: return ior(hires_);
        case 0x1e: return ior(altchar_);
        case 0x1f: return ior(vid80_);
        case 0x20: return 0;
        case 0x21: return ior(mono_);
        case 0x22: return text_color_;
        case 0x23: return c023_;
        case 0x24: return mouse_read_c024();
        case 0x25: return c025_;
        case 0x26: return adb_read_c026();
        case 0x27: return adb_read_c027();
        case 0x28: return 0;
        case 0x29: return newvideo_;
        case 0x2a: return 0;
        case 0x2b: return c02b_;
        case 0x2c: return 0;
        case 0x2d: return slotromsel_;
        case 0x2e: return read_vid_counter(true);
        case 0x2f: return read_vid_counter(false);
        case 0x30: speaker_ = !speaker_; return 0;
        case 0x31: return c031_;
        case 0x32: return 0;
        case 0x33: return c033_;
        case 0x34: return c034_;
        case 0x35: return shadow_;
        case 0x36: return speed_;
        case 0x37: return 0;
        case 0x38: case 0x39: {  // SCC command registers (B, A)
            const int ch = loc == 0x38 ? 1 : 0;
            const int reg = scc_ptr_[ch];
            scc_ptr_[ch] = 0;
            switch (reg) {
                case 0: return 0x2c;   // Tx buffer empty, DCD, CTS
                case 1: return 0x01;   // all sent
                case 2: return ch == 1 ? 0x06 : scc_wr2_;  // vector
                case 3: return 0x00;   // no interrupts pending (channel A only)
                default: return 0x00;
            }
        }
        case 0x3a: case 0x3b: return 0;
        case 0x3c: return doc_ctl_;
        case 0x3d: {
            uint8_t ret = doc_saved_;
            if (doc_ctl_ & 0x40) doc_saved_ = doc_.ram_read(doc_ptr_);
            else doc_saved_ = doc_.read(uint8_t(doc_ptr_));
            if (doc_ctl_ & 0x20) doc_ptr_ = uint16_t(doc_ptr_ + 1);
            return ret;
        }
        case 0x3e: return uint8_t(doc_ptr_);
        case 0x3f: return uint8_t(doc_ptr_ >> 8);
        case 0x41: return c041_;
        case 0x46: {
            uint8_t tmp = c046_;
            c046_ = uint8_t((tmp & 0xbf) | ((tmp & 0x80) >> 1));
            return tmp;
        }
        case 0x47:
            remove_irq(kIrqVbl | kIrqQtrSec);
            c046_ &= 0xe7;
            return 0;
        case 0x50: text_ = false; return 0;
        case 0x51: text_ = true; return 0;
        case 0x52: mixed_ = false; return 0;
        case 0x53: mixed_ = true; return 0;
        case 0x54: statereg_ &= ~kPage2; rebuild_map(); return 0;
        case 0x55: statereg_ |= kPage2; rebuild_map(); return 0;
        case 0x56: hires_ = false; rebuild_map(); return 0;
        case 0x57: hires_ = true; rebuild_map(); return 0;
        case 0x5e: an3_ = false; return 0;
        case 0x5f: an3_ = true; return 0;
        case 0x60: return 0;
        case 0x61: return ior(c025_ & 0x80);  // Open Apple (Command)
        case 0x62: return ior(c025_ & 0x40);  // Solid Apple (Option)
        case 0x63: return 0;
        case 0x64: case 0x65: case 0x66: case 0x67:
            return 0;  // no joystick: paddle timers already expired
        case 0x68: return uint8_t(statereg_ & 0xff);
        case 0x70: return 0;
        default: break;
    }
    if (loc >= 0x71 && loc <= 0x7f) return rom_[0x3c000 + loc];
    if (loc >= 0x80 && loc <= 0x8f) {
        uint32_t lcbank2 = (loc & 0x08) ? 0 : kLcBank2;
        uint32_t rdrom = (((loc >> 1) ^ loc) & 1) ? kRdRom : 0;
        uint32_t prewrite = (loc & 1) ? kPrewrite : 0;
        uint32_t wrdefram = statereg_ & kWrDefRam;
        if ((loc & 1) == 0) {
            wrdefram = 0;
        } else if (statereg_ & kPrewrite) {
            wrdefram = kWrDefRam;
        }
        statereg_ = (statereg_ & ~(kPrewrite | kWrDefRam | kRdRom | kLcBank2)) | lcbank2 | rdrom | prewrite | wrdefram;
        rebuild_map();
        return 0;
    }
    if (loc >= 0xe0 && loc <= 0xef) return iwm_access(loc & 0xf, false, 0);
    return 0;
}

void Apple2GS::io_write(uint16_t address, uint8_t value) {
    const uint8_t loc = uint8_t(address);
    if (address >= 0xc100) {
        const int page = (address >> 8) & 0xf;
        if (page == 3 && !(slotromsel_ & 0x08)) statereg_ |= kIntC8;
        if (address == 0xcfff) statereg_ &= ~kIntC8;
        return;
    }
    switch (loc) {
        case 0x00: st80_ = false; rebuild_map(); return;
        case 0x01: st80_ = true; rebuild_map(); return;
        case 0x02: statereg_ &= ~kRamRd; rebuild_map(); return;
        case 0x03: statereg_ |= kRamRd; rebuild_map(); return;
        case 0x04: statereg_ &= ~kRamWrt; rebuild_map(); return;
        case 0x05: statereg_ |= kRamWrt; rebuild_map(); return;
        case 0x06: statereg_ &= ~kIntCx; return;
        case 0x07: statereg_ |= kIntCx; return;
        case 0x08: statereg_ &= ~kAltZp; rebuild_map(); return;
        case 0x09: statereg_ |= kAltZp; rebuild_map(); return;
        case 0x0a: slotromsel_ &= uint8_t(~0x08); return;
        case 0x0b: slotromsel_ |= 0x08; return;
        case 0x0c: vid80_ = false; return;
        case 0x0d: vid80_ = true; return;
        case 0x0e: altchar_ = false; return;
        case 0x0f: altchar_ = true; return;
        case 0x21: mono_ = (value & 0x80) != 0; return;
        case 0x22: text_color_ = value; return;
        case 0x23: {
            uint8_t tmp = uint8_t((c023_ & 0x70) | (value & 0x0f));
            if ((tmp & 0x22) == 0x22) irq_pending_ |= kIrqScan;
            if (!(tmp & 0x02)) irq_pending_ &= ~kIrqScan;
            if ((tmp & 0x44) == 0x44) irq_pending_ |= kIrq1Sec;
            if (!(tmp & 0x04)) irq_pending_ &= ~kIrq1Sec;
            if (irq_pending_ & (kIrqScan | kIrq1Sec)) tmp |= 0x80;
            c023_ = tmp;
            add_irq(0);
            return;
        }
        case 0x26: adb_write_c026(value); return;
        case 0x27: adb_write_c027(value); return;
        case 0x29: newvideo_ = value; return;
        case 0x2b: c02b_ = value; return;
        case 0x2d: slotromsel_ = value; return;
        case 0x30: speaker_ = !speaker_; return;
        case 0x31: c031_ = uint8_t(value & 0xc0); return;
        case 0x32: {
            uint8_t tmp = uint8_t(c023_ & 0x7f);
            if (!(value & 0x40) && (tmp & 0x40)) {
                irq_pending_ &= ~kIrq1Sec;
                tmp &= 0xbf;
            }
            if (!(value & 0x20) && (tmp & 0x20)) {
                irq_pending_ &= ~kIrqScan;
                tmp &= 0xdf;
            }
            if (irq_pending_ & (kIrq1Sec | kIrqScan)) tmp |= 0x80;
            c023_ = tmp;
            add_irq(0);
            return;
        }
        case 0x33: c033_ = value; return;
        case 0x34:
            border_ = value & 0x0f;
            clock_write_c034(value);
            return;
        case 0x35:
            if (shadow_ != value) {
                shadow_ = value;
                rebuild_map();
            }
            return;
        case 0x36: speed_ = uint8_t(value & ~0x20); return;
        case 0x38: case 0x39: {
            const int ch = loc == 0x38 ? 1 : 0;
            if (scc_ptr_[ch] == 0) {
                scc_ptr_[ch] = uint8_t((value & 0x07) | ((value & 0x38) == 0x08 ? 0x08 : 0));
            } else {
                if (scc_ptr_[ch] == 2) scc_wr2_ = value;
                scc_ptr_[ch] = 0;
            }
            return;
        }
        case 0x3c: doc_ctl_ = value; return;
        case 0x3d:
            if (doc_ctl_ & 0x40) doc_.ram_write(doc_ptr_, value);
            else {
                if (trace_) std::fprintf(stderr, "DOC w %02x=%02x ctl=%02x f=%llu\n", doc_ptr_ & 0xff, value, doc_ctl_, (unsigned long long)frame_count_);
                doc_.write(uint8_t(doc_ptr_), value);
            }
            if (doc_ctl_ & 0x20) doc_ptr_ = uint16_t(doc_ptr_ + 1);
            return;
        case 0x3e: doc_ptr_ = uint16_t((doc_ptr_ & 0xff00) | value); return;
        case 0x3f: doc_ptr_ = uint16_t((doc_ptr_ & 0x00ff) | (value << 8)); return;
        case 0x41:
            c041_ = value & 0x1f;
            if (!(value & 0x08)) remove_irq(kIrqVbl);
            if (!(value & 0x10)) remove_irq(kIrqQtrSec);
            return;
        case 0x47:
            remove_irq(kIrqVbl | kIrqQtrSec);
            c046_ &= 0xe7;
            return;
        case 0x50: text_ = false; return;
        case 0x51: text_ = true; return;
        case 0x52: mixed_ = false; return;
        case 0x53: mixed_ = true; return;
        case 0x54: statereg_ &= ~kPage2; rebuild_map(); return;
        case 0x55: statereg_ |= kPage2; rebuild_map(); return;
        case 0x56: hires_ = false; rebuild_map(); return;
        case 0x57: hires_ = true; rebuild_map(); return;
        case 0x5e: an3_ = false; return;
        case 0x5f: an3_ = true; return;
        case 0x68:
            statereg_ = (statereg_ & ~0xffu) | value;
            rebuild_map();
            return;
        default: break;
    }
    if (loc >= 0x10 && loc <= 0x1f) {
        adb_access_c010();
        return;
    }
    if (loc >= 0x80 && loc <= 0x8f) {
        uint32_t lcbank2 = (loc & 0x08) ? 0 : kLcBank2;
        uint32_t rdrom = (((loc >> 1) ^ loc) & 1) ? kRdRom : 0;
        uint32_t wrdefram = (loc & 1) ? (statereg_ & kWrDefRam) : 0;
        statereg_ = (statereg_ & ~(kPrewrite | kWrDefRam | kRdRom | kLcBank2)) | lcbank2 | rdrom | wrdefram;
        rebuild_map();
        return;
    }
    if (loc >= 0xe0 && loc <= 0xef) iwm_access(loc & 0xf, true, value);
}

// Minimal IWM: switches are tracked, the 3.5" status line reports "no
// drive / no disk" and the data register never delivers nibbles, so the
// firmware and GS/OS see empty internal disk ports.
uint8_t Apple2GS::iwm_access(uint16_t offset, bool write, uint8_t value) {
    const int sw = offset >> 1;
    if (offset & 1) iwm_state_ |= uint8_t(1 << sw);
    else iwm_state_ &= uint8_t(~(1 << sw));
    const bool motor = iwm_state_ & 0x10;
    const bool q6 = iwm_state_ & 0x40;
    const bool q7 = iwm_state_ & 0x80;
    if (write) {
        if (q6 && q7 && (offset & 1) && !motor) iwm_mode_ = value & 0x1f;
        return 0;
    }
    if (offset & 1) return 0;
    if (!q7 && !q6) return motor ? 0x00 : 0xff;               // data
    if (!q7 && q6) return uint8_t(0x80 | (motor ? 0x20 : 0) | (iwm_mode_ & 0x1f));  // status
    if (q7 && !q6) return 0x80;  // handshake: ready, write underrun (idle)
    return 0;
}

// ---------------------------------------------------------------------------
// Clock chip / battery RAM ($C033/$C034)
// ---------------------------------------------------------------------------

void Apple2GS::update_clock_time() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    // Seconds since 1 Jan 1904, local time.
    int64_t days = 0;
    for (int y = 1904; y < local.tm_year + 1900; y++) days += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
    days += local.tm_yday;
    int64_t secs = days * 86400 + local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    clk_time_ = uint32_t(secs);
}

void Apple2GS::clock_write_c034(uint8_t value) {
    c034_ = value & 0x7f;
    if (value & 0x80) do_clock_data();
}

void Apple2GS::do_clock_data() {
    const bool read = (c034_ & 0x40) != 0;
    switch (clk_mode_) {
        case ClkMode::Idle: {
            clk_read_ = (c033_ >> 7) & 1;
            clk_reg1_ = (c033_ >> 2) & 3;
            int op = (c033_ >> 4) & 7;
            if (read) {
                clk_mode_ = ClkMode::Idle;
                break;
            }
            switch (op) {
                case 0: clk_mode_ = ClkMode::Time; break;
                case 3:
                    clk_mode_ = ClkMode::Internal;
                    if (clk_reg1_ & 2) {
                        clk_mode_ = ClkMode::Bram2;
                        clk_reg1_ = (c033_ & 7) << 5;
                    }
                    break;
                case 2: clk_mode_ = ClkMode::Bram1; clk_reg1_ += 0x10; break;
                case 4: case 5: case 6: case 7:
                    clk_mode_ = ClkMode::Bram1;
                    clk_reg1_ = (c033_ >> 2) & 0xf;
                    break;
                default: break;
            }
            break;
        }
        case ClkMode::Bram2:
            if (!read && (c033_ & 0x83) == 0) {
                clk_reg1_ |= (c033_ >> 2) & 0x1f;
                clk_mode_ = ClkMode::Bram1;
            } else {
                clk_mode_ = ClkMode::Idle;
            }
            break;
        case ClkMode::Bram1:
            if (read) {
                if (clk_read_) c033_ = bram_[size_t(clk_reg1_ & 0xff)];
            } else if (!clk_read_) {
                bram_[size_t(clk_reg1_ & 0xff)] = c033_;
            }
            clk_mode_ = ClkMode::Idle;
            break;
        case ClkMode::Time:
            if (read) {
                if (clk_read_) c033_ = uint8_t(clk_time_ >> (clk_reg1_ * 8));
            } else if (!clk_read_) {
                uint32_t mask = 0xffu << (8 * clk_reg1_);
                clk_time_ = (clk_time_ & ~mask) | (uint32_t(c033_) << (8 * clk_reg1_));
            }
            clk_mode_ = ClkMode::Idle;
            break;
        case ClkMode::Internal:
            clk_mode_ = ClkMode::Idle;
            break;
    }
}

// ---------------------------------------------------------------------------
// ADB GLU: high-level model of the keyboard microcontroller.
// ---------------------------------------------------------------------------

void Apple2GS::adb_reset() {
    c027_ = 0;
    key_down_ = false;
    hard_key_down_ = false;
    kbd_chars_buffered_ = 0;
    std::memset(kbd_buf_, 0, sizeof kbd_buf_);
    kbd_dev_addr_ = 2;
    mouse_dev_addr_ = 3;
    adb_data_pending_ = 0;
    adb_interrupt_byte_ = 0;
    adb_state_ = AdbState::Idle;
    mouse_coord_ = false;
    mouse_valid_ = false;
    kbd_reg0_.clear();
    kbd_reg3_ = 0x602;
    remove_irq(kIrqAdbData | kIrqAdbMouse | kIrqAdbSrq);
}

void Apple2GS::update_adb_data_irq() {
    if ((c027_ & kC027DataInt) && (adb_data_pending_ > 0 || adb_interrupt_byte_ != 0)) add_irq(kIrqAdbData);
}

void Apple2GS::adb_send_bytes(int n, const uint8_t* bytes) {
    adb_state_ = AdbState::Sending;
    adb_data_pending_ = n;
    for (int i = 0; i < n; i++) adb_data_[i] = bytes[i];
    if (c027_ & kC027DataInt) add_irq(kIrqAdbData);
}

void Apple2GS::adb_response_packet(int n, uint32_t value) {
    adb_state_ = AdbState::Idle;
    adb_data_pending_ = n;
    for (int i = 0; i < 4; i++) adb_data_[i] = uint8_t(value >> (8 * i));
    adb_interrupt_byte_ |= uint8_t(n ? 0x80 + n - 1 : 0x80);
    if (c027_ & kC027DataInt) add_irq(kIrqAdbData);
}

void Apple2GS::adb_kbd_talk_reg0() {
    int num_bytes = 0, num = 0;
    uint32_t val0 = kbd_reg0_.size() > 0 ? kbd_reg0_[0] : 0;
    uint32_t val1 = kbd_reg0_.size() > 1 ? kbd_reg0_[1] : 0xff;
    if (!kbd_reg0_.empty()) {
        num_bytes = 2;
        num = 1;
        if ((val0 & 0x7f) == 0x7f) {
            val1 = val0;
        } else if (kbd_reg0_.size() > 1) {
            num = 2;
            if ((val1 & 0x7f) == 0x7f) {
                num = 1;
                val1 = 0xff;
            }
        } else {
            val1 = 0xff;
        }
    }
    kbd_reg0_.erase(kbd_reg0_.begin(), kbd_reg0_.begin() + num);
    adb_response_packet(num_bytes, (val0 << 8) | val1);
    if (kbd_reg0_.empty()) {
        adb_interrupt_byte_ &= uint8_t(~0x08);
        remove_irq(kIrqAdbSrq);
    }
}

uint8_t Apple2GS::adb_read_c026() {
    uint8_t ret = 0;
    switch (adb_state_) {
        case AdbState::Idle:
            ret = adb_interrupt_byte_;
            adb_interrupt_byte_ = 0;
            if (irq_pending_ & kIrqAdbSrq) adb_interrupt_byte_ |= 0x08;
            if (adb_data_pending_ == 0) remove_irq(kIrqAdbData);
            else adb_state_ = AdbState::Sending;
            break;
        case AdbState::InCmd:
            ret = 0;
            break;
        case AdbState::Sending:
            ret = adb_data_[0];
            for (int i = 1; i < adb_data_pending_; i++) adb_data_[i - 1] = adb_data_[i];
            adb_data_pending_--;
            if (adb_data_pending_ <= 0) {
                adb_data_pending_ = 0;
                adb_state_ = AdbState::Idle;
                remove_irq(kIrqAdbData);
            }
            break;
    }
    return ret;
}

void Apple2GS::adb_write_c026(uint8_t val) {
    if (adb_state_ == AdbState::InCmd) {
        adb_cmd_data_[adb_cmd_so_far_++] = val;
        if (adb_cmd_so_far_ >= adb_cmd_len_) adb_do_cmd();
        return;
    }
    if (adb_state_ == AdbState::Sending) {
        // A new command aborts whatever was pending.
        adb_data_pending_ = 0;
        adb_state_ = AdbState::Idle;
    }
    adb_cmd_ = val;
    adb_cmd_so_far_ = 0;
    adb_cmd_len_ = 0;
    const int dev = val & 0xf;
    auto in_cmd = [&](int len) {
        adb_state_ = AdbState::InCmd;
        adb_cmd_len_ = len;
    };
    switch (val) {
        case 0x01: case 0x03: case 0x73: break;  // abort, flush, disable mouse SRQ
        case 0x04: case 0x05: in_cmd(1); break;  // set/clear modes
        case 0x06: in_cmd(3); break;             // set config
        case 0x07: in_cmd(8); break;             // sync (ROM 03: 8 bytes)
        case 0x08: case 0x09: in_cmd(2); break;  // write/read mem
        case 0x0a: { uint8_t b = adb_mode_; adb_send_bytes(1, &b); break; }
        case 0x0b: {
            uint8_t b[4] = {0x82, uint8_t((mouse_dev_addr_ << 4) | kbd_dev_addr_), 0x00, 0x23};
            adb_send_bytes(4, b);
            break;
        }
        case 0x0d: { uint8_t b = 6; adb_send_bytes(1, &b); break; }  // version (ROM 03)
        case 0x0e: { uint8_t b[2] = {0x08, 0x00}; adb_send_bytes(2, b); break; }
        case 0x0f: { uint8_t b[2] = {0x0a, 0x00}; adb_send_bytes(2, b); break; }
        case 0x10: warm_reset(); break;
        case 0x11: in_cmd(1); break;
        case 0x12: case 0x13: in_cmd(2); break;
        default:
            if (val >= 0xb0 && val <= 0xbf) {
                in_cmd(2);  // listen register 3
            } else if (val >= 0xc0 && val <= 0xcf) {
                if (dev == kbd_dev_addr_) adb_kbd_talk_reg0();
            } else if (val >= 0xf0) {
                if (dev == kbd_dev_addr_) adb_response_packet(2, kbd_reg3_);
            }
            break;
    }
}

void Apple2GS::adb_do_cmd() {
    adb_state_ = AdbState::Idle;
    const int dev = adb_cmd_ & 0xf;
    switch (adb_cmd_) {
        case 0x04: adb_mode_ = uint8_t(adb_mode_ | adb_cmd_data_[0]); break;
        case 0x05: adb_mode_ = uint8_t(adb_mode_ & ~adb_cmd_data_[0]); break;
        case 0x06: break;
        case 0x07: adb_mode_ = adb_cmd_data_[0]; break;
        case 0x08: adb_memory_[adb_cmd_data_[0]] = adb_cmd_data_[1]; break;
        case 0x09: {
            uint32_t addr = uint32_t(adb_cmd_data_[1] << 8) | adb_cmd_data_[0];
            uint8_t v = 0;
            if (addr < 0x100) {
                v = adb_memory_[addr];
                if (addr == 0x0c) v = c025_ & 0xc7;
            } else if (addr == 0x1400) {
                v = 0x72;  // ROM self-test checksum
            } else if (addr == 0x1401) {
                v = 0xf7;
            }
            adb_send_bytes(1, &v);
            break;
        }
        case 0x11: adb_key_update(adb_cmd_data_[0] & 0x7f, (adb_cmd_data_[0] >> 7) != 0); break;
        default:
            if (adb_cmd_ >= 0xb0 && adb_cmd_ <= 0xbf && dev == kbd_dev_addr_) {
                if (adb_cmd_data_[1] == 0xfe) kbd_dev_addr_ = adb_cmd_data_[0] & 0xf;
                kbd_reg3_ = uint16_t(((adb_cmd_data_[0] & 0xf) << 12) | (kbd_reg3_ & 0x0fff));
            }
            break;
    }
}

uint8_t Apple2GS::adb_read_c027() {
    uint8_t ret = uint8_t(c027_ & kC027NegMask);
    if (mouse_valid_) ret |= kC027MouseData;
    if (adb_interrupt_byte_ != 0) ret |= kC027DataValid;
    else if (adb_data_pending_ > 0 && adb_state_ != AdbState::InCmd) ret |= kC027DataValid;
    if (mouse_coord_) ret |= kC027MouseCoord;
    return ret;
}

void Apple2GS::adb_write_c027(uint8_t val) {
    uint8_t old = c027_;
    c027_ = uint8_t(val & kC027NegMask);
    if (!(c027_ & kC027MouseInt) && (old & kC027MouseInt)) remove_irq(kIrqAdbMouse);
    if (!(c027_ & kC027DataInt) && (old & kC027DataInt)) remove_irq(kIrqAdbData);
    if ((c027_ & kC027MouseInt) && mouse_valid_) add_irq(kIrqAdbMouse);
    update_adb_data_irq();
}

void Apple2GS::adb_key_update(int a2code, bool is_up) {
    if (a2code < 0 || a2code > 0x7f) return;
    // Ctrl-Reset.
    if (!is_up && a2code == 0x7f && (c025_ & 0x02)) {
        warm_reset();
        return;
    }
    const bool autopoll = !(adb_mode_ & 1) && kbd_dev_addr_ == 2;
    if (!autopoll) {
        kbd_reg0_.push_back(uint8_t(a2code | (is_up ? 0x80 : 0)));
        if (kbd_reg3_ & 0x200) {
            adb_interrupt_byte_ |= 0x08;
            add_irq(kIrqAdbSrq);
        }
        return;
    }
    const int i = (a2code >> 5) & 3;
    const uint32_t mask = 1u << (a2code & 0x1f);
    if (is_up) {
        if (!(virtual_key_up_[i] & mask)) {
            virtual_key_up_[i] |= mask;
            adb_key_event(a2code, true);
        }
    } else if (virtual_key_up_[i] & mask) {
        virtual_key_up_[i] &= ~mask;
        adb_key_event(a2code, false);
    }
}

void Apple2GS::adb_key_event(int a2code, bool is_up) {
    const KeyAscii& k = kKeyAscii[a2code];
    const bool hard_key = k.normal >= 0 && !(k.normal & 0xef00);
    bool shift = c025_ & 0x01, ctrl = c025_ & 0x02, caps = c025_ & 0x04;
    int ascii = k.normal;
    if (caps && ascii >= 'a' && ascii <= 'z') {
        ascii = k.shifted;
        if (shift && (adb_mode_ & 0x40)) ascii = k.normal;
    } else if (shift) {
        ascii = k.shifted;
    }
    if (ctrl && k.ctrl >= 0) ascii = k.ctrl;
    if (!is_up && a2code == 0x35 && ctrl && (c025_ & 0x80)) {
        // Control-Command-Escape: desk manager interrupt.
        adb_interrupt_byte_ |= 0x20;
        if (c027_ & kC027DataInt) add_irq(kIrqAdbData);
    }
    uint8_t special = 0;
    if (ascii >= 0) special = uint8_t((ascii >> 8) & 0xff);
    const uint8_t key = uint8_t(((ascii < 0 ? 0 : ascii) & 0x7f) | 0x80);

    if (!is_up) {
        if (hard_key) {
            kbd_buf_[kbd_chars_buffered_] = key;
            if (kbd_chars_buffered_ < 7) kbd_chars_buffered_++;
            key_down_ = true;
            a2code_down_ = a2code;
            repeat_frame_ = frame_count_ + uint64_t(repeat_delay_);
            hard_key_down_ = true;
        }
        c025_ = uint8_t(c025_ | special);
    } else {
        if (hard_key && a2code == a2code_down_) {
            hard_key_down_ = false;
            key_down_ = false;
        }
        c025_ = uint8_t(c025_ & ~special);
    }
    if (key_down_) c025_ &= uint8_t(~0x20);
    else c025_ |= 0x20;
}

uint8_t Apple2GS::adb_read_c000() {
    if (!(kbd_buf_[0] & 0x80) && !key_down_) return kbd_buf_[0];
    if (kbd_buf_[0] & 0x80) {
        if (kbd_read_no_update_++ > 5 && kbd_chars_buffered_ > 1) adb_access_c010();
    } else if (key_down_ && frame_count_ >= repeat_frame_) {
        c025_ |= 0x08;
        adb_key_event(a2code_down_, false);
        repeat_frame_ = frame_count_ + uint64_t(repeat_rate_);
    }
    return kbd_buf_[0];
}

uint8_t Apple2GS::adb_access_c010() {
    kbd_read_no_update_ = 0;
    uint8_t tmp = uint8_t(kbd_buf_[0] & 0x7f);
    kbd_buf_[0] = tmp;
    tmp = uint8_t(tmp | (hard_key_down_ ? 0x80 : 0));
    if (kbd_chars_buffered_) {
        for (int i = 1; i < kbd_chars_buffered_; i++) kbd_buf_[i - 1] = kbd_buf_[i];
        kbd_chars_buffered_--;
    }
    c025_ &= uint8_t(~0x08);
    return tmp;
}

uint8_t Apple2GS::mouse_read_c024() {
    if ((adb_mode_ & 0x02) || mouse_dev_addr_ != 3) {
        mouse_valid_ = false;
        remove_irq(kIrqAdbMouse);
        return 0;
    }
    int dx = mouse_target_x_ - mouse_a2_x_;
    int dy = mouse_target_y_ - mouse_a2_y_;
    dx = std::clamp(dx, -0x3f, 0x3f);
    dy = std::clamp(dy, -0x3f, 0x3f);
    // When the Event Manager is running, keep its cursor position in step
    // with ours so the IIGS pointer tracks the host pointer exactly.
    const uint32_t tools = uint32_t(slow_ram_[0x103c8]) | (uint32_t(slow_ram_[0x103c9]) << 8) |
                           (uint32_t(slow_ram_[0x103ca]) << 16);
    bool em_active = false;
    if (tools >= 0x20000 && tools + 28 < fast_ram_.size()) {
        em_active = (fast_ram_[tools + 24] | fast_ram_[tools + 25]) != 0;
    }
    if (em_active) {
        if (!mouse_coord_) {
            slow_ram_[0x47c] = fast_ram_[0x47c] = uint8_t(mouse_a2_x_);
            slow_ram_[0x57c] = fast_ram_[0x57c] = uint8_t(mouse_a2_x_ >> 8);
            slow_ram_[0x10190] = uint8_t(mouse_a2_x_);
            slow_ram_[0x10192] = uint8_t(mouse_a2_x_ >> 8);
        } else {
            slow_ram_[0x4fc] = fast_ram_[0x4fc] = uint8_t(mouse_a2_y_);
            slow_ram_[0x5fc] = fast_ram_[0x5fc] = uint8_t(mouse_a2_y_ >> 8);
            slow_ram_[0x10191] = uint8_t(mouse_a2_y_);
            slow_ram_[0x10193] = uint8_t(mouse_a2_y_ >> 8);
        }
    }
    uint8_t ret;
    if (mouse_coord_) {
        mouse_a2_y_ += dy;
        mouse_a2_button_ = mouse_button_;
        ret = uint8_t(((!mouse_button_) ? 0x80 : 0) | (dy & 0x7f));
    } else {
        mouse_a2_x_ += dx;
        ret = uint8_t(((!mouse_button_) ? 0x80 : 0) | (dx & 0x7f));
    }
    if (mouse_coord_ && mouse_a2_x_ == mouse_target_x_ && mouse_a2_y_ == mouse_target_y_ &&
        mouse_a2_button_ == mouse_button_) {
        mouse_valid_ = false;
        remove_irq(kIrqAdbMouse);
    }
    mouse_coord_ = !mouse_coord_;
    return ret;
}

void Apple2GS::set_inputs(const MachineInputs& inputs) {
    for (const HostKey& hk : kHostKeys) {
        bool now = inputs.key(hk.key);
        bool before = prev_keys_[size_t(hk.key)];
        if (now != before) adb_key_update(hk.a2code, !now);
    }
    prev_keys_ = inputs.keys;

    if (inputs.has_pointer) {
        // The host pointer is absolute; the ADB mouse reports deltas towards
        // it (in 640x200 or 320x200 Super Hi-Res units).
        const int x = std::clamp(inputs.pointer_x, 0, kScreenWidth - 1);
        const int y = std::clamp(inputs.pointer_y, 0, kScreenHeight - 1);
        const bool shr320 = (newvideo_ & 0x80) && !(slow_ram_[0x19d00] & 0x80);
        mouse_target_x_ = shr320 ? x / 2 : x;
        mouse_target_y_ = y / 2;
        const bool button = inputs.pointer_button1;
        const bool moved = mouse_target_x_ != mouse_a2_x_ || mouse_target_y_ != mouse_a2_y_;
        mouse_button_ = button;
        if ((moved || button != mouse_a2_button_) && !(adb_mode_ & 0x02)) {
            mouse_valid_ = true;
            if (c027_ & kC027MouseInt) add_irq(kIrqAdbMouse);
        }
    }
}

void Apple2GS::set_dip_switch(int, uint8_t) {}

// ---------------------------------------------------------------------------
// SmartPort (slot 7, high level)
// ---------------------------------------------------------------------------

int Apple2GS::disk_read(int unit, uint32_t buf, uint32_t block) {
    if (unit < 0 || unit >= int(disks_.size())) return 0x28;  // no device
    const Disk& d = disks_[size_t(unit)];
    if (block >= d.blocks) return 0x2d;  // bad block
    const uint8_t* src = &d.data[d.offset + size_t(block) * 512];
    for (uint32_t i = 0; i < 512; i++) mem_write((buf + i) & 0xffffff, src[i]);
    return 0;
}

int Apple2GS::disk_write(int unit, uint32_t buf, uint32_t block) {
    if (unit < 0 || unit >= int(disks_.size())) return 0x28;
    Disk& d = disks_[size_t(unit)];
    if (d.write_protect) return 0x2b;
    if (block >= d.blocks) return 0x2d;
    uint8_t* dst = &d.data[d.offset + size_t(block) * 512];
    for (uint32_t i = 0; i < 512; i++) dst[i] = mem_read((buf + i) & 0xffffff);
    if (d.persist) {
        // Write through to the image file so changes survive the session.
        std::fstream f(d.path, std::ios::in | std::ios::out | std::ios::binary);
        if (f) {
            f.seekp(std::streamoff(d.offset + size_t(block) * 512));
            f.write(reinterpret_cast<const char*>(dst), 512);
        }
    }
    return 0;
}

void Apple2GS::wdm(uint8_t signature) {
    if (signature != 0xc7) return;
    if (cpu_.p.v) smartport_boot();
    else if (cpu_.p.c) smartport_call();
    else smartport_prodos();
}

void Apple2GS::smartport_boot() {
    int ret = disk_read(0, 0x800, 0);
    mem_write(0x7f8, 0xc7);
    mem_write(0x42, 0x01);  // ProDOS driver parameters: READ, unit $70
    mem_write(0x43, 0x70);
    mem_write(0x44, 0x00);
    mem_write(0x45, 0x08);
    mem_write(0x46, 0x00);
    mem_write(0x47, 0x00);
    cpu_.x = 0x70;
    if (ret == 0 && mem_read(0x801) != 0) {
        cpu_.set_pc(0x000801);
    } else {
        // Nothing bootable here: continue the ROM's slot scan with slot 6.
        cpu_.set_pc(0x00c600);
    }
}

void Apple2GS::smartport_prodos() {
    mem_write(0x7f8, 0xc7);
    const uint16_t dp = cpu_.d;
    auto dpb = [&](int o) { return mem_read(uint16_t(dp + o)); };
    uint8_t cmd = dpb(0x42);
    uint8_t unit_num = dpb(0x43);
    uint32_t buf = uint32_t(dpb(0x44) | (dpb(0x45) << 8));
    uint32_t blk = uint32_t(dpb(0x46) | (dpb(0x47) << 8));
    int unit = unit_num >> 7;
    if ((unit_num & 0x70) != 0x70) unit += 2;
    int ret = 0x27;
    switch (cmd) {
        case 0x00:
            if (unit < int(disks_.size())) {
                size_t blocks = disks_[size_t(unit)].blocks;
                cpu_.x = uint16_t(blocks & 0xff);
                cpu_.y = uint16_t((blocks >> 8) & 0xff);
                ret = 0;
            } else {
                ret = 0x28;
            }
            break;
        case 0x01: ret = disk_read(unit, buf, blk); break;
        case 0x02: ret = disk_write(unit, buf, blk); break;
        case 0x03: ret = unit < int(disks_.size()) ? 0 : 0x28; break;
        default: ret = 0x01; break;
    }
    cpu_.a = uint16_t((cpu_.a & 0xff00) | uint8_t(ret));
    cpu_.p.c = ret != 0;
    cpu_.p.z = ret == 0;
}

void Apple2GS::smartport_call() {
    mem_write(0x7f8, 0xc7);
    // Pull the JSR return address: it points at the inline command block.
    uint8_t lo, hi;
    if (cpu_.emulation()) {
        cpu_.sp = uint16_t(0x100 | uint8_t(cpu_.sp + 1));
        lo = mem_read(cpu_.sp);
        cpu_.sp = uint16_t(0x100 | uint8_t(cpu_.sp + 1));
        hi = mem_read(cpu_.sp);
    } else {
        cpu_.sp = uint16_t(cpu_.sp + 1);
        lo = mem_read(cpu_.sp);
        cpu_.sp = uint16_t(cpu_.sp + 1);
        hi = mem_read(cpu_.sp);
    }
    const uint32_t bank = uint32_t(cpu_.pbr) << 16;
    const uint16_t rts = uint16_t((lo | (hi << 8)) + 1);
    auto rb = [&](uint32_t a) { return mem_read(a & 0xffffff); };
    const uint8_t cmd = rb(bank | rts);
    const bool ext = (cmd & 0x40) != 0;
    uint32_t list = uint32_t(rb(bank | uint16_t(rts + 1)) | (rb(bank | uint16_t(rts + 2)) << 8));
    if (ext) list |= uint32_t(rb(bank | uint16_t(rts + 3))) << 16;
    else list |= bank;
    const uint32_t mask = ext ? 0xffffff : 0xffffffu;
    auto lb = [&](uint32_t o) { return rb((list + o) & mask); };

    const uint8_t param_cnt = lb(0);
    const uint8_t unit = lb(1);
    int ret = 0;
    const int nunits = int(disks_.size());
    switch (cmd & 0x3f) {
        case 0x00: {  // STATUS
            if (param_cnt != 3) { ret = 0x04; break; }
            uint32_t sp = uint32_t(lb(2) | (lb(3) << 8));
            uint8_t code = ext ? lb(6) : lb(4);
            if (ext) sp |= uint32_t(lb(4)) << 16;
            else sp |= bank;
            auto wb = [&](uint32_t o, uint8_t v) { mem_write((sp + o) & 0xffffff, v); };
            if (unit == 0 && code == 0) {
                wb(0, uint8_t(nunits));
                wb(1, 0xff);  // no interrupts
                wb(2, 0x4b); wb(3, 0x00);  // vendor
                wb(4, 0x00); wb(5, 0x10);  // version
                wb(6, 0x00); wb(7, 0x00);
                cpu_.x = 8;
                cpu_.y = 0;
            } else if (unit > 0 && (code == 0 || code == 3)) {
                uint8_t stat = 0x80;
                uint32_t blocks = 0;
                if (unit <= nunits) {
                    stat = 0xf8;
                    blocks = uint32_t(disks_[size_t(unit - 1)].blocks);
                    if (disks_[size_t(unit - 1)].write_protect) stat |= 0x04;
                }
                wb(0, stat);
                wb(1, uint8_t(blocks));
                wb(2, uint8_t(blocks >> 8));
                wb(3, uint8_t(blocks >> 16));
                uint32_t o = 4;
                if (ext) wb(o++, uint8_t(blocks >> 24));
                if (code == 0) {
                    cpu_.x = uint16_t(o);
                } else {
                    const char name[] = "DSP-CPP DISK    ";
                    wb(o, 12);
                    for (int i = 0; i < 16; i++) wb(o + 1 + uint32_t(i), uint8_t(name[i]));
                    wb(o + 17, 0x02);  // type: hard disk
                    wb(o + 18, 0xc0);  // subtype: extended + removable
                    wb(o + 19, 0x00);
                    wb(o + 20, 0x00);  // firmware version
                    cpu_.x = uint16_t(o + 21);
                    if (unit > nunits) ret = 0x28;
                }
                cpu_.y = 0;
            } else {
                ret = 0x21;  // bad control/status code
            }
            break;
        }
        case 0x01:    // READ BLOCK
        case 0x02: {  // WRITE BLOCK
            if (param_cnt != 3) { ret = 0x04; break; }
            uint32_t buf = uint32_t(lb(2) | (lb(3) << 8));
            uint32_t o = 4;
            if (ext) {
                buf |= uint32_t(lb(4)) << 16;
                o = 6;
            } else {
                buf |= bank;
            }
            uint32_t block = uint32_t(lb(o) | (lb(o + 1) << 8) | (lb(o + 2) << 16));
            if (ext) block |= uint32_t(lb(o + 3)) << 24;
            if (unit < 1 || unit > nunits) { ret = 0x28; break; }
            ret = (cmd & 0x3f) == 1 ? disk_read(unit - 1, buf, block) : disk_write(unit - 1, buf, block);
            cpu_.x = 0;
            cpu_.y = 2;
            break;
        }
        case 0x03:  // FORMAT
            ret = (unit >= 1 && unit <= nunits) ? 0 : 0x28;
            break;
        case 0x04:  // CONTROL
            ret = 0;
            break;
        case 0x05:  // INIT
            ret = 0;
            break;
        default:
            ret = 0x01;  // bad command
            break;
    }
    cpu_.a = uint16_t((cpu_.a & 0xff00) | uint8_t(ret));
    cpu_.p.c = ret != 0;
    cpu_.p.z = ret == 0;
    cpu_.set_pc(bank | uint16_t(rts + 3 + (ext ? 2 : 0)));
}

// ---------------------------------------------------------------------------
// Sound: DOC at 894.886 kHz / (oscillators + 2), plus the 1-bit speaker.
// ---------------------------------------------------------------------------

void Apple2GS::sound_advance(int ticks) {
    // One DOC output sample takes exactly 16 * (enabled + 2) master ticks.
    const double doc_ticks = (double(kMasterClock) / 2.0) / doc_.output_rate();
    doc_phase_ += ticks;
    while (doc_phase_ >= doc_ticks) {
        doc_phase_ -= doc_ticks;
        doc_last_ = doc_.generate();
    }
    constexpr double kTicksPerOut = 14318181.0 / 44100.0;
    out_phase_ += ticks;
    while (out_phase_ >= kTicksPerOut) {
        out_phase_ -= kTicksPerOut;
        // DOC mix scaled by the sound GLU volume, plus the 1-bit speaker,
        // through a DC-blocking high-pass filter.
        const int32_t vol = doc_ctl_ & 0x0f;
        double in = double((doc_last_ * vol) >> 6) + (speaker_ ? 4000.0 : -4000.0);
        double out = in - hp_prev_in_ + 0.995 * hp_prev_out_;
        hp_prev_in_ = in;
        hp_prev_out_ = out;
        int32_t s = int32_t(out);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (audio_.size() < 44100) audio_.push_back(int16_t(s));
    }
}

void Apple2GS::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

void Apple2GS::fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int yy = std::max(0, y); yy < std::min(kScreenHeight, y + h); yy++) {
        uint32_t* row = &framebuffer_[size_t(yy) * kScreenWidth];
        for (int xx = std::max(0, x); xx < std::min(kScreenWidth, x + w); xx++) row[xx] = color;
    }
}

// Apple II modes use a 560x384 area centred in the 640x400 frame.
namespace {
constexpr int kA2X = 40;
constexpr int kA2Y = 8;
}  // namespace

void Apple2GS::render_text_row(int row, bool col80, uint32_t fg, uint32_t bg, int /*ybase*/) {
    const bool page2 = (statereg_ & kPage2) && !st80_;
    const uint32_t base = (page2 ? 0x800u : 0x400u) + text_row_offset(row);
    const bool flash = flash_on();
    const int cols = col80 ? 80 : 40;
    for (int c = 0; c < cols; c++) {
        uint8_t ch;
        if (col80) {
            ch = (c & 1) ? slow_ram_[base + uint32_t(c >> 1)] : slow_ram_[0x10000 + base + uint32_t(c >> 1)];
        } else {
            ch = slow_ram_[base + uint32_t(c)];
        }
        // The video ROM is active-low (a 0 bit lights the dot), indexed by
        // the screen code; the second 2K is the alternate (MouseText) set.
        const bool invert_flash = !altchar_ && ch >= 0x40 && ch < 0x80 && flash;
        const uint32_t glyph_base = (altchar_ ? 0x800u : 0u) + uint32_t(ch) * 8;
        const int px_w = col80 ? 1 : 2;
        for (int y = 0; y < 8; y++) {
            uint8_t bits = uint8_t(~chr_rom_[glyph_base + uint32_t(y)]);
            if (invert_flash) bits = uint8_t(~bits);
            uint32_t* out0 = &framebuffer_[size_t(kA2Y + (row * 8 + y) * 2) * kScreenWidth];
            uint32_t* out1 = out0 + kScreenWidth;
            for (int x = 0; x < 7; x++) {
                uint32_t color = (bits & (1 << x)) ? fg : bg;
                int px = kA2X + (c * 7 + x) * px_w;
                for (int k = 0; k < px_w; k++) {
                    out0[px + k] = color;
                    out1[px + k] = color;
                }
            }
        }
    }
}

void Apple2GS::render_lores(int first_row, int last_row) {
    const bool page2 = (statereg_ & kPage2) && !st80_;
    const bool dbl = vid80_ && !an3_;
    for (int row = first_row; row < last_row; row++) {
        const uint32_t base = (page2 ? 0x800u : 0x400u) + text_row_offset(row);
        for (int c = 0; c < 40; c++) {
            for (int half = 0; half < 2; half++) {
                const int y = kA2Y + (row * 8 + half * 4) * 2;
                if (dbl) {
                    uint8_t a = slow_ram_[0x10000 + base + uint32_t(c)];
                    uint8_t m = slow_ram_[base + uint32_t(c)];
                    uint8_t ca = uint8_t(half ? (a >> 4) : (a & 0xf));
                    ca = uint8_t(((ca << 1) | (ca >> 3)) & 0xf);  // aux colours are rotated
                    uint8_t cm = uint8_t(half ? (m >> 4) : (m & 0xf));
                    fill_rect(kA2X + c * 14, y, 7, 8, palette16_[ca]);
                    fill_rect(kA2X + c * 14 + 7, y, 7, 8, palette16_[cm]);
                } else {
                    uint8_t m = slow_ram_[base + uint32_t(c)];
                    uint8_t cm = uint8_t(half ? (m >> 4) : (m & 0xf));
                    fill_rect(kA2X + c * 14, y, 14, 8, palette16_[cm]);
                }
            }
        }
    }
}

void Apple2GS::render_hires(int first_row, int last_row) {
    const bool page2 = (statereg_ & kPage2) && !st80_;
    const uint32_t page = page2 ? 0x4000u : 0x2000u;
    const uint32_t white = palette16_[15], black = palette16_[0];
    for (int line = first_row * 8; line < last_row * 8; line++) {
        const uint32_t addr = page + uint32_t((line & 7) << 10) + uint32_t(((line >> 3) & 7) << 7) +
                              uint32_t((line >> 6) * 40);
        bool bits[282] = {};
        bool shift[280] = {};
        for (int c = 0; c < 40; c++) {
            uint8_t b = slow_ram_[addr + uint32_t(c)];
            for (int i = 0; i < 7; i++) {
                bits[1 + c * 7 + i] = (b >> i) & 1;
                shift[c * 7 + i] = (b & 0x80) != 0;
            }
        }
        uint32_t* out0 = &framebuffer_[size_t(kA2Y + line * 2) * kScreenWidth + kA2X];
        uint32_t* out1 = out0 + kScreenWidth;
        for (int x = 0; x < 280; x++) {
            uint32_t color;
            const bool on = bits[x + 1], left = bits[x], right = bits[x + 2];
            if (mono_ || (newvideo_ & 0x20)) {
                color = on ? white : black;
            } else if (on && (left || right)) {
                color = white;
            } else if (on || (left && right)) {
                // Isolated dot (or gap between two same-phase dots): colour.
                const bool odd = (x & 1) != 0;
                const bool pal = shift[x];
                int idx = pal ? (odd ? 9 : 6) : (odd ? 12 : 3);
                if (!on) idx = pal ? (odd ? 6 : 9) : (odd ? 3 : 12);
                color = palette16_[idx];
                if (!on && !(left && right)) color = black;
            } else {
                color = black;
            }
            out0[x * 2] = out0[x * 2 + 1] = color;
            out1[x * 2] = out1[x * 2 + 1] = color;
        }
    }
}

void Apple2GS::render_dhires(int first_row, int last_row) {
    const bool page2 = (statereg_ & kPage2) && !st80_;
    const uint32_t page = page2 ? 0x4000u : 0x2000u;
    const bool mono = mono_ || (newvideo_ & 0x20);
    for (int line = first_row * 8; line < last_row * 8; line++) {
        const uint32_t addr = page + uint32_t((line & 7) << 10) + uint32_t(((line >> 3) & 7) << 7) +
                              uint32_t((line >> 6) * 40);
        uint8_t dots[560];
        for (int c = 0; c < 40; c++) {
            uint8_t a = slow_ram_[0x10000 + addr + uint32_t(c)];
            uint8_t m = slow_ram_[addr + uint32_t(c)];
            for (int i = 0; i < 7; i++) {
                dots[c * 14 + i] = (a >> i) & 1;
                dots[c * 14 + 7 + i] = (m >> i) & 1;
            }
        }
        uint32_t* out0 = &framebuffer_[size_t(kA2Y + line * 2) * kScreenWidth + kA2X];
        uint32_t* out1 = out0 + kScreenWidth;
        for (int x = 0; x < 560; x++) {
            uint32_t color;
            if (mono) {
                color = dots[x] ? palette16_[15] : palette16_[0];
            } else {
                const int g = x & ~3;
                int v = dots[g] | (dots[g + 1] << 1) | (dots[g + 2] << 2) | (dots[g + 3] << 3);
                // Stream order b0..b3 -> lo-res colour number.
                int idx = ((v & 1) << 3) | ((v & 2) << 1) | ((v & 4) >> 1) | ((v & 8) >> 3);
                idx = ((idx >> 1) | (idx << 3)) & 0xf;
                color = palette16_[idx];
            }
            out0[x] = color;
            out1[x] = color;
        }
    }
}

void Apple2GS::render_shr() {
    const uint8_t* e1 = &slow_ram_[0x10000];
    for (int line = 0; line < 200; line++) {
        const uint8_t scb = e1[0x9d00 + line];
        const uint8_t* pal = &e1[0x9e00 + (scb & 0x0f) * 32];
        uint32_t colors[16];
        for (int i = 0; i < 16; i++) colors[i] = rgb12(uint16_t(pal[i * 2] | ((pal[i * 2 + 1] & 0x0f) << 8)));
        const uint8_t* px = &e1[0x2000 + line * 160];
        uint32_t* out0 = &framebuffer_[size_t(line * 2) * kScreenWidth];
        uint32_t* out1 = out0 + kScreenWidth;
        if (scb & 0x80) {
            for (int b = 0; b < 160; b++) {
                const uint8_t v = px[b];
                out0[b * 4 + 0] = colors[8 + ((v >> 6) & 3)];
                out0[b * 4 + 1] = colors[12 + ((v >> 4) & 3)];
                out0[b * 4 + 2] = colors[0 + ((v >> 2) & 3)];
                out0[b * 4 + 3] = colors[4 + (v & 3)];
            }
        } else {
            const bool fill = (scb & 0x20) != 0;
            uint32_t last = colors[0];
            for (int b = 0; b < 160; b++) {
                const uint8_t v = px[b];
                for (int n = 0; n < 2; n++) {
                    const int idx = n == 0 ? (v >> 4) : (v & 0xf);
                    uint32_t c = (fill && idx == 0) ? last : colors[idx];
                    last = c;
                    out0[b * 4 + n * 2] = out0[b * 4 + n * 2 + 1] = c;
                }
            }
        }
        std::memcpy(out1, out0, sizeof(uint32_t) * kScreenWidth);
    }
}

void Apple2GS::render() {
    if (newvideo_ & 0x80) {
        render_shr();
        return;
    }
    const uint32_t border = palette16_[border_ & 0xf];
    fill_rect(0, 0, kScreenWidth, kA2Y, border);
    fill_rect(0, kScreenHeight - kA2Y, kScreenWidth, kA2Y, border);
    fill_rect(0, kA2Y, kA2X, 384, border);
    fill_rect(kScreenWidth - kA2X, kA2Y, kA2X, 384, border);

    const uint32_t fg = palette16_[(text_color_ >> 4) & 0xf];
    const uint32_t bg = palette16_[text_color_ & 0xf];
    int text_from = 0;
    if (!text_) {
        const int gr_rows = mixed_ ? 20 : 24;
        if (hires_) {
            if (vid80_ && !an3_) render_dhires(0, gr_rows);
            else render_hires(0, gr_rows);
        } else {
            render_lores(0, gr_rows);
        }
        text_from = gr_rows;
    }
    for (int row = text_from; row < 24; row++) render_text_row(row, vid80_, fg, bg, 0);
}

}  // namespace dsp
