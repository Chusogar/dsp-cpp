#include "drivers/exelv.h"
#include <cstdio>

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
    {"exeltel_7040.rom", 0x1000, 0x0000, 0x2792f02f},
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

const Key kMatrix[8][8] = {
    {Key::Z, Key::Up, Key::Right, Key::Down, Key::Left, Key::E, Key::Space, Key::Minus},
    {Key::LeftCtrl, Key::CapsLock, Key::X, Key::Slash, Key::Escape, Key::R, Key::Comma, Key::RightCtrl},
    {Key::Tab, Key::Count, Key::V, Key::Quote, Key::Num1, Key::Num6, Key::Num8, Key::Num0},
    {Key::A, Key::Backspace, Key::C, Key::Period, Key::Num2, Key::Num3, Key::Num9, Key::Minus},
    {Key::LeftShift, Key::O, Key::H, Key::Count, Key::T, Key::M, Key::N, Key::G},
    {Key::S, Key::U, Key::K, Key::Count, Key::Y, Key::Count, Key::B, Key::D},
    {Key::W, Key::P, Key::J, Key::Count, Key::Num4, Key::Num7, Key::Count, Key::F},
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
        // MAME still ships this 7042 image as BAD_DUMP (CRC a0163507). The first
        // 1 KiB matches the EXL-100 7041, then the rest diverges; running it
        // posts mailbox $04 and the TMS7040 hangs at $FA29. Skip it and HLE the
        // mailbox $08 handshake instead.
        if (sub_rom.size() >= 0x800 && crc32_of(sub_rom.data(), sub_rom.size()) == 0xa0163507) {
            warnings_.push_back(
                "exeltel_7042.bin is MAME's known BAD_DUMP; mailbox $08 is HLE'd");
            sub_rom.clear();
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
    hle_io_sent_ = false;
    hle_io_lowered_ = false;
    hle_io_delay_ = int(maincpu_.cpu_clock() / 5);  // ~0.2 s
    k_channels_[0] = 0xff;
    k_channels_[1] = 0xff;
    k_channels_[2] = 0x3e;
    k_ch_byte_ = 0;
    k_ch_bit_ = 0;
    k_bit_bit_ = false;
    k_bit_num_ = false;
    k_timer_us_ = 0;
    k_started_ = false;
    k_boot_cycles_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
    cass_bit_ = 1;
    vdp_.reset();
    speech_.reset();
    speech_.strobe_ws_rs(0x03);
    speech_irq_ = true;
    maincpu_.reset();
    if (sub_present_) {
        subcpu_.reset();
        subcpu_.set_input_line(Tms7000::kInt1, IrqLine::Clear);
        subcpu_.set_input_line(Tms7000::kInt3, IrqLine::Clear);
    }
}

uint8_t Exelv::cart_r(uint16_t offset) const {
    if (model_ == Model::Exeltel && !system_rom_.empty()) {
        const size_t addr = size_t(offset) + 0x200;
        if (addr < system_rom_.size()) return system_rom_[addr];
        return 0xff;
    }
    if (cart_.empty()) return 0xff;
    if (cart_.size() == 0x7e00) {
        return offset < cart_.size() ? cart_[offset] : 0xff;
    }
    const size_t addr = size_t(offset) + 0x200;
    return addr < cart_.size() ? cart_[addr] : 0xff;
}

uint8_t Exelv::read_main(uint16_t address) {
    if (address == 0x0124) return vdp_.vram_r();
    if (address == 0x0125) return vdp_.reg_r();
    if (address == 0x0128) return vdp_.initptr_r();
    if (address == 0x0130) return mailbox_wx319_r();
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
    if (address >= 0xc000 && address <= 0xc7ff) ram_[address - 0xc000] = value;
}

uint8_t Exelv::read_sub(uint16_t) { return 0xff; }
void Exelv::write_sub(uint16_t, uint8_t) {}

uint8_t Exelv::mailbox_wx319_r() { return wx319_; }
void Exelv::mailbox_wx318_w(uint8_t data) { wx318_ = data; }

uint8_t Exelv::tms7020_porta_r() {
    uint8_t data = (tms7041_portb_ & 0x80) ? 0x01 : 0x00;
    data |= 0x10;  // cassette idle high
    return data;
}

void Exelv::tms7020_portb_w(uint8_t data) {
    tms7020_portb_ = data;
    cass_bit_ = (data & 0x08) ? -1 : 1;
}

uint8_t Exelv::tms7041_porta_r() {
    uint8_t data = 0;
    data |= speech_irq_ ? 0x00 : 0x08;
    // PA.7 = /READY inverted sense used by 7041 (spin while bit7 set = not ready).
    // readyq() true = busy → expose as 1; ready → 0 so the BTJO loop exits.
    data |= speech_.readyq() ? 0x80 : 0x00;
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

void Exelv::on_main_cycles(int cycles) {
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

uint8_t Exelv::scan_key_channel() const {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            const Key key = kMatrix[row][col];
            if (key == Key::Count) continue;
            if (inputs_.key(key)) return uint8_t(row * 8 + col);
        }
    }
    if (inputs_.player1.up) return 1;
    if (inputs_.player1.right) return 2;
    if (inputs_.player1.down) return 3;
    if (inputs_.player1.left) return 4;
    if (inputs_.player1.button1) return 6;  // space
    return 0xff;
}

void Exelv::tick_keyboard(int cpu_cycles) {
    if (!sub_present_) return;
    const uint32_t cpu_clock = subcpu_.cpu_clock();
    k_boot_cycles_ += cpu_cycles;
    if (!k_started_) {
        if (k_boot_cycles_ < int64_t(cpu_clock) * 2) return;
        k_started_ = true;
        k_timer_us_ = 0;
    }

    k_timer_us_ -= int(int64_t(cpu_cycles) * 1000000 / cpu_clock);
    if (k_timer_us_ > 0) return;
    k_timer_us_ = 0;

    auto assert_ir = [&](bool on) {
        subcpu_.set_input_line(Tms7000::kInt1, on ? IrqLine::Assert : IrqLine::Clear);
    };
    auto wait_us = [&](int us) { k_timer_us_ = us; };

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
            maincpu_.run(slice);
            if (sub_present_) subcpu_.run(slice);
            remain -= slice;
        }
        tick_keyboard(cycles_per_line);
        // EXL-100 waits for mailbox $08. EXELTEL's 7040 also CMP #$08 on the
        // first handshake; the bad 7042 posts $04 and that path hangs at $FA29,
        // so only a missing/disabled I/O CPU uses this HLE.
        if (!sub_present_) {
            if (!hle_io_sent_) {
                hle_io_delay_ -= cycles_per_line;
                if (hle_io_delay_ <= 0) {
                    wx319_ = 0x08;  // I/O CPU initialized
                    tms7041_portb_ |= 0x80;  // main PA.0 handshake high
                    maincpu_.set_input_line(Tms7000::kInt1, IrqLine::Hold);
                    hle_io_sent_ = true;
                    hle_io_delay_ = cycles_per_line * kScanlines * 5;  // hold PA.0 ~0.1 s
                }
            } else if (!hle_io_lowered_) {
                hle_io_delay_ -= cycles_per_line;
                if (hle_io_delay_ <= 0) {
                    // TMS7040 INT1 handler then BTJO %$01,P4 waiting for PA.0 low.
                    tms7041_portb_ &= uint8_t(~0x80);
                    hle_io_lowered_ = true;
                }
            }
        }
    }
}

void Exelv::set_inputs(const MachineInputs& inputs) { inputs_ = inputs; }

void Exelv::set_dip_switch(int, uint8_t) {}

void Exelv::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

}  // namespace dsp
