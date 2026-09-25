#include "drivers/computers/exelv.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kExl100Main = {
    {"exl100in.bin|exl100.bin", 0x0800, 0x0000, 0x049109a3},
};
const std::vector<RomEntry> kExl100Sub = {
    {"exl100_7041.bin|exl100io.bin", 0x1000, 0x0000, 0x38f6fc7a},
};
const std::vector<RomEntry> kExeltelMain = {
    {"exeltel_7040.bin|exeltel.bin", 0x1000, 0x0000, 0x2792f02f},
};
const std::vector<RomEntry> kExeltelSub = {
    {"exeltel_7042.bin", 0x1000, 0x0000, 0xa0163507},
};
const std::vector<RomEntry> kExeltelSys = {
    {"exeltel14.bin|amper.bin|exeltel.rom", 0x10000, 0x0000, 0},
};

bool read_plain_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(size_t(n));
    f.read(reinterpret_cast<char*>(out.data()), n);
    return bool(f);
}

std::string lower_copy(std::string value) {
    for (char& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool is_bios_name(const std::string& name) {
    const std::string lower = lower_copy(name);
    return lower.find("exl100") != std::string::npos ||
           lower.find("exeltel") != std::string::npos ||
           lower.find("7040") != std::string::npos ||
           lower.find("7041") != std::string::npos ||
           lower.find("7042") != std::string::npos ||
           lower == "amper.bin" || lower == "cm62312.bin";
}

void fill_idle_rom(std::vector<uint8_t>& rom, uint16_t rom_base) {
    std::fill(rom.begin(), rom.end(), 0x00);  // NOP
    if (rom.empty()) return;
    rom[0] = 0x01;  // IDLE at the reset target
    const uint16_t vector = rom_base;
    rom[rom.size() - 2] = uint8_t(vector >> 8);
    rom[rom.size() - 1] = uint8_t(vector);
}

// Keyboard channels (row * 8 + column) as in MAME's exelv input ports. The
// host keys follow MAME's PORT_CODEs; keys the EXL-100 labels differently use
// the physical position ([ ] \\ = ; and Left Alt for FCT).
const Key kMatrix[8][8] = {
    {Key::Z, Key::Up, Key::Right, Key::Down, Key::Left, Key::E, Key::Space, Key::Equals},
    {Key::LeftCtrl, Key::CapsLock, Key::X, Key::Slash, Key::Escape, Key::R, Key::Comma, Key::Cbm},
    {Key::Tab, Key::Home, Key::V, Key::Quote, Key::Num1, Key::Num6, Key::Num8, Key::Num0},
    {Key::A, Key::Backspace, Key::C, Key::Period, Key::Num2, Key::Num3, Key::Num9, Key::Minus},
    {Key::LeftShift, Key::O, Key::H, Key::Asterisk, Key::T, Key::M, Key::N, Key::G},
    {Key::S, Key::U, Key::K, Key::Backslash, Key::Y, Key::Count, Key::B, Key::D},
    {Key::W, Key::P, Key::J, Key::At, Key::Num4, Key::Num7, Key::RightAlt, Key::F},
    {Key::Q, Key::I, Key::L, Key::Enter, Key::Num5, Key::Semicolon, Key::Count, Key::Count},
};

}  // namespace

Exelv::Exelv(Model model)
    : model_(model),
      maincpu_(model == Model::Exl100 ? kExl100Crystal : kExeltelCrystal,
               model == Model::Exl100 ? Tms7000::Chip::Tms7020 : Tms7000::Chip::Tms7040,
               model == Model::Exl100 ? 2u : 4u),
      subcpu_(model == Model::Exl100 ? kExl100Crystal : kExeltelCrystal,
              model == Model::Exl100 ? Tms7000::Chip::Tms7041 : Tms7000::Chip::Tms7042,
              model == Model::Exl100 ? 2u : 4u),
      speech_(640000) {
    maincpu_.set_exl_lvdp(true);
    maincpu_.set_memory_handlers([this](uint16_t a) { return read_main(a); },
                                 [this](uint16_t a, uint8_t v) { write_main(a, v); });
    maincpu_.set_port_in(Tms7000::kPortA, [this]() { return tms7020_porta_r(); });
    maincpu_.set_port_out(Tms7000::kPortB, [this](uint8_t v) { tms7020_portb_w(v); });
    maincpu_.set_cycle_handler([this](int c) { on_main_cycles(c); });

    subcpu_.set_memory_handlers([this](uint16_t a) { return read_sub(a); },
                                [this](uint16_t a, uint8_t v) { write_sub(a, v); });
    subcpu_.set_port_in(Tms7000::kPortA, [this]() { return tms7041_porta_r(); });
    subcpu_.set_port_out(Tms7000::kPortB, [this](uint8_t v) { tms7041_portb_w(v); });
    subcpu_.set_port_in(Tms7000::kPortC, [this]() { return tms7041_portc_r(); });
    subcpu_.set_port_out(Tms7000::kPortC, [this](uint8_t v) { tms7041_portc_w(v); });
    subcpu_.set_port_in(Tms7000::kPortD, [this]() { return tms7041_portd_r(); });
    subcpu_.set_port_out(Tms7000::kPortD, [this](uint8_t v) { tms7041_portd_w(v); });
    speech_.set_irq_callback([this](bool on) { speech_irq_ = on; });
}

const char* Exelv::title() const {
    return model_ == Model::Exeltel ? "Exelvision EXELTEL" : "Exelvision EXL-100";
}

void Exelv::install_dummy_bios() {
    std::vector<uint8_t> main_rom(model_ == Model::Exl100 ? 0x800 : 0x1000);
    std::vector<uint8_t> sub_rom(0x1000);
    fill_idle_rom(main_rom, model_ == Model::Exl100 ? 0xf800 : 0xf000);
    fill_idle_rom(sub_rom, 0xf000);
    maincpu_.set_internal_rom(main_rom.data(), main_rom.size());
    subcpu_.set_internal_rom(sub_rom.data(), sub_rom.size());
    sub_present_ = true;
    bios_loaded_ = true;
}

bool Exelv::load_bios(const std::string& rom_path, std::string* error) {
    (void)error;
    auto load_entries = [&](const std::vector<RomEntry>& entries, std::vector<uint8_t>& dest) {
        dest.clear();
        RomLoader loader;
        std::string ignored;
        if (!loader.open(rom_path, &ignored)) return false;
        dest.assign(entries[0].length, 0);
        if (!loader.load(entries, dest, &ignored)) {
            dest.clear();
            return false;
        }
        return true;
    };

    std::vector<uint8_t> main_rom, sub_rom;
    if (model_ == Model::Exl100) {
        if (!load_entries(kExl100Main, main_rom)) return false;
        load_entries(kExl100Sub, sub_rom);
    } else {
        if (!load_entries(kExeltelMain, main_rom)) return false;
        load_entries(kExeltelSub, sub_rom);
        // MAME ships this 7042 image as BAD_DUMP (CRC a0163507). It is the
        // EXL-100 TMS7041 program with the second and third KiB swapped (an
        // address-line mix-up while dumping): $F400-$F7FF holds the 7041's
        // $F800-$FBFF and vice versa, the first and last KiB are identical.
        // Swapping them back gives a working I/O CPU (IR keyboard, speech,
        // mailbox) instead of a hang on the $04 it posted before.
        if (sub_rom.size() == 0x1000 && crc32_of(sub_rom.data(), sub_rom.size()) == 0xa0163507) {
            std::swap_ranges(sub_rom.begin() + 0x400, sub_rom.begin() + 0x800,
                             sub_rom.begin() + 0x800);
            // The one EXELTEL difference the TMS7040 checks: its character
            // generator request ($0B) expects the font from character 1, so
            // glyph 64 lands on 'A' at $C380 (system ROM $2168). The 7041
            // sends from character 0 (MOVD %>F722,R17 at $F2EB); start one
            // 10-byte glyph later, or the system ROM resets the I/O CPU
            // forever.
            if (sub_rom[0x2eb] == 0x88 && sub_rom[0x2ec] == 0xf7 && sub_rom[0x2ed] == 0x22) {
                sub_rom[0x2ed] = 0x2c;
            }
            // Command $01 is a NOP on the EXELTEL (sent after each speech
            // phrase); on the 7041 it is a serial routine that swallows the
            // next command. Point it at the end of command $08 ($F247:
            // AND %>EF,R9 / RETS), which clears the "command busy" flag the
            // main loop checks before it reports keys again.
            if (sub_rom[0x1e7] == 0xf3 && sub_rom[0x1e8] == 0x61 &&
                sub_rom[0x247] == 0x73 && sub_rom[0x248] == 0xef && sub_rom[0x24a] == 0x0a) {
                sub_rom[0x1e7] = 0xf2;
                sub_rom[0x1e8] = 0x47;
            }
        }
        std::vector<uint8_t> sys(0x10000, 0);
        RomLoader loader;
        std::string ignored;
        if (loader.open(rom_path, &ignored) && loader.load(kExeltelSys, sys, &ignored)) {
            system_rom_ = std::move(sys);
        }
    }

    if (main_rom.size() < (model_ == Model::Exl100 ? 0x800u : 0x1000u)) return false;
    maincpu_.set_internal_rom(main_rom.data(), main_rom.size());
    if (sub_rom.size() >= 0x800) {
        subcpu_.set_internal_rom(sub_rom.data(), sub_rom.size());
        sub_present_ = true;
    } else {
        sub_present_ = false;
        if (model_ == Model::Exl100) {
            warnings_.push_back("I/O CPU ROM missing; mailbox init is HLE'd");
        } else if (warnings_.empty() ||
                   warnings_.back().find("BAD_DUMP") == std::string::npos) {
            warnings_.push_back("I/O CPU ROM missing; mailbox $08 is HLE'd");
        }
    }
    bios_loaded_ = true;
    return true;
}

bool Exelv::init(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    bios_loaded_ = false;
    sub_present_ = false;
    cart_.clear();
    system_rom_.clear();
    warnings_.clear();

    if (!rom_path.empty() && load_bios(rom_path, error)) {
        if (fs::is_directory(rom_path, ec)) {
            for (const auto& item : fs::directory_iterator(rom_path, ec)) {
                if (!item.is_regular_file(ec)) continue;
                const std::string name = item.path().filename().string();
                if (is_bios_name(name)) continue;
                const std::string ext = lower_copy(item.path().extension().string());
                if (ext == ".bin" || ext == ".rom" || ext == ".cart") {
                    std::vector<uint8_t> data;
                    if (read_plain_file(item.path().string(), data)) {
                        load_cart_bytes(std::move(data), error);
                        break;
                    }
                }
            }
        }
        reset();
        return true;
    }

    if (error && error->empty()) {
        *error = model_ == Model::Exeltel
                     ? "cannot load EXELTEL BIOS (exeltel_7040.bin)"
                     : "cannot load EXL-100 BIOS (exl100in.bin)";
    }
    return false;
}

bool Exelv::load_cart_bytes(std::vector<uint8_t> data, std::string* error) {
    if (data.empty()) {
        if (error) *error = "empty cartridge";
        return false;
    }
    cart_ = std::move(data);
    return true;
}

bool Exelv::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string ext = lower_copy(fs::path(path).extension().string());
    if (ext == ".k7" || ext == ".wav") {
        if (!read_plain_file(path, data)) {
            if (error) *error = "cannot read cassette " + path;
            return false;
        }
        const bool ok = ext == ".wav" ? tape_.load_wav(data, error) : tape_.load(std::move(data));
        if (!ok) {
            if (error && error->empty()) *error = "empty cassette " + path;
            return false;
        }
        if (tape_save_path_.empty()) {
            fs::path save = fs::path(path);
            save.replace_filename(save.stem().string() + "-save.k7");
            tape_save_path_ = save.string();
        }
        return true;
    }
    if (fs::is_regular_file(path, ec)) {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {};
        probe.read(magic, 4);
        const bool is_zip = probe.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K';
        if (!is_zip) {
            if (!read_plain_file(path, data)) {
                if (error) *error = "cannot read " + path;
                return false;
            }
            return load_cart_bytes(std::move(data), error);
        }
    }
    RomLoader loader;
    if (!loader.open(path, error)) return false;
    if (!loader.load_first_file(data, error)) return false;
    return load_cart_bytes(std::move(data), error);
}

void Exelv::reset() {
    ram_.fill(0);
    tms7020_portb_ = 0;
    tms7041_portb_ = 0;
    tms7041_portc_ = 0;
    tms7041_portd_ = 0;
    wx318_ = 0;
    wx319_ = 0;
    speech_irq_ = false;
    main_debt_ = sub_debt_ = 0;
    page_bit1_ = page_bit2_ = false;
    last_sent_ = 0;
    last_key_ = 0;
    tape_.rewind();
    tape_idle_frames_ = 0;
    chord_phase_ = 0;
    chord_key_ = chord_mod_ = 0xff;
    p64_ = 0;
    hle_io_sent_ = false;
    hle_io_delay_ = int(maincpu_.cpu_clock() / 5);  // ~0.2 s
    k_channels_[0] = 0xff;
    k_channels_[1] = 0xff;
    k_channels_[2] = 0x3e;
    k_ch_byte_ = 0;
    k_ch_bit_ = 0;
    k_bit_bit_ = false;
    k_bit_num_ = false;
    k_timer_cycles_ = 0;
    k_started_ = false;
    k_boot_cycles_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
    cass_bit_ = 1;
    vdp_.reset();
    speech_.reset();
    maincpu_.reset();
    if (sub_present_) {
        subcpu_.reset();
        subcpu_.set_input_line(Tms7000::kInt1, IrqLine::Clear);
        subcpu_.set_input_line(Tms7000::kInt3, IrqLine::Clear);
    }
}

uint8_t Exelv::cart_r(uint16_t offset) const {
    if (model_ == Model::Exeltel) {
        // EXELTEL pages $0200-$7FFF. Page 2 is the upper half of the 64 KiB
        // system ROM (the telematics environment, entry $7FFD -> BR $0203,
        // which stores 2 as its own page), page 3 the lower half (the
        // questionnaire / calculator the upper half calls as page 3, $020F).
        // The TMS7040 boot probes pages 6, 4 and 2 for an $AA/$55 signature
        // at $7FFC; an EXL-100 cartridge answers on page 6.
        const int page = exeltel_page();
        if (page == 2 || page == 3) {
            const size_t addr = size_t(offset) + 0x200 + (page == 2 ? 0x8000 : 0);
            return addr < system_rom_.size() ? system_rom_[addr] : 0xff;
        }
        if (page != 6) return 0xff;
    }
    if (cart_.empty()) return 0xff;
    // The cartridge slot is linear and mirrors smaller ROMs across
    // $0200-$7FFF (MAME generic_rom_linear_device): an 8 or 16 KiB cart
    // carries its $AA signature at the end, which the BIOS reads at $7FFC.
    // A $7E00-byte image starts at $0200.
    if (cart_.size() == 0x7e00) return cart_[offset % cart_.size()];
    return cart_[(size_t(offset) + 0x200) % cart_.size()];
}

int Exelv::exeltel_page() const {
    // Page bit 0 is port B bit 2 (inverted); bits 1 and 2 are set by reading
    // P56/P57 and cleared by writing them, and only count while P64 bit 6 is
    // set (TMS7040 routine $F2F4).
    int page = (tms7020_portb_ & 0x04) ? 0 : 1;
    if (p64_ & 0x40) page |= (page_bit1_ ? 2 : 0) | (page_bit2_ ? 4 : 0);
    return page;
}

uint8_t Exelv::read_main(uint16_t address) {
    if (address == 0x0124) return vdp_.vram_r();
    if (address == 0x0125) return vdp_.reg_r();
    if (address == 0x0128) return vdp_.initptr_r();
    if (address == 0x0130) return mailbox_wx319_r();
    if (model_ == Model::Exeltel) {
        if (address == 0x0138) {
            page_bit1_ = true;
            return 0xff;
        }
        if (address == 0x0139) {
            page_bit2_ = true;
            return 0xff;
        }
        if (address == 0x0140) return p64_;
    }
    if (address >= 0x0200 && address <= 0x7fff) return cart_r(uint16_t(address - 0x0200));
    if (address >= 0xc000 && address <= 0xc7ff) return ram_[address - 0xc000];
    return 0xff;
}

void Exelv::write_main(uint16_t address, uint8_t value) {
    if (address == 0x012d) {
        vdp_.reg_w(value);
        return;
    }
    if (address == 0x012e) {
        vdp_.vram_w(value);
        return;
    }
    if (address == 0x0130) {
        mailbox_wx318_w(value);
        return;
    }
    if (model_ == Model::Exeltel) {
        if (address == 0x0138) {
            page_bit1_ = false;
            return;
        }
        if (address == 0x0139) {
            page_bit2_ = false;
            return;
        }
        if (address == 0x0140) {
            p64_ = value;
            return;
        }
    }
    if (address >= 0xc000 && address <= 0xc7ff) ram_[address - 0xc000] = value;
}

uint8_t Exelv::read_sub(uint16_t) { return 0xff; }
void Exelv::write_sub(uint16_t, uint8_t) {}

uint8_t Exelv::mailbox_wx319_r() { return wx319_; }
void Exelv::mailbox_wx318_w(uint8_t data) { wx318_ = data; }

uint8_t Exelv::tms7020_porta_r() {
    uint8_t data = (tms7041_portb_ & 0x80) ? 0x01 : 0x00;
    // PA.4: cassette input, high when idle.
    if (!tape_.loaded() || tape_.level()) data |= 0x10;
    return data;
}

void Exelv::tms7020_portb_w(uint8_t data) {
    // Every write of the BIOS byte writer is a half-period boundary, even
    // when bit 3 does not change (the first half after an idle low line).
    if (in_tape_write()) tape_.record_edge(main_cycles_, (data & 0x08) != 0);
    tms7020_portb_ = data;
    // With no I/O CPU, acknowledge "byte read" (PB.1) on PA.0 like the 7041.
    if (!sub_present_) tms7041_portb_ = uint8_t((tms7041_portb_ & 0x7f) | ((data & 0x02) ? 0x80 : 0));
    cass_bit_ = (data & 0x08) ? -1 : 1;
}

uint8_t Exelv::tms7041_porta_r() {
    uint8_t data = 0;
    data |= speech_.intq() ? 0x08 : 0x00;    // A3: TMS5220 /INT (high = idle)
    data |= speech_.readyq() ? 0x80 : 0x00;  // A7: TMS5220 /READY (high = busy)
    data |= (tms7020_portb_ & 0x01) ? 0x04 : 0x00;
    data |= (tms7020_portb_ & 0x02) ? 0x10 : 0x00;
    return data;
}

void Exelv::tms7041_portb_w(uint8_t data) {
    speech_.strobe_ws_rs(data & 0x03);
    if ((tms7041_portb_ & 0x04) && !(data & 0x04)) {
        maincpu_.set_input_line(Tms7000::kInt1, IrqLine::Hold);
    }
    if (!(tms7041_portb_ & 0x40) && (data & 0x40)) {
        wx319_ = tms7041_portc_;
        // Function $01 (key/joystick 0) is followed by the key code; $04
        // there reports the release.
        if (last_sent_ == 0x01 && wx319_ != 0x04) last_key_ = wx319_;
        last_sent_ = wx319_;
    }
    tms7041_portb_ = data;
}

uint8_t Exelv::tms7041_portc_r() {
    if (!(tms7041_portb_ & 0x20)) return wx318_;
    return 0xff;
}

void Exelv::tms7041_portc_w(uint8_t data) { tms7041_portc_ = data; }

uint8_t Exelv::tms7041_portd_r() { return speech_.status(); }

void Exelv::tms7041_portd_w(uint8_t data) {
    speech_.set_data_latch(data);
    tms7041_portd_ = data;
}

bool Exelv::in_tape_read() const {
    // TRAP 14 tape code in the internal ROM (load, save and bit timing).
    const uint16_t pc = maincpu_.pc();
    if (model_ == Model::Exl100) return pc >= 0xfca1 && pc < 0xfea0;
    return pc >= 0xfac0 && pc < 0xfcdb;
}

bool Exelv::in_tape_write() const {
    const uint16_t pc = maincpu_.pc();
    if (model_ == Model::Exl100) return pc >= 0xfde5 && pc < 0xfe10;
    return pc >= 0xfc20 && pc < 0xfc4b;
}

void Exelv::flush_tape_recording() {
    std::vector<uint8_t> bytes = tape_.take_recording();
    if (bytes.empty()) return;
    const std::string path = tape_save_path_.empty() ? "exelvision-save.k7" : tape_save_path_;
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

void Exelv::fast_load_hook() {
    // BIOS TRAP 14 addresses: start of the leader/sync search, the first
    // instruction after the $70 sync byte, and the read-byte routine.
    const bool exl = model_ == Model::Exl100;
    const uint16_t sync_start = exl ? 0xfca8 : 0xfadc;
    const uint16_t after_sync = exl ? 0xfcfe : 0xfb32;
    const uint16_t read_byte = exl ? 0xfe82 : 0xfcbd;
    const uint16_t pc = maincpu_.pc();
    if (pc == sync_start) {
        if (tape_.seek_sync()) maincpu_.set_pc(after_sync);
    } else if (pc == read_byte) {
        const int value = tape_.next_byte();
        if (value < 0) return;  // end of tape: let the BIOS time out
        maincpu_.set_a(uint8_t(value));
        // RETS: the low byte of the return address is on top of the stack.
        const uint8_t sp = maincpu_.sp();
        maincpu_.set_pc(uint16_t((maincpu_.ram_at(uint8_t(sp - 1)) << 8) | maincpu_.ram_at(sp)));
        maincpu_.set_sp(uint8_t(sp - 2));
    }
}

void Exelv::on_main_cycles(int cycles) {
    main_cycles_ += uint64_t(cycles);
    if (tape_fast_ && tape_playing_ && tape_.loaded()) fast_load_hook();
    if (tape_playing_ && tape_.loaded() && in_tape_read()) tape_.advance(cycles);
    speech_.tick(cycles);
    const uint32_t cpu_clock = maincpu_.cpu_clock();
    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= cpu_clock) {
        audio_accumulator_ -= cpu_clock;
        int32_t sample = speech_.update();
        sample += cass_bit_ * 800;
        audio_.push_back(int16_t(std::clamp(sample, int32_t(-32768), int32_t(32767))));
    }
}

uint8_t Exelv::scan_key_channel() {
    // The EXL-100 modifiers (SHIFT, CTL, FCT) are pressed and released
    // before the key they modify; the IR keyboard sends one channel at a
    // time. A host chord such as Shift+3 is turned into that sequence: the
    // modifier's channel once, then the key.
    constexpr uint8_t kShift = 4 * 8 + 0, kCtl = 1 * 8 + 0, kFct = 1 * 8 + 7;
    uint8_t modifier = 0xff;
    uint8_t key = 0xff;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            const Key k = kMatrix[row][col];
            if (k == Key::Count) continue;
            bool down = inputs_.key(k);
            if (k == Key::LeftShift) down = down || inputs_.key(Key::RightShift);
            if (k == Key::LeftCtrl) down = down || inputs_.key(Key::RightCtrl);
            if (!down) continue;
            const uint8_t channel = uint8_t(row * 8 + col);
            if (channel == kShift || channel == kCtl || channel == kFct) {
                if (modifier == 0xff) modifier = channel;
            } else if (key == 0xff) {
                key = channel;
            }
        }
    }
    if (key == 0xff) {
        if (inputs_.player1.up) key = 1;
        else if (inputs_.player1.right) key = 2;
        else if (inputs_.player1.down) key = 3;
        else if (inputs_.player1.left) key = 4;
        else if (inputs_.player1.button1) key = 6;  // space
    }
    // The chord's key is sent once even if the host released it meanwhile.
    if (chord_phase_ == 2 && chord_key_ != 0xff) return chord_key_;
    if (key == 0xff || modifier == 0xff) {
        if (chord_phase_ == 1 && chord_key_ != 0xff) return chord_mod_;
        chord_phase_ = 0;
        return key != 0xff ? key : modifier;
    }
    if (chord_phase_ == 0) {
        chord_phase_ = 1;
        chord_mod_ = modifier;
        chord_key_ = key;
    }
    return chord_phase_ == 1 ? chord_mod_ : key;
}

void Exelv::tick_keyboard(int cpu_cycles) {
    if (!sub_present_) return;
    const uint32_t cpu_clock = subcpu_.cpu_clock();
    k_boot_cycles_ += cpu_cycles;
    if (!k_started_) {
        if (k_boot_cycles_ < int64_t(cpu_clock) * 2) return;
        k_started_ = true;
        k_timer_cycles_ = 0;
    }

    // Sub-CPU cycles left before the next IR edge; wait_us adds to it so
    // the edges keep their exact spacing across slices.
    k_timer_cycles_ -= cpu_cycles;
    if (k_timer_cycles_ > 0) return;

    auto assert_ir = [&](bool on) {
        subcpu_.set_input_line(Tms7000::kInt1, on ? IrqLine::Assert : IrqLine::Clear);
    };
    auto wait_us = [&](int us) {
        k_timer_cycles_ += int(int64_t(us) * cpu_clock / 1000000);
    };

    if (k_ch_byte_ < 2) {
        k_channels_[0] = scan_key_channel();
        if (k_channels_[0] != 0xff && k_ch_byte_ == 0) {
            if (k_channels_[1] == 0xff) k_channels_[1] = k_channels_[0];
            k_ch_bit_ = 0;
            k_bit_num_ = false;
            k_ch_byte_ = 1;
        }
    }
    if (k_ch_byte_ == 0) {
        wait_us(25000);
        return;
    }

    if (k_ch_bit_ == 0) {
        if (!k_bit_num_) {
            assert_ir(true);
            k_bit_num_ = true;
            wait_us(540);
        } else {
            assert_ir(false);
            k_bit_num_ = false;
            k_ch_bit_ = 1;
            wait_us(2840);
        }
        return;
    }
    if (k_ch_bit_ == 1) {
        if (!k_bit_num_) {
            assert_ir(true);
            k_bit_num_ = true;
            wait_us(540);
        } else {
            assert_ir(false);
            k_bit_num_ = false;
            k_ch_bit_ = 2;
            wait_us(590);
        }
        return;
    }
    if (k_ch_bit_ == 8) {
        // A chord's modifier is sent once and released before the key.
        if (k_ch_byte_ == 1 && chord_phase_ == 1) {
            chord_phase_ = 2;
            k_channels_[0] = 0xff;
        } else if (k_ch_byte_ == 1 && chord_phase_ == 2 && k_channels_[1] == chord_key_) {
            chord_key_ = 0xff;  // sent; from now on it repeats only while held
            k_channels_[0] = scan_key_channel();
        }
        assert_ir(false);
        k_ch_bit_ = 0;
        if (k_ch_byte_ == 1) {
            if (k_channels_[0] < 0xff)
                wait_us(90000);
            else {
                k_ch_byte_ = 2;
                wait_us(3000);
            }
        } else if (k_ch_byte_ == 2) {
            k_channels_[1] = 0xff;
            k_ch_byte_ = 0;
            wait_us(20000);
        }
        return;
    }
    if (!k_bit_num_) {
        k_bit_bit_ = (k_channels_[k_ch_byte_] >> (k_ch_bit_ - 2)) & 1;
        assert_ir(k_bit_bit_);
        k_bit_num_ = true;
        wait_us(590);
        return;
    }
    assert_ir(!k_bit_bit_);
    k_bit_num_ = false;
    k_ch_bit_++;
    wait_us(540);
}

void Exelv::run_frame() {
    const uint32_t cpu_clock = maincpu_.cpu_clock();
    const int cycles_per_line = int(double(cpu_clock) / (kFramesPerSecond * kScanlines) + 0.5);
    for (int line = 0; line < kScanlines; line++) {
        vdp_.interrupt();
        int remain = cycles_per_line;
        while (remain > 0) {
            const int slice = std::min(remain, 16);
            // Instructions overrun a slice; carry the excess so neither CPU
            // runs faster than real time (the IR decoder times bits with
            // timer 1 and must stay in step with the keyboard timing).
            if (slice > main_debt_) main_debt_ += maincpu_.run(slice - main_debt_);
            main_debt_ -= slice;
            if (sub_present_) {
                if (slice > sub_debt_) sub_debt_ += subcpu_.run(slice - sub_debt_);
                sub_debt_ -= slice;
            }
            tick_keyboard(slice);
            remain -= slice;
        }
        // Without an I/O CPU ROM, post the "I/O CPU initialized" byte ($08)
        // the main CPU waits for. The INT1 handler reads it with PA.0 low,
        // raises PB.1 and waits for PA.0 to follow (tms7020_portb_w).
        if (!sub_present_ && !hle_io_sent_) {
            hle_io_delay_ -= cycles_per_line;
            if (hle_io_delay_ <= 0) {
                wx319_ = 0x08;
                tms7041_portb_ &= uint8_t(~0x80);
                maincpu_.set_input_line(Tms7000::kInt1, IrqLine::Hold);
                hle_io_sent_ = true;
            }
        }
    }
    // Write a SAVE out once the BIOS has left the tape routine for a second.
    if (tape_.recording()) {
        if (in_tape_read()) {
            tape_idle_frames_ = 0;
        } else if (++tape_idle_frames_ >= int(kFramesPerSecond)) {
            flush_tape_recording();
            tape_idle_frames_ = 0;
        }
    }
}

Exelv::~Exelv() { flush_tape_recording(); }

void Exelv::set_inputs(const MachineInputs& inputs) { inputs_ = inputs; }

void Exelv::set_dip_switch(int, uint8_t) {}

void Exelv::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
