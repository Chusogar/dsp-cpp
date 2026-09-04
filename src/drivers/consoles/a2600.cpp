#include "drivers/consoles/a2600.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

namespace dsp {
namespace {

std::string lower_copy(std::string value) {
    for (char& ch : value) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

bool has_cart_extension(const std::string& name) {
    const std::string lower = lower_copy(name);
    return lower.size() >= 4 &&
           (lower.compare(lower.size() - 4, 4, ".bin") == 0 ||
            lower.compare(lower.size() - 4, 4, ".a26") == 0 ||
            lower.compare(lower.size() - 4, 4, ".rom") == 0 ||
            lower.compare(lower.size() - 4, 4, ".zip") == 0);
}

bool name_says_superchip(const std::string& name) {
    const std::string lower = lower_copy(name);
    return lower.find("f8sc") != std::string::npos ||
           lower.find("f6sc") != std::string::npos ||
           lower.find("f4sc") != std::string::npos ||
           lower.find(".sc.") != std::string::npos;
}

}  // namespace

A2600::A2600() : cpu_(kCpuClock) {
    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cpu_cycles(cycles); });
    riot_.set_pa([this]() { return read_swcha(); });
    riot_.set_pb([this]() { return read_swchb(); });
    framebuffer_.fill(0xFF000000);
}

bool A2600::init(const std::string& rom_path, std::string* error) {
    if (rom_path.empty()) {
        if (error) *error = "Atari 2600 needs a cartridge (.bin/.a26)";
        return false;
    }
    return load_media(rom_path, error);
}

bool A2600::load_media(const std::string& path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<uint8_t> data;
    if (fs::is_directory(path, ec)) {
        if (!load_from_directory(path, data, error)) return false;
    } else if (!read_plain_or_zip(path, data, error)) {
        return false;
    }
    if (!install_cartridge(data, error)) return false;
    if (name_says_superchip(path)) superchip_ = true;
    reset();
    return true;
}

bool A2600::read_plain_or_zip(const std::string& path, std::vector<uint8_t>& data,
                              std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        std::ifstream probe(path, std::ios::binary);
        char magic[4] = {};
        probe.read(magic, 4);
        const bool is_zip = probe.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K' &&
                            magic[2] == 0x03 && magic[3] == 0x04;
        if (!is_zip) {
            probe.clear();
            probe.seekg(0, std::ios::end);
            const std::streamoff size = probe.tellg();
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
    data.reserve(kMaxCartridge);
    return loader.load_first_file(data, error);
}

bool A2600::load_from_directory(const std::string& directory, std::vector<uint8_t>& data,
                                std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        if (!has_cart_extension(name)) continue;
        if (read_plain_or_zip(entry.path().string(), data, error)) return true;
    }
    if (error) *error = "no Atari 2600 cartridge in " + directory;
    return false;
}

bool A2600::install_cartridge(const std::vector<uint8_t>& data, std::string* error) {
    if (data.empty() || data.size() > kMaxCartridge) {
        if (error) *error = "cartridge is empty or larger than 64 KiB";
        return false;
    }

    size_t size = data.size();
    superchip_ = false;
    if (size > 128 && (size % 1024 == 128)) {
        superchip_ = true;
        size -= 128;
    }

    // Pad odd dumps up to the next power-of-two bank window.
    size_t padded = 1;
    while (padded < size) padded <<= 1;
    if (padded < 2048) padded = 2048;

    rom_.assign(padded, 0xFF);
    std::memcpy(rom_.data(), data.data(), size);
    if (size < padded) {
        for (size_t i = size; i < padded; i++) rom_[i] = rom_[i % size];
    }
    rom_mask_ = padded - 1;

    bank_count_ = int(padded / 4096);
    if (bank_count_ < 1) bank_count_ = 1;
    if (padded <= 4096) {
        mapper_ = Mapper::Flat;
        bank_count_ = 1;
    } else if (padded <= 8192) {
        mapper_ = Mapper::F8;
        bank_count_ = 2;
    } else if (padded <= 16384) {
        mapper_ = Mapper::F6;
        bank_count_ = 4;
    } else if (padded <= 32768) {
        mapper_ = Mapper::F4;
        bank_count_ = 8;
    } else {
        mapper_ = Mapper::F0;
        bank_count_ = 16;
    }
    bank_ = bank_count_ - 1;
    superchip_ram_.fill(0);
    return true;
}

void A2600::reset() {
    tia_.reset();
    riot_.reset();
    cpu_.set_halted(false);
    cpu_.reset();
    visible_y_ = 0;
    prev_vsync_ = false;
    audio_.clear();
    framebuffer_.fill(0xFF000000);
    if (bank_count_ > 1) bank_ = bank_count_ - 1;
}

void A2600::on_cpu_cycles(int cycles) {
    tia_.add_cpu_cycles(cycles);
    riot_.tick(cycles);
}

uint8_t A2600::read_swcha() const {
    // Active low: P1 bits 0-3 right/left/down/up, P2 bits 4-7.
    uint8_t value = 0xff;
    auto apply = [&](const InputState& p, int shift) {
        if (p.right) value = uint8_t(value & ~(1u << shift));
        if (p.left) value = uint8_t(value & ~(1u << (shift + 1)));
        if (p.down) value = uint8_t(value & ~(1u << (shift + 2)));
        if (p.up) value = uint8_t(value & ~(1u << (shift + 3)));
    };
    apply(inputs_.player1, 0);
    apply(inputs_.player2, 4);
    return value;
}

uint8_t A2600::read_swchb() const {
    // D0 reset, D1 select (0 = pressed). D3 colour (1 = colour). D6/D7 difficulty.
    uint8_t value = 0x34;  // unused bits read high
    if (!inputs_.player1.start) value |= 0x01;
    if (!inputs_.player1.select) value |= 0x02;
    if (dips_ & 0x08) value |= 0x08;
    if (dips_ & 0x40) value |= 0x40;
    if (dips_ & 0x80) value |= 0x80;
    return value;
}

void A2600::set_inputs(const MachineInputs& inputs) {
    inputs_ = inputs;
    tia_.set_inpt4(inputs.player1.button1);
    tia_.set_inpt5(inputs.player2.button1);
}

void A2600::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dips_ = value;
}

void A2600::drain_audio(std::vector<int16_t>& out) {
    out.swap(audio_);
    audio_.clear();
}

void A2600::touch_hotspot(uint16_t offset) {
    int next = bank_;
    switch (mapper_) {
        case Mapper::F8:
            if (offset == 0xff8) next = 0;
            else if (offset == 0xff9) next = 1;
            break;
        case Mapper::F6:
            if (offset >= 0xff6 && offset <= 0xff9) next = int(offset - 0xff6);
            break;
        case Mapper::F4:
            if (offset >= 0xff4 && offset <= 0xffb) next = int(offset - 0xff4);
            break;
        case Mapper::F0:
            if (offset >= 0xff0 && offset <= 0xff7) next = int(offset - 0xff0);
            break;
        case Mapper::Flat:
            break;
    }
    if (next >= 0 && next < bank_count_) bank_ = next;
}

uint8_t A2600::read_cartridge(uint16_t offset) {
    offset &= 0x0fff;
    touch_hotspot(offset);
    if (superchip_) {
        if (offset < 0x80) return 0;
        if (offset < 0x100) return superchip_ram_[offset - 0x80];
    }
    if (mapper_ == Mapper::Flat) return rom_[offset & rom_mask_];
    const size_t base = size_t(bank_) * 0x1000;
    return rom_[(base + offset) & rom_mask_];
}

void A2600::write_cartridge(uint16_t offset, uint8_t value) {
    offset &= 0x0fff;
    touch_hotspot(offset);
    if (superchip_ && offset < 0x80) superchip_ram_[offset] = value;
}

uint8_t A2600::read_byte(uint16_t address) {
    address &= 0x1fff;
    if (address & 0x1000) return read_cartridge(address);
    if (address & 0x80) {
        if (address & 0x200) return riot_.io_read(uint8_t(address & 0x1f));
        return riot_.ram_read(uint8_t(address & 0x7f));
    }
    return tia_.read(uint8_t(address & 0x0f));
}

void A2600::write_byte(uint16_t address, uint8_t value) {
    address &= 0x1fff;
    if (address & 0x1000) {
        write_cartridge(address, value);
        return;
    }
    if (address & 0x80) {
        if (address & 0x200) riot_.io_write(uint8_t(address & 0x1f), value);
        else riot_.ram_write(uint8_t(address & 0x7f), value);
        return;
    }
    tia_.write(uint8_t(address & 0x3f), value);
    if (tia_.wsync()) cpu_.set_halted(true);
}

void A2600::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        tia_.begin_line();
        tia_.clear_wsync();
        cpu_.set_halted(false);
        cpu_.run(kCyclesPerLine);
        tia_.clock_audio();
        tia_.emit_audio(kCyclesPerLine, kCpuClock, audio_);

        const bool vsync = tia_.vsync();
        if (vsync && !prev_vsync_) visible_y_ = 0;
        prev_vsync_ = vsync;
        if (!tia_.blanked() && visible_y_ < kScreenHeight) {
            tia_.render_line(&framebuffer_[size_t(visible_y_) * kScreenWidth]);
            visible_y_++;
        }
    }
}

}  // namespace dsp
