#include "drivers/amstrad_cpc.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kCpc464Rom = {{"cpc464.rom", 0x8000, 0x0000, 0x40852f25}};
const std::vector<RomEntry> kCpc664Rom = {{"cpc664.rom", 0x8000, 0x0000, 0x9ab5a036}};
const std::vector<RomEntry> kCpc6128Rom = {{"cpc6128.rom", 0x8000, 0x0000, 0x9e827fe1}};
const std::vector<RomEntry> kAmsdosRom = {{"amsdos.rom", 0x4000, 0x0000, 0x1fe22ecd}};

// CRTC register write masks, amstrad_cpc.pas write_crtc(). Registers 16/17
// (light pen, read only on this board) are never written through this path.
constexpr uint8_t kCrtcMasks[18] = {0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
                                    3,    0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
                                    0xff, 0xff};

// Hardware colour palette (color monitor), amstrad_cpc_paleta().
constexpr uint32_t kCpcPalette[32] = {
    0x808080, 0x808080, 0x00FF80, 0xFFFF80, 0x000080, 0xFF0080, 0x008080, 0xFF8080,
    0xFF0080, 0xFFFF80, 0xFFFF00, 0xFFFFFF, 0xFF0000, 0xFF00FF, 0xFF8000, 0xFF80FF,
    0x000080, 0x00FF80, 0x00FF00, 0x00FFFF, 0x000000, 0x0000FF, 0x008000, 0x0080FF,
    0x800080, 0x80FF80, 0x80FF00, 0x80FFFF, 0x800000, 0x8000FF, 0x808000, 0x8080FF,
};

// Green monochrome monitor brightness table, amstrad_cpc_paleta().
constexpr float kGreenClassic[32] = {
    0.5647f, 0.5647f, 0.7529f, 0.9412f, 0.1882f, 0.3765f, 0.4706f, 0.6588f,
    0.3765f, 0.9412f, 0.9098f, 0.9725f, 0.3451f, 0.4078f, 0.6275f, 0.6902f,
    0.1882f, 0.7529f, 0.7216f, 0.7843f, 0.1569f, 0.2196f, 0.4392f, 0.5020f,
    0.2824f, 0.8471f, 0.8157f, 0.8784f, 0.2510f, 0.3137f, 0.5333f, 0.5961f,
};

// One row of the keyboard matrix per keyb_line value 0..9, eventos_cpc().
// Only the plain (unshifted) keys are mapped: the CPC's shift+digit
// punctuation symbols and the numeric keypad are not covered by this port.
struct KeyBit {
    Key key;
    uint8_t mask;
};

const std::vector<KeyBit> kRow0 = {{Key::Up, 0xfe}, {Key::Right, 0xfd}, {Key::Down, 0xfb}};
const std::vector<KeyBit> kRow1 = {{Key::Left, 0xfe}};
const std::vector<KeyBit> kRow2 = {
    {Key::Enter, 0xfb}, {Key::LeftShift, 0xdf}, {Key::LeftCtrl, 0x7f}};
const std::vector<KeyBit> kRow3 = {{Key::P, 0xf7}, {Key::F1, 0xfb}};
const std::vector<KeyBit> kRow4 = {{Key::Num0, 0xfe}, {Key::Num9, 0xfd}, {Key::O, 0xfb},
                                   {Key::I, 0xf7},    {Key::L, 0xef},    {Key::K, 0xdf},
                                   {Key::M, 0xbf}};
const std::vector<KeyBit> kRow5 = {{Key::Num8, 0xfe}, {Key::Num7, 0xfd}, {Key::U, 0xfb},
                                   {Key::Y, 0xf7},    {Key::H, 0xef},    {Key::J, 0xdf},
                                   {Key::N, 0xbf},    {Key::Space, 0x7f}};
const std::vector<KeyBit> kRow6 = {{Key::Num6, 0xfe}, {Key::Num5, 0xfd}, {Key::R, 0xfb},
                                   {Key::T, 0xf7},    {Key::G, 0xef},    {Key::F, 0xdf},
                                   {Key::B, 0xbf},    {Key::V, 0x7f}};
const std::vector<KeyBit> kRow7 = {{Key::Num4, 0xfe}, {Key::Num3, 0xfd}, {Key::E, 0xfb},
                                   {Key::W, 0xf7},    {Key::S, 0xef},    {Key::D, 0xdf},
                                   {Key::C, 0xbf},    {Key::X, 0x7f}};
const std::vector<KeyBit> kRow8 = {{Key::Num1, 0xfe}, {Key::Num2, 0xfd}, {Key::Escape, 0xfb},
                                   {Key::Q, 0xf7},    {Key::Tab, 0xef},  {Key::A, 0xdf},
                                   {Key::CapsLock, 0xbf}, {Key::Z, 0x7f}};
const std::vector<KeyBit> kRow9 = {{Key::Backspace, 0x7f}};

}  // namespace

AmstradCpc::AmstradCpc(Model model) : model_(model), cpu_(kCpuClock), ay_(kAyClock, 0.8f) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);

    cpu_.set_memory_handlers([this](uint16_t address) { return read_byte(address); },
                             [this](uint16_t address, uint8_t value) { write_byte(address, value); });
    cpu_.set_io_handlers([this](uint16_t port) { return read_port(port); },
                         [this](uint16_t port, uint8_t value) { write_port(port, value); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });

    ay_.set_port_handlers([this] { return keyboard_scan(); }, nullptr, nullptr, nullptr);
    ppi_.set_port_handlers(
		[this] { return port_a_read(); },
		[this] { return port_b_read(); },
		[this] { return port_c_read(); },

		[this] (uint8_t v) { port_a_write(v); },
		nullptr,
		[this] (uint8_t v) { port_c_write(v); });
}

const char* AmstradCpc::title() const {
    switch (model_) {
        case Model::CPC464: return "Amstrad CPC 464";
        case Model::CPC664: return "Amstrad CPC 664";
        case Model::CPC6128: return "Amstrad CPC 6128";
    }
    return "Amstrad CPC";
}

bool AmstradCpc::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    const std::vector<RomEntry>* main_rom = &kCpc464Rom;
    if (model_ == Model::CPC664) main_rom = &kCpc664Rom;
    if (model_ == Model::CPC6128) main_rom = &kCpc6128Rom;

    std::vector<uint8_t> combined;
    if (!loader.load(*main_rom, combined, error)) return false;
    combined.resize(0x8000, 0);

    lower_rom_.assign(combined.begin(), combined.begin() + 0x4000);
    upper_roms_[0].assign(combined.begin() + 0x4000, combined.end());
    rom_enabled_[0] = true;

    if (model_ != Model::CPC464) {
        std::vector<uint8_t> amsdos;
        std::string amsdos_error;
        if (loader.load(kAmsdosRom, amsdos, &amsdos_error)) {
            amsdos.resize(0x4000, 0);
            upper_roms_[7] = std::move(amsdos);
            rom_enabled_[7] = true;
        } else {
            warnings_.push_back("amsdos.rom not loaded, disk BASIC extensions unavailable (" +
                                amsdos_error + ")");
        }
    }

    ram_pages_ = (model_ == Model::CPC6128) ? 8 : 4;
    ram_.assign(size_t(ram_pages_) * 0x4000, 0);

    for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);

    build_palette();
    reset();
    return true;
}

namespace {
bool ends_with_ci(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(),
                      [](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); });
}
}  // namespace

uint16_t rd16le(const uint8_t* p)
{
    return uint16_t(p[0] | (uint16_t(p[1]) << 8));
}

bool load_binary_file(const std::string& path,
                      std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);

    if (!f)
        return false;

    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();

    if (size <= 0)
        return false;

    f.seekg(0, std::ios::beg);

    out.resize(size_t(size));

    f.read(reinterpret_cast<char*>(out.data()), size);

    return bool(f);
}


bool AmstradCpc::load_media(const std::string& path,
                            std::string* error)
{
    if (ends_with_ci(path, ".sna"))
        return load_sna(path, error);

    if (ends_with_ci(path, ".dsk") ||
        ends_with_ci(path, ".edsk"))
    {
        if (model_ == Model::CPC464) {
            if (error) {
                *error =
                    "CPC464 has no disk controller. "
                    "Use CPC664 or CPC6128 for .dsk/.edsk: " + path;
            }
            return false;
        }

        std::printf("Loading DISK %s\n", path.c_str());

        const bool ok = fdc_.load_disk(0, path, error);

        if (!ok) {
            std::printf("DISK load failed: %s\n",
                        error ? error->c_str() : "(no error)");
            return false;
        }

        std::printf("DISK inserted in drive A: %d\n",
                    fdc_.disk_inserted(0) ? 1 : 0);

        return true;
    }

    return tape_.load(path, error);
}

bool AmstradCpc::load_sna(const std::string& path,
                          std::string* error)
{
    std::vector<uint8_t> buf;

    if (!load_binary_file(path, buf)) {
        if (error)
            *error = "cannot open SNA: " + path;
        return false;
    }

    if (buf.size() < 0x100) {
        if (error)
            *error = "invalid SNA";
        return false;
    }

    const uint8_t* h = buf.data();

    if (std::memcmp(h, "MV - SNA", 8) != 0) {
        if (error)
            *error = "not a CPC snapshot";
        return false;
    }

    const uint8_t version = h[0x10];

    if ((version < 1) || (version > 3)) {
        if (error)
            *error = "unsupported CPC SNA version";
        return false;
    }

    const uint16_t mem_kb = rd16le(h + 0x6b);
    const size_t mem_size = size_t(mem_kb) * 1024;

    if (buf.size() < (0x100 + mem_size)) {
        if (error)
            *error = "truncated snapshot";
        return false;
    }

    if (mem_size > ram_.size()) {
        if (error)
            *error = "snapshot requires more RAM than current CPC model";
        return false;
    }

    //
    // Reinicialización parcial.
    //
    cpu_.reset();
    ay_.reset();
    ppi_.reset();
    fdc_.reset();

    ga_ = GateArray{};
    crt_ = Crtc{};
    ppi_state_ = Ppi{};

    ppi_state_.keyb_val.fill(0xff);

    irq_asserted_ = false;
    iff1_before_ = false;
    mod_address_ = false;

    cpc_line_ = 0;

    audio_.clear();
    audio_accumulator_ = 0;
    tape_accumulator_ = 0;

    //
    // ------------------------------
    // Z80
    // ------------------------------
    //
    cpu_.f = h[0x11];
    cpu_.a = h[0x12];

    cpu_.c = h[0x13];
    cpu_.b = h[0x14];

    cpu_.e = h[0x15];
    cpu_.d = h[0x16];

    cpu_.l = h[0x17];
    cpu_.h = h[0x18];

    cpu_.r = h[0x19];
    cpu_.i = h[0x1a];

    cpu_.iff1 = (h[0x1b] & 1) != 0;
    cpu_.iff2 = (h[0x1c] & 1) != 0;

    cpu_.ix = rd16le(h + 0x1d);
    cpu_.iy = rd16le(h + 0x1f);

    cpu_.sp = rd16le(h + 0x21);

    cpu_.set_pc(rd16le(h + 0x23));

    cpu_.im = h[0x25] & 3;

    cpu_.f2 = h[0x26];
    cpu_.a2 = h[0x27];

    cpu_.c2 = h[0x28];
    cpu_.b2 = h[0x29];

    cpu_.e2 = h[0x2a];
    cpu_.d2 = h[0x2b];

    cpu_.l2 = h[0x2c];
    cpu_.h2 = h[0x2d];

    cpu_.halted = false;

    cpu_.set_irq(IrqLine::Clear);

    iff1_before_ = cpu_.iff1;

    //
    // ------------------------------
    // Gate Array
    // ------------------------------
    //
    ga_.pen = h[0x2e] & 0x1f;

    for (int i = 0; i < 17; i++) {
        ga_.pal[size_t(i)] = h[0x2f + i] & 0x1f;
    }

    const uint8_t ga_ctrl = h[0x40];

    ga_.video_mode = ga_ctrl & 3;
    ga_.nvideo = ga_.video_mode;

    ga_.rom_low = (ga_ctrl & 0x04) == 0;
    ga_.rom_high = (ga_ctrl & 0x08) == 0;

    ga_.change_video = false;
    ga_.lines_count = 0;
    ga_.lines_sync = 0;

    //
    // ------------------------------
    // Banking CPC6128
    // ------------------------------
    //
    ga_.marco = {0,1,2,3};
    ga_.marco_latch = 0;

    if (model_ == Model::CPC6128) {
        write_ram_banking(uint8_t(0xc0 | (h[0x41] & 7)));
    }

    //
    // ------------------------------
    // CRTC
    // ------------------------------
    //
    crt_ = Crtc{};

    for (int r = 0; r < 18; r++) {
        crt_.reg = uint8_t(r);
        write_crtc(1, h[0x43 + r]);
    }

    crt_.reg = h[0x42] & 0x1f;

    crt_.line_address =
        uint16_t((uint16_t(crt_.regs[12]) << 8) |
                  crt_.regs[13]);

    crt_.state_refresh_address =
        crt_.line_address;

    crt_.end_of_line_address =
        crt_.line_address;

    //
    // ------------------------------
    // ROM seleccionada
    // ------------------------------
    //
    {
        const uint8_t selected = h[0x55] & 0x0f;

        ga_.rom_selected =
            rom_enabled_[selected]
                ? selected
                : 0;
    }

    //
    // ------------------------------
    // PPI 8255
    // ------------------------------
    //
    ppi_state_.port_a_read_latch  = h[0x56];
    ppi_state_.port_a_write_latch = h[0x56];

    ppi_state_.port_c = h[0x58];

    ppi_state_.keyb_line =
        h[0x58] & 0x0f;

    ppi_state_.tape_motor =
        (h[0x58] & 0x10) != 0;

    ppi_state_.ay_control =
        uint8_t((h[0x58] >> 6) & 0x03);

    ppi_.write(3, h[0x59]);
    ppi_.write(0, h[0x56]);
    ppi_.write(1, h[0x57]);
    ppi_.write(2, h[0x58]);

    //
    // ------------------------------
    // AY-3-8912
    // ------------------------------
    //
    // Restauración mínima:
    // el estado completo depende de la API
    // interna de AY8910.
    //
    ppi_state_.port_a_write_latch = h[0x56];
    update_ay();

    //
    // ------------------------------
    // RAM
    // ------------------------------
    //
    std::fill(ram_.begin(),
              ram_.end(),
              0);

    std::memcpy(
        ram_.data(),
        buf.data() + 0x100,
        mem_size);

    //
    // ------------------------------
    // Estado vídeo
    // ------------------------------
    //
    build_palette();

    const uint8_t border_pen =
        ga_.pal[0x10] & 0x1f;

    framebuffer_.assign(
        size_t(kScreenWidth) * kScreenHeight,
        palette_[border_pen]);

    std::printf(
        "Loaded CPC SNA '%s' version=%u RAM=%uKB\n",
        path.c_str(),
        unsigned(version),
        unsigned(mem_kb));

    return true;
}



uint8_t& AmstradCpc::ram(int page, uint16_t offset) {
    const size_t index = size_t(page % ram_pages_) * 0x4000 + (offset & 0x3fff);
    return ram_[index];
}

void AmstradCpc::build_palette() {
    if (color_monitor_) {
        for (size_t index = 0; index < 32; index++) {
            const uint32_t rgb = kCpcPalette[index];
            palette_[index] = 0xff000000u | (rgb & 0xffffffu);
        }
    } else {
        for (size_t index = 0; index < 32; index++) {
            float green = kGreenClassic[index] * 255.0f * (1.0f + float(bright_) / 4.0f);
            green = std::min(green, 255.0f);
            palette_[index] = 0xff000000u | (uint32_t(green) << 8);
        }
    }
}

void AmstradCpc::reset() {
    cpu_.reset();
    ay_.reset();
    ppi_.reset();
    fdc_.reset();
    tape_.rewind();

    ga_ = GateArray{};
    ppi_state_ = Ppi{};
    ppi_state_.keyb_val.fill(0xff);
    mod_address_ = false;
    irq_asserted_ = false;
    iff1_before_ = false;
    cpc_line_ = 0;

    crt_ = Crtc{};
    crt_.char_total = 64 * 8;
    crt_.pixel_visible = 48 * 8;
    crt_.borde = uint16_t((kScreenWidth - crt_.pixel_visible) / 2);

    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    audio_.clear();
    audio_accumulator_ = 0;
    tape_accumulator_ = 0;
}

// ---------------------------------------------------------------------------
// Main CPU memory map.
// ---------------------------------------------------------------------------

uint8_t AmstradCpc::read_byte(uint16_t address) const {
    if (address <= 0x3fff) {
        if (ga_.rom_low) return lower_rom_.empty() ? 0xff : lower_rom_[address];
        return ram_[size_t(ga_.marco[0]) * 0x4000 + address];
    }
    if (address <= 0x7fff) {
        return ram_[size_t(ga_.marco[1]) * 0x4000 + (address & 0x3fff)];
    }
    if (address <= 0xbfff) {
        return ram_[size_t(ga_.marco[2]) * 0x4000 + (address & 0x3fff)];
    }
    if (ga_.rom_high && rom_enabled_[ga_.rom_selected]) {
        const std::vector<uint8_t>& slot = upper_roms_[ga_.rom_selected];
        const uint16_t offset = address & 0x3fff;
        return offset < slot.size() ? slot[offset] : 0xff;
    }
    return ram_[size_t(ga_.marco[3]) * 0x4000 + (address & 0x3fff)];
}

void AmstradCpc::write_byte(uint16_t address, uint8_t value) {
    ram_[size_t(ga_.marco[address >> 14]) * 0x4000 + (address & 0x3fff)] = value;
}

// ---------------------------------------------------------------------------
// I/O map: multiple devices can be selected at once by different address
// bits, exactly like cpc_inbyte/cpc_outbyte.
// ---------------------------------------------------------------------------

uint8_t AmstradCpc::read_port(uint16_t port) {
    uint8_t result = 0xff;
    if ((port & 0x4000) == 0) result = read_crtc(uint8_t(port >> 8));
    if ((port & 0x0800) == 0) result = ppi_.read((port >> 8) & 3);
    if ((port & 0x0400) == 0) {  // Expansion
        // bit 5 = 0: serial; bit 6 = 0: reserved (neither emulated)
        if ((port & 0x80) == 0) {  // FDC
            switch (port & 0x101) {
                case 0x100: result = fdc_.read_status(); break;
                case 0x101: result = fdc_.read_data(); break;
                default: break;  // 0/1 not used
            }
        }
    }
    // Printer is not emulated.
    return result;
}

void AmstradCpc::write_port(uint16_t port, uint8_t value) {
    if ((port & 0xc000) == 0x4000) {
        write_ga(value);
    } else if ((port & 0x4000) == 0) {
        write_crtc(uint8_t(port >> 8), value);
    }
    if ((port & 0x2000) == 0) {
        const uint8_t slot = uint8_t(value & 0x0f);
        ga_.rom_selected = rom_enabled_[slot] ? slot : 0;
    }
    if ((port & 0x1000) == 0) return;  // printer, not emulated
    if ((port & 0x0800) == 0) ppi_.write((port >> 8) & 3, value);
    if ((port & 0x0400) == 0) {  // Expansion
        // bit 5 = 0: serial; bit 6 = 0: reserved (neither emulated)
        if ((port & 0x80) == 0) {  // FDC
            switch (port & 0x101) {
                case 0: case 1: fdc_.write_motor(value); break;
                case 0x100: case 0x101: fdc_.write_data(value); break;
                default: break;
            }
        }
    }
}

void AmstradCpc::write_ga(uint8_t value) {
    switch (value >> 6) {
        case 0:
            ga_.pen = (value & 0x10) == 0 ? uint8_t(value & 0x0f) : uint8_t(0x10);
            break;
        case 1:
            ga_.pal[ga_.pen] = uint8_t(value & 0x1f);
            break;
        case 2: {
            if (ga_.nvideo != (value & 3)) {
                ga_.nvideo = uint8_t(value & 3);
                ga_.change_video = true;
            }
            ga_.rom_low = (value & 4) == 0;
            ga_.rom_high = (value & 8) == 0;
            if ((value & 0x10) != 0) {
                ga_.lines_count = 0;
                cpu_.set_irq(IrqLine::Clear);
                irq_asserted_ = false;
            }
            break;
        }
        case 3:
            write_ram_banking(value);
            break;
        default:
            break;
    }
}

void AmstradCpc::write_ram_banking(uint8_t value) {
    if (model_ != Model::CPC6128) return;  // only the 6128 has a bank switcher
    ga_.marco[0] = 0;
    ga_.marco[1] = 1;
    ga_.marco[2] = 2;
    ga_.marco[3] = 3;
    constexpr uint8_t kPage = 4;  // the extra 64K bank (no silicon disk expansion)
    switch (value & 7) {
        case 1: ga_.marco[3] = uint8_t(3 + kPage); break;
        case 2:
            ga_.marco[0] = uint8_t(0 + kPage);
            ga_.marco[1] = uint8_t(1 + kPage);
            ga_.marco[2] = uint8_t(2 + kPage);
            ga_.marco[3] = uint8_t(3 + kPage);
            break;
        case 3:
            ga_.marco[1] = 3;
            ga_.marco[3] = uint8_t(3 + kPage);
            break;
        case 4: ga_.marco[1] = uint8_t(0 + kPage); break;
        case 5: ga_.marco[1] = uint8_t(1 + kPage); break;
        case 6: ga_.marco[1] = uint8_t(2 + kPage); break;
        case 7: ga_.marco[1] = uint8_t(3 + kPage); break;
        default: break;
    }
    ga_.marco_latch = uint8_t(value & 7);
}

void AmstradCpc::write_crtc(uint8_t port, uint8_t value) {
    switch (port & 3) {
        case 0:
            crt_.reg = uint8_t(value & 0x1f);
            break;
        case 1: {
            if (crt_.reg >= crt_.regs.size()) break;
            const uint8_t masked = uint8_t(value & kCrtcMasks[crt_.reg]);
            if (crt_.regs[crt_.reg] == masked) break;
            crt_.regs[crt_.reg] = masked;
            switch (crt_.reg) {
                case 0:
                    crt_.char_total = uint16_t((crt_.regs[0] + 1) * 8);
                    break;
                case 1:
                    crt_.pixel_visible = uint16_t((crt_.regs[1] < 50 ? crt_.regs[1] : 49) * 8);
                    crt_.borde = uint16_t((kScreenWidth - crt_.pixel_visible) / 2);
                    break;
                case 5:
                    crt_.is_in_adjustment_period = false;
                    crt_.adj_count = 0;
                    break;
                case 9:
                    mod_address_ = true;
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            break;  // registers 2/3 (select/status) are write only in this model
    }
}

uint8_t AmstradCpc::read_crtc(uint8_t port) const {
    switch (port & 3) {
        case 2: return 0x80;
        case 3:
            if (crt_.reg >= 12 && crt_.reg <= 17) return crt_.regs[crt_.reg];
            return 0;
        default: return 0;  // registers 0/1 are write only
    }
}

// ---------------------------------------------------------------------------
// PPI 8255 ports.
// ---------------------------------------------------------------------------

void AmstradCpc::update_ay() {
    switch (ppi_state_.ay_control) {
        case 0: ppi_state_.port_a_read_latch = 0xff; break;
        case 1: ppi_state_.port_a_read_latch = ay_.read(); break;
        case 2: ay_.write(ppi_state_.port_a_write_latch); break;
        case 3: ay_.control(ppi_state_.port_a_write_latch); break;
        default: break;
    }
}

uint8_t AmstradCpc::port_a_read() {
    update_ay();
    return ppi_state_.port_a_read_latch;
}

// Bit 0: VSYNC. Bits 1-6: fixed manufacturer/refresh/expansion/printer status
// ($7e). Bit 7: cassette read data. The reference engine happened to shift the
// tape bit into bit 1 (already forced high by the $7e mask, so it never had
// any effect); this port follows the hardware bit layout documented in its
// own comment so that tape loading actually works.
uint8_t AmstradCpc::port_b_read() const {
    uint8_t value = 0x7e;
    if (crt_.state_vsync) value |= 0x01;
    if (tape_.playing() && tape_.ear()) value |= 0x80;
    return value;
}

void AmstradCpc::port_a_write(uint8_t value) {
    ppi_state_.port_a_write_latch = value;
    update_ay();
}

void AmstradCpc::port_c_write(uint8_t value)
{
    const uint8_t old_pc = ppi_state_.port_c;

    ppi_state_.port_c = value;

    //
    // PC0-PC3 = keyboard row select
    //
    ppi_state_.keyb_line = value & 0x0f;

    //
    // PC4 = cassette motor
    //
    // Mirror tape_timer_exec() from the original amstrad_cpc.pas: when the
    // firmware turns the motor on, start the virtual tape; when it turns it
    // off, stop it. Without this the SpectrumTape player never leaves the
    // paused state and CDT/TZX loading does nothing.
    const bool motor_on = (value & 0x10) != 0;
    if (motor_on != ppi_state_.tape_motor) {
        ppi_state_.tape_motor = motor_on;
        if (tape_.loaded()) {
            tape_.set_playing(motor_on);
        }
    }

    //
    // PC6-PC7 = AY control
    //
    const uint8_t new_ay_control =
        uint8_t((value >> 6) & 0x03);

    if (new_ay_control != ppi_state_.ay_control ||
        ((old_pc ^ value) & 0xc0))
    {
        ppi_state_.ay_control = new_ay_control;
        update_ay();
    }
}

uint8_t AmstradCpc::port_c_read() const
{
    return ppi_state_.port_c;
}

uint8_t AmstradCpc::keyboard_scan() {
    return ppi_state_.keyb_line < 16 ? ppi_state_.keyb_val[ppi_state_.keyb_line] : 0xff;
}

// ---------------------------------------------------------------------------
// Video: CRTC + gate array, one 4 MHz T-state clock at a time.
// ---------------------------------------------------------------------------

void AmstradCpc::fill_line(int line, uint32_t color) {
    if (line < 0 || line >= kScreenHeight) return;
    uint32_t* row = framebuffer_.data() + size_t(line) * kScreenWidth;
    std::fill(row, row + kScreenWidth, color);
}

void AmstradCpc::adjust_vertical_total() {
    if (crt_.adj_count == crt_.regs[5]) {
        crt_.is_in_adjustment_period = false;
        crt_.line_address = uint16_t((crt_.regs[12] << 8) | crt_.regs[13]);
        crt_.state_refresh_address = crt_.line_address;
        crt_.adj_count = 0;
        crt_.next_line_is_visible = true;
    } else {
        crt_.adj_count = uint8_t((crt_.adj_count + 1) & 0x1f);
    }
}

void AmstradCpc::do_end_of_line() {
    if (ga_.change_video) {
        ga_.video_mode = ga_.nvideo;
        ga_.change_video = false;
        if (mod_address_) {
            crt_.line_address = uint16_t((crt_.regs[12] << 8) | crt_.regs[13]);
            crt_.state_refresh_address = crt_.line_address;
            mod_address_ = false;
        }
    }

    cpc_line_ = (cpc_line_ + 1) % kScreenHeight;
    crt_.pant_x = 0;
    crt_.pant_addr = uint16_t(((crt_.line_address & 0x3ff) << 1) |
                              ((crt_.state_row_address & 7) << 11) |
                              ((crt_.line_address & 0x3000) << 2));

    if (crt_.next_line_is_visible) {
        crt_.line_is_visible = true;
        crt_.next_line_is_visible = false;
    }
    if (crt_.next_line_no_visible) {
        crt_.line_is_visible = false;
        crt_.next_line_no_visible = false;
    }

    if (crt_.state_vsync) {
        uint8_t tempb = uint8_t(crt_.regs[3] >> 4);
        if (tempb == 0) tempb = 16;
        if (crt_.vsync_counter == tempb) crt_.state_vsync = false;
        else crt_.vsync_counter++;
    }
    if (crt_.regs[4] >= crt_.regs[7] && crt_.char_crt == crt_.regs[7]) {
        crt_.state_vsync = true;
        crt_.vsync_counter = 0;
    }

    if (crt_.is_in_adjustment_period) {
        adjust_vertical_total();
    } else {
        if (crt_.state_row_address == crt_.regs[9]) {
            crt_.state_row_address = 0;
            crt_.line_address = crt_.end_of_line_address;
            if (crt_.char_crt == crt_.regs[4]) {
                crt_.is_in_adjustment_period = true;
                crt_.adj_count = 0;
                crt_.char_crt = 0;
                adjust_vertical_total();
            } else {
                crt_.char_crt = uint8_t((crt_.char_crt + 1) & 0x7f);
            }
        } else {
            crt_.state_row_address = uint8_t((crt_.state_row_address + 1) & 0x1f);
        }
        if (crt_.char_crt == crt_.regs[6]) crt_.next_line_no_visible = true;
    }

    const bool was_vsync = crt_.was_vsync;
    if (!was_vsync && crt_.state_vsync) ga_.lines_sync = 2;
    if (was_vsync && !crt_.state_vsync) cpc_line_ = 0;
    crt_.was_vsync = crt_.state_vsync;

    fill_line(cpc_line_, palette_[ga_.pal[0x10]]);
}

void AmstradCpc::draw_pixels() {
    for (int g = 0; g < 2; g++) {
        if (crt_.pant_x < crt_.pixel_visible) {
            // Video RAM always reads the base 64K, regardless of ROM/RAM banking.
            const uint8_t val = ram(crt_.pant_addr >> 14, crt_.pant_addr & 0x3fff);
            if ((crt_.pant_addr & 0x7ff) == 0x7ff) crt_.pant_addr = uint16_t(crt_.pant_addr & 0xf800);
            else crt_.pant_addr++;

            uint32_t samples[4];
            switch (ga_.video_mode) {
                case 0: {
                    const uint8_t p1 = uint8_t(((val & 2) << 2) | ((val & 0x20) >> 3) |
                                               ((val & 8) >> 2) | ((val & 0x80) >> 7));
                    const uint8_t p2 = uint8_t(((val & 1) << 3) | ((val & 0x10) >> 2) |
                                               ((val & 4) >> 1) | ((val & 0x40) >> 6));
                    samples[0] = samples[1] = palette_[ga_.pal[p1]];
                    samples[2] = samples[3] = palette_[ga_.pal[p2]];
                    break;
                }
                case 1: {
                    const uint8_t p1 = uint8_t(((val & 0x80) >> 7) + ((val & 8) >> 2));
                    const uint8_t p2 = uint8_t(((val & 0x40) >> 6) + ((val & 4) >> 1));
                    const uint8_t p3 = uint8_t(((val & 0x20) >> 5) + ((val & 2) >> 0));
                    const uint8_t p4 = uint8_t(((val & 0x10) >> 4) + ((val & 1) << 1));
                    samples[0] = palette_[ga_.pal[p1]];
                    samples[1] = palette_[ga_.pal[p2]];
                    samples[2] = palette_[ga_.pal[p3]];
                    samples[3] = palette_[ga_.pal[p4]];
                    break;
                }
                default: {
                    // Mode 2 (640x200 mono) is blended pairwise into the same
                    // 4-samples-per-byte cadence as modes 0/1, matching the
                    // reference engine's fixed-width 400 pixel canvas.
                    const uint8_t bits[8] = {
                        uint8_t((val & 0x80) >> 7), uint8_t((val & 0x40) >> 6),
                        uint8_t((val & 0x20) >> 5), uint8_t((val & 0x10) >> 4),
                        uint8_t((val & 0x08) >> 3), uint8_t((val & 0x04) >> 2),
                        uint8_t((val & 0x02) >> 1), uint8_t((val & 0x01) >> 0)};
                    for (int pair = 0; pair < 4; pair++) {
                        const uint32_t c1 = palette_[ga_.pal[bits[pair * 2]]];
                        const uint32_t c2 = palette_[ga_.pal[bits[pair * 2 + 1]]];
                        const uint32_t r = (((c1 >> 16) & 0xff) + ((c2 >> 16) & 0xff)) / 2;
                        const uint32_t g2 = (((c1 >> 8) & 0xff) + ((c2 >> 8) & 0xff)) / 2;
                        const uint32_t b = ((c1 & 0xff) + (c2 & 0xff)) / 2;
                        samples[pair] = 0xff000000u | (r << 16) | (g2 << 8) | b;
                    }
                    break;
                }
            }

            const int x = crt_.borde + crt_.pant_x;
            if (cpc_line_ >= 0 && cpc_line_ < kScreenHeight) {
                uint32_t* row = framebuffer_.data() + size_t(cpc_line_) * kScreenWidth;
                for (int i = 0; i < 4; i++) {
                    const int px = x + i;
                    if (px >= 0 && px < kScreenWidth) row[px] = samples[i];
                }
            }
        }
        crt_.pant_x = uint16_t(crt_.pant_x + 4);
    }
}

void AmstradCpc::advance_video(int cycles) {
    // Matches the reference's `estados_t shr 2`: any remainder below 4 T-states
    // is dropped rather than carried over, the same approximation the source
    // engine uses (the CRTC clock is only ever advanced in whole character
    // clocks per instruction).
    const int ticks = cycles / 4;
    for (int i = 0; i < ticks; i++) {
        if (crt_.pant_x < crt_.char_total && crt_.line_is_visible && !crt_.state_vsync &&
            !crt_.state_hsync) {
            draw_pixels();
        }

        if (crt_.state_hsync) {
            uint8_t tempb = uint8_t(crt_.regs[3] & 0x0f);
            if (tempb == 0) tempb = 16;
            if (crt_.hsync_counter == tempb) crt_.state_hsync = false;
            else crt_.hsync_counter++;
        }
        if (crt_.regs[0] >= crt_.regs[2] && crt_.character_counter == crt_.regs[2]) {
            crt_.hsync_counter = 0;
            crt_.state_hsync = true;
        }

        if (crt_.character_counter == crt_.regs[1]) {
            crt_.end_of_line_address = crt_.state_refresh_address;
        } else {
            crt_.state_refresh_address = uint16_t((crt_.state_refresh_address + 1) & 0x3fff);
        }

        if (crt_.character_counter == crt_.regs[0]) {
            do_end_of_line();
            crt_.character_counter = 0;
            crt_.state_refresh_address = crt_.line_address;
        } else {
            crt_.character_counter++;
        }

        if (crt_.was_hsync && !crt_.state_hsync) {
            // Gate array 300 Hz interrupt generator, ga_exec().
            ga_.lines_count++;
            if (ga_.lines_sync > 0) {
                ga_.lines_sync--;
                if (ga_.lines_sync == 0) {
                    if (ga_.lines_count >= 32) {
                        cpu_.set_irq(IrqLine::Assert);
                        irq_asserted_ = true;
                    }
                    ga_.lines_count = 0;
                }
            } else if (ga_.lines_count == 52) {
                ga_.lines_count = 0;
                cpu_.set_irq(IrqLine::Assert);
                irq_asserted_ = true;
            }
        }
        crt_.was_hsync = crt_.state_hsync;
    }
}

// ---------------------------------------------------------------------------
// CPU cycle hook: tape clock, interrupt acknowledge and video/audio ticking.
// ---------------------------------------------------------------------------

void AmstradCpc::on_cycles(int cycles) {
    // The tape engine is built for the Spectrum's 3.5 MHz clock; scale by
    // 3500000/4000000 = 7/8 like play_cinta_tzx(trunc(estados_t*0.875)).
    tape_accumulator_ += int64_t(cycles) * 7;
    const int64_t tape_cycles = tape_accumulator_ / 8;
    tape_accumulator_ -= tape_cycles * 8;
    tape_.advance(int(tape_cycles));

    // Detect the CPU actually taking the maskable interrupt (iff1 goes from
    // set to clear while the gate array line is asserted: DI cannot be the
    // cause, since an asserted+enabled IRQ always pre-empts the next opcode
    // fetch), mirroring amstrad_raised_z80's CLEAR_LINE-on-acknowledge.
    const bool iff1_now = cpu_.iff1;
    if (irq_asserted_ && iff1_before_ && !iff1_now) {
        cpu_.set_irq(IrqLine::Clear);
        irq_asserted_ = false;
        ga_.lines_count = uint8_t(ga_.lines_count & 0x1f);
    }
    iff1_before_ = iff1_now;

    advance_video(cycles);

    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= int64_t(kCpuClock)) {
        audio_accumulator_ -= int64_t(kCpuClock);
        int32_t sample = ay_.update();
        if (tape_.playing() && tape_.ear()) sample += 4000;
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void AmstradCpc::run_frame() { cpu_.run(kCyclesPerFrame); }

// ---------------------------------------------------------------------------
// Inputs.
// ---------------------------------------------------------------------------

namespace {
void apply_row(std::array<uint8_t, 16>& keyb_val, int row, const std::vector<KeyBit>& bits,
              const MachineInputs& inputs) {
    for (const KeyBit& entry : bits) {
        if (inputs.key(entry.key)) keyb_val[size_t(row)] &= entry.mask;
    }
}
}  // namespace

void AmstradCpc::set_inputs(const MachineInputs& inputs) {
    ppi_state_.keyb_val.fill(0xff);

    apply_row(ppi_state_.keyb_val, 0, kRow0, inputs);
    apply_row(ppi_state_.keyb_val, 1, kRow1, inputs);
    apply_row(ppi_state_.keyb_val, 2, kRow2, inputs);
    apply_row(ppi_state_.keyb_val, 3, kRow3, inputs);
    apply_row(ppi_state_.keyb_val, 4, kRow4, inputs);
    apply_row(ppi_state_.keyb_val, 5, kRow5, inputs);
    apply_row(ppi_state_.keyb_val, 7, kRow7, inputs);
    apply_row(ppi_state_.keyb_val, 8, kRow8, inputs);
    apply_row(ppi_state_.keyb_val, 9, kRow9, inputs);

    apply_row(ppi_state_.keyb_val, 6, kRow6, inputs);

    // Row 6 doubles as joystick 2 and row 9 as joystick 1, wired straight into
    // the keyboard matrix on real hardware. Disabled by default (see the
    // joystick_enabled_ comment in the header): the shared front end's
    // hard-coded joystick scancodes overlap ordinary letters and space.
    if (joystick_enabled_) {
        const InputState& p1 = inputs.player1;
        if (p1.up) ppi_state_.keyb_val[9] &= 0xfe;
        if (p1.down) ppi_state_.keyb_val[9] &= 0xfd;
        if (p1.left) ppi_state_.keyb_val[9] &= 0xfb;
        if (p1.right) ppi_state_.keyb_val[9] &= 0xf7;
        if (p1.button1) ppi_state_.keyb_val[9] &= 0xef;
        if (p1.button2) ppi_state_.keyb_val[9] &= 0xdf;

        const InputState& p2 = inputs.player2;
        if (p2.up) ppi_state_.keyb_val[6] &= 0xfe;
        if (p2.down) ppi_state_.keyb_val[6] &= 0xfd;
        if (p2.left) ppi_state_.keyb_val[6] &= 0xfb;
        if (p2.right) ppi_state_.keyb_val[6] &= 0xf7;
        if (p2.button1) ppi_state_.keyb_val[6] &= 0xef;
        if (p2.button2) ppi_state_.keyb_val[6] &= 0xdf;
    }
}

void AmstradCpc::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) {
        const bool color = value != 0;
        if (color != color_monitor_) {
            color_monitor_ = color;
            build_palette();
        }
    } else if (bank == 1) {
        joystick_enabled_ = value != 0;
    }
}

void AmstradCpc::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
