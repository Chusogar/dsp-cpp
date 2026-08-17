#include "drivers/genesis.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

bool ends_with_ci(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    });
}

std::vector<uint8_t> read_file_bytes(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "cannot open " + path;
        return {};
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
}

bool looks_like_genesis(const std::vector<uint8_t>& data) {
    if (data.size() < 0x110) return false;
    const char* at = reinterpret_cast<const char*>(data.data() + 0x100);
    return std::memcmp(at, "SEGA", 4) == 0 || std::memcmp(at, " SEGA", 5) == 0;
}

void deinterleave_smd(std::vector<uint8_t>& data) {
    if (data.size() < 0x4000) return;
    std::vector<uint8_t> out(data.size());
    for (size_t block = 0; block + 0x4000 <= data.size(); block += 0x4000) {
        for (int i = 0; i < 0x2000; i++) {
            out[block + size_t(i) * 2] = data[block + 0x2000 + size_t(i)];
            out[block + size_t(i) * 2 + 1] = data[block + size_t(i)];
        }
    }
    data.swap(out);
}

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

uint32_t next_pow2(uint32_t value) {
    uint32_t mask = 1;
    while (mask < value) mask <<= 1;
    return mask;
}

uint8_t pad_bits(const InputState& pad, bool th) {
    // Active-low 3-button protocol.
    // TH=1: CBRLDU    TH=0: SA00DU
    // button1 = A, button2 = B, button3 = C.
    uint8_t value = 0x3f;
    if (pad.up) value &= ~0x01;
    if (pad.down) value &= ~0x02;
    if (th) {
        if (pad.left) value &= ~0x04;
        if (pad.right) value &= ~0x08;
        if (pad.button2) value &= ~0x10;  // B
        if (pad.button3) value &= ~0x20;  // C
        // Space / Ctrl also tap B so a single-button game stays playable.
        if (pad.button1) value &= ~0x10;
    } else {
        value &= ~0x0c;
        if (pad.button1) value &= ~0x10;  // A
        if (pad.start) value &= ~0x20;    // Start
    }
    return value;
}

}  // namespace

Genesis::Genesis(Region region)
    : region_(region),
      pal_(region == Region::Europe),
      m68k_(region == Region::Europe ? kM68kClockPal : kM68kClockNtsc),
      z80_(region == Region::Europe ? kZ80ClockPal : kZ80ClockNtsc),
      vdp_(region == Region::Europe),
      ym_(region == Region::Europe ? kM68kClockPal : kM68kClockNtsc, 1.6f) {
    framebuffer_.assign(size_t(kScreenWidth) * Sega3155313::kMaxHeight, 0xff000000u);
    for (int i = 0; i < 8; i++) rom_banks_[size_t(i)] = uint32_t(i) << 19;

    m68k_.set_memory_handlers([this](uint32_t a) { return read_word(a); },
                              [this](uint32_t a, uint16_t v) { write_word(a, v); });
    m68k_.set_byte_handlers([this](uint32_t a) { return read_byte(a); },
                            [this](uint32_t a, uint8_t v) { write_byte(a, v); });
    m68k_.set_cycle_handler([this](int cycles) { on_m68k_cycles(cycles); });

    z80_.set_memory_handlers([this](uint16_t a) { return z80_read(a); },
                             [this](uint16_t a, uint8_t v) { z80_write(a, v); });
    z80_.set_io_handlers([](uint16_t) { return uint8_t(0xff); },
                         [](uint16_t, uint8_t) {});

    vdp_.set_irq_handlers(
        [this](bool assert) { m68k_.set_irq(4, assert ? IrqLine::Hold : IrqLine::Clear); },
        [this](bool assert) { m68k_.set_irq(6, assert ? IrqLine::Hold : IrqLine::Clear); });
    vdp_.set_z80_irq_handler(
        [this](bool assert) { z80_.set_irq(assert ? IrqLine::Hold : IrqLine::Clear); });
    vdp_.set_dma_reader([this](uint32_t address) { return read_word(address); });

    set_dip_switch(0, uint8_t(region_));
}

bool Genesis::init(const std::string& rom_path, std::string* error) {
    return load_media(rom_path, error);
}

bool Genesis::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (ends_with_ci(path, ".zip")) {
        RomLoader loader;
        if (!loader.open(path, error)) return false;
        if (!loader.load_first_file(data, error)) return false;
        for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);
    } else {
        data = read_file_bytes(path, error);
        if (data.empty()) {
            // Directory or zip-without-extension: try RomLoader.
            RomLoader loader;
            std::string loader_error;
            if (loader.open(path, &loader_error) && loader.load_first_file(data, &loader_error)) {
                for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);
            } else {
                if (error && error->empty()) *error = loader_error;
                return false;
            }
        }
    }
    return load_cartridge(std::move(data), error);
}

bool Genesis::load_rom(std::vector<uint8_t> data, std::string* error) {
    return load_cartridge(std::move(data), error);
}

bool Genesis::load_cartridge(std::vector<uint8_t> data, std::string* error) {
    if (data.size() < 0x200) {
        if (error) *error = "cartridge image is too small";
        return false;
    }
    // Copier header (SMD/MD): 512 bytes, payload a multiple of 16 KiB.
    if ((data.size() % 16384) == 512) {
        data.erase(data.begin(), data.begin() + 512);
    }
    if (!looks_like_genesis(data)) deinterleave_smd(data);
    if (data.size() > size_t(kMaxRom)) data.resize(size_t(kMaxRom));
    rom_ = std::move(data);
    rom_mask_ = next_pow2(uint32_t(std::max<size_t>(rom_.size(), 2))) - 1;
    ssf2_mapper_ = rom_.size() > 0x400000;
    for (int i = 0; i < 8; i++) rom_banks_[size_t(i)] = uint32_t(i) << 19;
    parse_header();
    reset();
    return true;
}

void Genesis::parse_header() {
    sram_present_ = false;
    sram_start_ = 0x200000;
    sram_end_ = 0x20ffff;
    if (rom_.size() > 0x1b8 && rom_[0x1b0] == 'R' && rom_[0x1b1] == 'A') {
        sram_start_ = be32(&rom_[0x1b4]) & 0xffffff;
        sram_end_ = be32(&rom_[0x1b8]) & 0xffffff;
        if (sram_end_ < sram_start_) sram_end_ = sram_start_;
        sram_present_ = true;
    } else if (rom_.size() <= 0x200000) {
        // Plenty of games keep 8-64 KiB of backup RAM at $200000 without a
        // complete header; map a 64 KiB window so saves and high scores work.
        sram_present_ = true;
    }
    sram_.fill(0xff);
    sram_enabled_ = sram_present_;
}

void Genesis::reset() {
    ram_.fill(0);
    z80_ram_.fill(0);
    io_data_.fill(0);
    io_ctrl_.fill(0);
    io_data_[0] = version_reg_;
    io_data_[1] = 0x7f;
    io_data_[2] = 0x7f;
    io_data_[3] = 0x40;
    io_ctrl_[1] = 0x00;
    io_ctrl_[2] = 0x00;
    z80_has_bus_ = true;
    z80_is_reset_ = true;
    z80_bank_ = 0;
    z80_bank_shift_ = 0;
    audio_.clear();
    audio_accumulator_ = 0;
    cycles_on_line_ = 0;
    ym_.reset();
    vdp_.reset();
    z80_.reset();
    m68k_.reset();
    check_z80_bus_reset();
}

void Genesis::check_z80_bus_reset() {
    if (z80_is_reset_) {
        z80_.reset();
        ym_.reset();
        z80_.halted = true;
        return;
    }
    z80_.halted = !z80_has_bus_;
}

void Genesis::set_dip_switch(int bank, uint8_t value) {
    if (bank != 0) return;
    switch (value) {
        case 0:
            region_ = Region::Japan;
            pal_ = false;
            version_reg_ = 0x80;  // domestic NTSC, no TMSS
            break;
        case 2:
            region_ = Region::Europe;
            pal_ = true;
            version_reg_ = 0xc1;  // overseas PAL, no TMSS
            break;
        default:
            region_ = Region::Usa;
            pal_ = false;
            version_reg_ = 0xa1;  // overseas NTSC, no TMSS
            break;
    }
    io_data_[0] = version_reg_;
}

void Genesis::set_inputs(const MachineInputs& inputs) { pads_ = inputs; }

uint8_t Genesis::read_pad(int port) const {
    const InputState& pad = (port == 1) ? pads_.player1 : pads_.player2;
    const uint8_t ctrl = io_ctrl_[size_t(port)];
    const uint8_t data = io_data_[size_t(port)];
    const bool th = (ctrl & 0x40) != 0 ? (data & 0x40) != 0 : true;
    const uint8_t pad_val = uint8_t(0x40 | pad_bits(pad, th));
    return uint8_t((data & ctrl) | (pad_val & ~ctrl) | 0x80);
}

uint8_t Genesis::read_io(uint32_t address) {
    const int index = int((address >> 1) & 0x0f);
    switch (index) {
        case 0:
            return version_reg_;
        case 1:
        case 2:
            return read_pad(index);
        case 3:
            return uint8_t(io_data_[3] | 0x7f);
        default:
            return io_data_[size_t(index)];
    }
}

void Genesis::write_io(uint32_t address, uint8_t value) {
    const int index = int((address >> 1) & 0x0f);
    if (index == 0) return;
    if (index <= 3) io_data_[size_t(index)] = value;
    else if (index <= 6) io_ctrl_[size_t(index - 3)] = value;
    else io_data_[size_t(index)] = value;
}

uint8_t Genesis::cart_read(uint32_t address) const {
    address &= 0xffffff;
    if (sram_present_ && sram_enabled_ && address >= sram_start_ && address <= sram_end_) {
        return sram_[(address - sram_start_) & 0xffff];
    }
    if (rom_.empty()) return 0xff;
    uint32_t offset = address;
    if (ssf2_mapper_) {
        const int slot = int(address >> 19) & 7;
        offset = rom_banks_[size_t(slot)] + (address & 0x7ffff);
    } else if (address >= rom_.size()) {
        offset = address & rom_mask_;
        if (offset >= rom_.size()) return 0xff;
    }
    if (offset >= rom_.size()) return 0xff;
    return rom_[offset];
}

void Genesis::cart_write(uint32_t address, uint8_t value) {
    address &= 0xffffff;
    if (address >= 0xa13000 && address <= 0xa130ff) {
        if ((address & 0xff) == 0xf1) {
            sram_enabled_ = (value & 0x01) != 0;
            return;
        }
        if (ssf2_mapper_ && (address & 1) != 0 && address >= 0xa130f3) {
            const int slot = int((address - 0xa130f3) >> 1) + 1;
            if (slot >= 1 && slot < 8) {
                rom_banks_[size_t(slot)] = uint32_t(value & 0x3f) << 19;
            }
        }
        return;
    }
    if (sram_present_ && sram_enabled_ && address >= sram_start_ && address <= sram_end_) {
        sram_[(address - sram_start_) & 0xffff] = value;
    }
}

uint8_t Genesis::z80_read(uint16_t address) {
    if (address < 0x4000) return z80_ram_[address & 0x1fff];
    if (address < 0x6000) return ym_.read(address & 3);
    if (address < 0x6100) return 0xff;
    if (address < 0x7f00) return 0xff;
    if (address < 0x7f20) return 0xff;
    if (address < 0x8000) return 0xff;
    const uint32_t m68k_addr = (uint32_t(z80_bank_) << 15) | uint32_t(address & 0x7fff);
    return read_byte(m68k_addr);
}

void Genesis::z80_write(uint16_t address, uint8_t value) {
    if (address < 0x4000) {
        z80_ram_[address & 0x1fff] = value;
        return;
    }
    if (address < 0x6000) {
        ym_.write(address & 3, value);
        return;
    }
    if (address < 0x6100) {
        z80_bank_ = uint16_t(((z80_bank_ >> 1) | (uint16_t(value & 1) << 8)) & 0x1ff);
        return;
    }
    if (address >= 0x7f00 && address < 0x7f20) {
        vdp_.write_byte(uint8_t(address), value);
        return;
    }
    if (address >= 0x8000) {
        const uint32_t m68k_addr = (uint32_t(z80_bank_) << 15) | uint32_t(address & 0x7fff);
        write_byte(m68k_addr, value);
    }
}

uint16_t Genesis::read_word(uint32_t address) {
    address &= 0xfffffe;
    if (address <= 0x3fffff) {
        return uint16_t((uint16_t(cart_read(address)) << 8) | cart_read(address + 1));
    }
    if (address >= 0xa00000 && address <= 0xa0ffff) {
        if (!z80_has_bus_ && !z80_is_reset_) {
            const uint8_t hi = z80_read(uint16_t(address & 0x7fff));
            const uint8_t lo = z80_read(uint16_t((address + 1) & 0x7fff));
            return uint16_t((uint16_t(hi) << 8) | lo);
        }
        return 0xffff;
    }
    if (address >= 0xa10000 && address <= 0xa1001f) {
        const uint8_t value = read_io(address);
        return uint16_t((uint16_t(value) << 8) | value);
    }
    if (address == 0xa11100) {
        const uint16_t busy = (z80_has_bus_ || z80_is_reset_) ? 0x0100 : 0x0000;
        return uint16_t(0xfeff | busy);
    }
    if (address >= 0xc00000 && address <= 0xc0001f) {
        return vdp_.read(uint8_t(address));
    }
    if (address >= 0xe00000) {
        const uint32_t offset = address & 0xfffe;
        return uint16_t((uint16_t(ram_[offset]) << 8) | ram_[offset + 1]);
    }
    if (address >= 0xa14000 && address <= 0xa14fff) return 0xffff;
    return 0xffff;
}

void Genesis::write_word(uint32_t address, uint16_t value) {
    address &= 0xfffffe;
    if (address <= 0x3fffff) {
        cart_write(address, uint8_t(value >> 8));
        cart_write(address + 1, uint8_t(value));
        return;
    }
    if (address >= 0xa00000 && address <= 0xa0ffff) {
        if (!z80_has_bus_ && !z80_is_reset_) {
            z80_write(uint16_t(address & 0x7fff), uint8_t(value >> 8));
            z80_write(uint16_t((address + 1) & 0x7fff), uint8_t(value));
        }
        return;
    }
    if (address >= 0xa10000 && address <= 0xa1001f) {
        write_io(address, uint8_t(value >> 8));
        return;
    }
    if (address == 0xa11100) {
        z80_has_bus_ = (value & 0x0100) == 0;
        check_z80_bus_reset();
        return;
    }
    if (address == 0xa11200) {
        z80_is_reset_ = (value & 0x0100) == 0;
        check_z80_bus_reset();
        return;
    }
    if (address >= 0xa13000 && address <= 0xa130ff) {
        cart_write(address, uint8_t(value >> 8));
        cart_write(address + 1, uint8_t(value));
        return;
    }
    if (address >= 0xa14000 && address <= 0xa14fff) return;  // TMSS lock
    if (address >= 0xc00000 && address <= 0xc0001f) {
        vdp_.write(uint8_t(address), value);
        return;
    }
    if (address >= 0xe00000) {
        const uint32_t offset = address & 0xfffe;
        ram_[offset] = uint8_t(value >> 8);
        ram_[offset + 1] = uint8_t(value);
    }
}

uint8_t Genesis::read_byte(uint32_t address) {
    address &= 0xffffff;
    if (address <= 0x3fffff) return cart_read(address);
    if (address >= 0xa00000 && address <= 0xa0ffff) {
        if (!z80_has_bus_ && !z80_is_reset_) return z80_read(uint16_t(address & 0x7fff));
        return 0xff;
    }
    if (address >= 0xa10000 && address <= 0xa1001f) return read_io(address);
    if ((address & 0xfffffe) == 0xa11100) {
        return uint8_t(((z80_has_bus_ || z80_is_reset_) ? 0x01 : 0x00));
    }
    if (address >= 0xc00000 && address <= 0xc0001f) {
        const uint16_t value = vdp_.read(uint8_t(address & 0x1e));
        return (address & 1) ? uint8_t(value) : uint8_t(value >> 8);
    }
    if (address >= 0xe00000) return ram_[address & 0xffff];
    return 0xff;
}

void Genesis::write_byte(uint32_t address, uint8_t value) {
    address &= 0xffffff;
    if (address <= 0x3fffff) {
        cart_write(address, value);
        return;
    }
    if (address >= 0xa00000 && address <= 0xa0ffff) {
        if (!z80_has_bus_ && !z80_is_reset_) z80_write(uint16_t(address & 0x7fff), value);
        return;
    }
    if (address >= 0xa10000 && address <= 0xa1001f) {
        write_io(address, value);
        return;
    }
    if ((address & 0xfffffe) == 0xa11100) {
        if ((address & 1) == 0) {
            z80_has_bus_ = (value & 0x01) == 0;
            check_z80_bus_reset();
        }
        return;
    }
    if ((address & 0xfffffe) == 0xa11200) {
        if ((address & 1) == 0) {
            z80_is_reset_ = (value & 0x01) == 0;
            check_z80_bus_reset();
        }
        return;
    }
    if (address >= 0xa13000 && address <= 0xa130ff) {
        cart_write(address, value);
        return;
    }
    if (address >= 0xc00000 && address <= 0xc0001f) {
        vdp_.write_byte(uint8_t(address), value);
        return;
    }
    if (address >= 0xe00000) ram_[address & 0xffff] = value;
}

void Genesis::on_m68k_cycles(int cycles) {
    cycles_on_line_ += cycles;
    vdp_.set_hpos_cycles(cycles_on_line_);
    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    const int64_t clock = int64_t(m68k_clock());
    while (audio_accumulator_ >= clock) {
        audio_accumulator_ -= clock;
        int32_t sample = ym_.update() + vdp_.psg().update() / 4;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        audio_.push_back(int16_t(sample));
    }
}

void Genesis::run_frame() {
    const int lines = vdp_.total_scanlines();
    const int m68k_per_line = std::max(1, int(double(m68k_clock()) / frames_per_second() / lines));
    const int z80_per_line = std::max(1, int(double(z80_clock()) / frames_per_second() / lines));
    const int height = vdp_.screen_height();
    if (int(framebuffer_.size()) < kScreenWidth * height) {
        framebuffer_.assign(size_t(kScreenWidth) * size_t(height), 0xff000000u);
    }

    for (int line = 0; line < lines; line++) {
        cycles_on_line_ = 0;
        vdp_.handle_scanline(line);
        if (line < height) {
            std::memcpy(&framebuffer_[size_t(line) * kScreenWidth], vdp_.line_buffer(),
                        size_t(kScreenWidth) * sizeof(uint32_t));
        }
        m68k_.run(m68k_per_line);
        if (!z80_is_reset_ && z80_has_bus_) z80_.run(z80_per_line);
    }
    vdp_.handle_eof();
}

void Genesis::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
