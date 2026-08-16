#include "drivers/atari_lynx.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

bool read_plain_or_zip_file(const std::string& path, std::vector<uint8_t>& data, size_t max_size,
                            std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {};
        probe.read(magic, 4);
        bool is_zip = probe.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K' &&
                      magic[2] == 0x03 && magic[3] == 0x04;
        if (!is_zip) {
            probe.clear();
            probe.seekg(0, std::ios::beg);
            probe.seekg(0, std::ios::end);
            std::streamoff size = probe.tellg();
            probe.seekg(0, std::ios::beg);
            if (size <= 0) {
                if (error) *error = "cannot read " + path;
                return false;
            }
            data.resize(size_t(size));
            probe.read(reinterpret_cast<char*>(data.data()), size);
            return bool(probe);
        }
    }
    RomLoader loader;
    if (!loader.open(path, error)) return false;
    data.reserve(max_size);
    return loader.load_first_file(data, error);
}

bool has_lnx_header(const std::vector<uint8_t>& data) {
    if (data.size() < 64) return false;
    return std::memcmp(data.data(), "LYNX", 4) == 0;
}

uint16_t guess_granularity(size_t size) {
    if (size == 0x20000) return 0x200;
    if (size == 0x80000) return 0x800;
    if (size == 0x100000) return 0x800;
    return 0x400;
}

// Original 512-byte bootstrap, mapped at $FE00. Copies the first cart page
// into $0200 and jumps there. Not the copyrighted Atari Lynx ROM.
void assemble_boot_rom(std::array<uint8_t, 0x200>& rom) {
    rom.fill(0x00);
    const uint8_t code[] = {
        0xa2, 0xff,              // FE00 ldx #$ff
        0x9a,                    // FE02 txs
        0xd8,                    // FE03 cld
        0x78,                    // FE04 sei
        0xa9, 0x02,              // FE05 lda #$02
        0x8d, 0x8a, 0xfd,        // FE07 sta $fd8a  IODIR bit1 out
        0xa9, 0x00,              // FE0A lda #$00
        0x8d, 0x8b, 0xfd,        // FE0C sta $fd8b  IODAT
        0xa9, 0x02,              // FE0F lda #$02
        0x8d, 0x87, 0xfd,        // FE11 sta $fd87  cart power
        0xa2, 0x08,              // FE14 ldx #$08
        0xa9, 0x03,              // FE16 lda #$03  strobe
        0x8d, 0x87, 0xfd,        // FE18 sta $fd87
        0xa9, 0x02,              // FE1B lda #$02
        0x8d, 0x87, 0xfd,        // FE1D sta $fd87
        0xca,                    // FE20 dex
        0xd0, 0xf3,              // FE21 bne strobe
        0xa0, 0x00,              // FE23 ldy #$00
        0xad, 0xb2, 0xfc,        // FE25 lda $fcb2  RCART0
        0x99, 0x00, 0x02,        // FE28 sta $0200,y
        0xc8,                    // FE2B iny
        0xd0, 0xf7,              // FE2C bne copy
        0x4c, 0x00, 0x02,        // FE2E jmp $0200
        0x40,                    // FE31 rti
    };
    std::copy(std::begin(code), std::end(code), rom.begin());
    rom[0x1fa] = 0x31;  // NMI
    rom[0x1fb] = 0xfe;
    rom[0x1fc] = 0x00;  // RESET
    rom[0x1fd] = 0xfe;
    rom[0x1fe] = 0x31;  // IRQ
    rom[0x1ff] = 0xfe;
}

}  // namespace

AtariLynx::AtariLynx() : cpu_(kCpuClock) {
    assemble_boot_rom(boot_rom_);
    cpu_.set_cmos(true);
    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cpu_cycles(cycles); });
    suzy_.set_ram(ram_.data());
    suzy_.set_cart0([this]() { return cart_read(); });
    mikey_.set_irq_callback([this](bool asserted) {
        cpu_.set_irq(asserted ? IrqLine::Assert : IrqLine::Clear);
    });
    mikey_.set_wake_callback([this]() { cpu_.set_halted(false); });
    mikey_.set_sysctl_callback([this](uint8_t sysctl, uint8_t iodat) { cart_strobe(sysctl, iodat); });
}

bool AtariLynx::init(const std::string& rom_path, std::string*) {
    reset();
    if (!rom_path.empty()) {
        std::string cart_error;
        if (!load_media(rom_path, &cart_error)) warnings_.push_back(cart_error);
    }
    return true;
}

bool AtariLynx::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_plain_or_zip_file(path, data, kMaxCartridge, error)) return false;
    if (data.empty()) {
        if (error) *error = "empty Lynx cartridge";
        return false;
    }

    granularity_ = 0x400;
    audin_offset_ = 0;
    if (has_lnx_header(data)) {
        granularity_ = uint16_t(data[4] | (data[5] << 8));
        if (granularity_ != 256 && granularity_ != 512 && granularity_ != 1024 &&
            granularity_ != 2048) {
            granularity_ = 0x400;
        }
        const uint16_t bank1 = uint16_t(data[6] | (data[7] << 8));
        data.erase(data.begin(), data.begin() + 64);
        if (bank1 != 0) audin_offset_ = 256u * granularity_;
    } else if (data.size() >= 10 && std::memcmp(data.data() + 6, "BS93", 4) == 0) {
        // Home-brew BLL / Handy quickload: 80 08 dw start dw len "BS93"
        const uint16_t start = uint16_t(data[3] | (data[2] << 8));
        uint16_t length = uint16_t(data[5] | (data[4] << 8));
        if (length >= 10) length = uint16_t(length - 10);
        ram_.fill(0);
        suzy_.reset();
        mikey_.reset();
        mapctl_ = 0x0c;
        const size_t copy = std::min(size_t(length), data.size() - 10);
        for (size_t i = 0; i < copy; i++) ram_[(start + i) & 0xffff] = data[10 + i];
        ram_[0xfffc] = uint8_t(start);
        ram_[0xfffd] = uint8_t(start >> 8);
        cart_.clear();
        cpu_.reset();
        return true;
    } else {
        granularity_ = guess_granularity(data.size());
    }

    cart_ = std::move(data);
    reset();
    return true;
}

void AtariLynx::reset() {
    ram_.fill(0);
    suzy_.reset();
    mikey_.reset();
    mapctl_ = 0;
    cart_block_ = 0;
    cart_counter_ = 0;
    last_sysctl_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
    framebuffer_.fill(0xff000000);
    cpu_.set_halted(false);
    cpu_.reset();
}

uint8_t AtariLynx::cart_read() {
    if (cart_.empty()) {
        cart_counter_ = uint16_t((cart_counter_ + 1) & (granularity_ - 1));
        return 0xff;
    }
    uint32_t address = uint32_t(cart_block_) * granularity_ + cart_counter_;
    if (mikey_.iodat() & 0x10) address += audin_offset_;
    uint8_t value = 0xff;
    if (address < cart_.size()) value = cart_[address];
    cart_counter_ = uint16_t((cart_counter_ + 1) & (granularity_ - 1));
    return value;
}

void AtariLynx::cart_strobe(uint8_t sysctl, uint8_t iodat) {
    if (sysctl & 0x02) {
        if ((sysctl & 0x01) && (last_sysctl_ & 0x01) == 0) {
            cart_block_ = uint8_t(((cart_block_ << 1) & 0xfe) | ((iodat >> 1) & 0x01));
            cart_counter_ = 0;
        }
    } else {
        cart_block_ = 0;
        cart_counter_ = 0;
    }
    last_sysctl_ = sysctl;
}

uint8_t AtariLynx::read_byte(uint16_t address) {
    if (address == 0xfff9) return mapctl_;
    if (address == 0xfff8) return ram_[0xfff8];

    const bool rom_on = (mapctl_ & 0x04) == 0;
    const bool vectors_rom = (mapctl_ & 0x08) == 0;
    if (address >= 0xfe00) {
        if (address >= 0xfffa && vectors_rom && rom_on) {
            return boot_rom_[address - 0xfe00];
        }
        if (address <= 0xfff7 && rom_on) return boot_rom_[address - 0xfe00];
    }
    if (address >= 0xfc00 && address <= 0xfcff && (mapctl_ & 0x01) == 0) {
        return suzy_.read(uint8_t(address));
    }
    if (address >= 0xfd00 && address <= 0xfdff && (mapctl_ & 0x02) == 0) {
        return mikey_.read(uint8_t(address));
    }
    return ram_[address];
}

void AtariLynx::write_byte(uint16_t address, uint8_t value) {
    if (address == 0xfff9) {
        mapctl_ = value;
        return;
    }
    if (address >= 0xfc00 && address <= 0xfcff && (mapctl_ & 0x01) == 0) {
        suzy_.write(uint8_t(address), value);
        return;
    }
    if (address >= 0xfd00 && address <= 0xfdff && (mapctl_ & 0x02) == 0) {
        mikey_.write(uint8_t(address), value);
        if (address == 0xfd91 && suzy_.busy()) cpu_.set_halted(true);
        return;
    }
    if ((address & 0xfffe) == 0xfff8) {
        if (address == 0xfff8) ram_[0xfff8] = value;
        return;
    }
    ram_[address] = value;
}

void AtariLynx::on_cpu_cycles(int cycles) {
    mikey_.tick(cycles);
    suzy_.tick(cycles);
    if (cpu_.halted() && !suzy_.busy()) cpu_.set_halted(false);
    audio_accumulator_ += uint64_t(cycles) * uint32_t(LynxMikey::kSampleRate);
    while (audio_accumulator_ >= kCpuClock) {
        audio_accumulator_ -= kCpuClock;
        audio_.push_back(mikey_.mix_sample());
    }
}

void AtariLynx::present_frame() {
    if (!mikey_.video_dma()) {
        framebuffer_.fill(0xff000000);
        return;
    }
    uint16_t address = uint16_t(mikey_.display_pointer() & 0xfffc);
    if (mikey_.flip_screen()) {
        for (int y = 0; y < kScreenHeight; y++) {
            const int dest = (kScreenHeight - 1 - y) * kScreenWidth;
            uint16_t line = uint16_t(address + y * 80);
            for (int x = kScreenWidth - 2; x >= 0; line++, x -= 2) {
                const uint8_t byte = ram_[line];
                framebuffer_[size_t(dest + x + 1)] = mikey_.pal_argb(byte >> 4);
                framebuffer_[size_t(dest + x + 0)] = mikey_.pal_argb(byte & 0x0f);
            }
        }
    } else {
        for (int y = 0; y < kScreenHeight; y++) {
            const int dest = y * kScreenWidth;
            for (int x = 0; x < kScreenWidth; address++, x += 2) {
                const uint8_t byte = ram_[address];
                framebuffer_[size_t(dest + x + 0)] = mikey_.pal_argb(byte >> 4);
                framebuffer_[size_t(dest + x + 1)] = mikey_.pal_argb(byte & 0x0f);
            }
        }
    }
}

void AtariLynx::run_frame() {
    for (int line = 0; line < kScanlines; line++) cpu_.run(kCyclesPerLine);
    present_frame();
}

void AtariLynx::set_inputs(const MachineInputs& inputs) {
    uint8_t joy = 0;
    if (inputs.player1.button1) joy |= 0x01;  // A
    if (inputs.player1.button2) joy |= 0x02;  // B
    if (inputs.player1.start) joy |= 0x04;    // Option 2
    if (inputs.player1.button3) joy |= 0x08;  // Option 1
    if (inputs.player1.right) joy |= 0x10;
    if (inputs.player1.left) joy |= 0x20;
    if (inputs.player1.down) joy |= 0x40;
    if (inputs.player1.up) joy |= 0x80;
    suzy_.set_joystick(joy);
    suzy_.set_switches(inputs.coin1 ? 0x01 : 0x00);  // Pause
}

void AtariLynx::set_dip_switch(int, uint8_t) {}

void AtariLynx::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
