#include "drivers/consoles/pcengine.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "core/rom_loader.h"

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
        const bool is_zip = probe.gcount() == 4 && magic[0] == 'P' && magic[1] == 'K' &&
                            magic[2] == 0x03 && magic[3] == 0x04;
        if (!is_zip) {
            probe.clear();
            probe.seekg(0, std::ios::end);
            const std::streamoff size = probe.tellg();
            probe.seekg(0, std::ios::beg);
            if (size <= 0) return false;
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

// A few dumps are bit reversed inside every byte; they always start with the
// mirrored version of the standard "boot" pattern.
bool needs_bit_swap(const std::vector<uint8_t>& data) {
    return data.size() >= 4 && data[0] == 0xaa && data[1] == 0xbb && data[2] == 0x02;
}

uint8_t bit_swap(uint8_t value) {
    uint8_t out = 0;
    for (int bit = 0; bit < 8; bit++) {
        if ((value & (1 << bit)) != 0) out = uint8_t(out | (1 << (7 - bit)));
    }
    return out;
}

}  // namespace

PcEngine::PcEngine() {
    cpu_.set_memory_handlers([this](uint32_t address) { return read_byte(address); },
                            [this](uint32_t address, uint8_t value) { write_byte(address, value); });
    cpu_.set_cycle_handler([this](int cycles) { on_cpu_cycles(cycles); });
    vdc_.set_irq_handler([this](bool state) {
        cpu_.set_irq_line(0, state ? IrqLine::Assert : IrqLine::Clear);
    });
}

bool PcEngine::init(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!rom_path.empty() && fs::is_regular_file(rom_path, ec)) {
        std::string cart_error;
        if (!load_media(rom_path, &cart_error)) {
            if (error != nullptr) *error = cart_error;
            return false;
        }
        return true;
    }
    if (!rom_path.empty()) {
        std::string cart_error;
        if (load_media(rom_path, &cart_error)) return true;
        warnings_.push_back(cart_error);
    }
    reset();
    return true;
}

bool PcEngine::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_plain_or_zip_file(path, data, kMaxCartridge + 0x200, error)) {
        if (error != nullptr && error->empty()) *error = "cannot read " + path;
        return false;
    }
    return load_hucard(data, error);
}

bool PcEngine::load_hucard(const std::vector<uint8_t>& data, std::string* error) {
    std::vector<uint8_t> rom = data;
    // Some dumps carry a 512 byte copier header in front of the image.
    if ((rom.size() % 0x2000) == 0x200) rom.erase(rom.begin(), rom.begin() + 0x200);
    if (rom.size() < 0x2000) {
        if (error != nullptr) *error = "HuCard image too small";
        return false;
    }
    if (rom.size() > size_t(kMaxCartridge)) rom.resize(size_t(kMaxCartridge));
    if (needs_bit_swap(rom)) {
        for (uint8_t& value : rom) value = bit_swap(value);
    }
    rom_ = std::move(rom);
    reset();
    return true;
}

void PcEngine::reset() {
    ram_.fill(0);
    cpu_.mpr.fill(0);
    sf2_bank_ = 0;
    pad_select_ = false;
    pad_clear_ = false;
    pad_ = 0xff;
    audio_accumulator_ = 0;
    audio_.clear();
    framebuffer_.fill(0xff000000u);
    vdc_.reset();
    vce_.reset();
    psg_.reset();
    cpu_.reset();
}

void PcEngine::set_dip_switch(int bank, uint8_t value) {
    (void)bank;
    (void)value;
}

void PcEngine::set_inputs(const MachineInputs& inputs) {
    const InputState& pad = inputs.player1;
    uint8_t value = 0;
    // Active low: bits 0-3 are the buttons, bits 4-7 the directions.
    if (!pad.button1) value |= 0x01;  // I
    if (!pad.button2) value |= 0x02;  // II
    if (!pad.button3) value |= 0x04;  // SELECT
    if (!pad.start) value |= 0x08;    // RUN
    if (!pad.up) value |= 0x10;
    if (!pad.right) value |= 0x20;
    if (!pad.down) value |= 0x40;
    if (!pad.left) value |= 0x80;
    pad_ = value;
}

uint32_t PcEngine::rom_offset(uint32_t bank) const {
    const size_t size = rom_.size();
    if (size == 0) return 0;
    if (size == 0x60000) {
        // 384 KiB HuCards: 256 KiB linear, then the last 128 KiB mirrored twice.
        const uint32_t low = bank & 0x3f;
        if (low < 0x20) return low * 0x2000;
        return 0x40000 + (low & 0x0f) * 0x2000;
    }
    if (size > 0x100000) {
        // Street Fighter II style mapper: the upper half is bank switched.
        if (bank < 0x40) return bank * 0x2000;
        return 0x80000 + uint32_t(sf2_bank_) * 0x80000 + (bank & 0x3f) * 0x2000;
    }
    return uint32_t((size_t(bank) * 0x2000) % size);
}

uint8_t PcEngine::read_byte(uint32_t address) {
    const uint32_t bank = (address >> 13) & 0xff;
    if (bank <= 0x7f) {
        if (rom_.empty()) return 0xff;
        const size_t offset = rom_offset(bank) + (address & 0x1fff);
        return offset < rom_.size() ? rom_[offset] : 0xff;
    }
    if (bank == 0xf7) return backup_ram_[address & 0x7ff];
    if (bank >= 0xf8 && bank <= 0xfb) return ram_[address & 0x1fff];
    if (bank == 0xff) return io_read(uint16_t(address & 0x1fff));
    return 0xff;
}

void PcEngine::write_byte(uint32_t address, uint8_t value) {
    const uint32_t bank = (address >> 13) & 0xff;
    if (bank <= 0x7f) {
        // Street Fighter II selects its ROM bank through $1ff0-$1ff3.
        if (rom_.size() > 0x100000 && (address & 0x1ffc) == 0x1ff0) sf2_bank_ = uint8_t(address & 3);
        return;
    }
    if (bank == 0xf7) {
        backup_ram_[address & 0x7ff] = value;
        return;
    }
    if (bank >= 0xf8 && bank <= 0xfb) {
        ram_[address & 0x1fff] = value;
        return;
    }
    if (bank == 0xff) io_write(uint16_t(address & 0x1fff), value);
}

uint8_t PcEngine::io_read(uint16_t offset) {
    switch (offset & 0x1c00) {
        case 0x0000: return vdc_.read(uint8_t(offset & 3));
        case 0x0400: return vce_.read(uint8_t(offset & 7));
        case 0x0800: return 0xff;  // the PSG is write only
        case 0x0c00: return cpu_.timer_r();
        case 0x1000: return joypad_read();
        case 0x1400: return cpu_.irq_status_r(uint8_t(offset & 3));
        default: return 0xff;  // CD-ROM and expansion port are not present
    }
}

void PcEngine::io_write(uint16_t offset, uint8_t value) {
    switch (offset & 0x1c00) {
        case 0x0000: vdc_.write(uint8_t(offset & 3), value); break;
        case 0x0400: vce_.write(uint8_t(offset & 7), value); break;
        case 0x0800: psg_.write(uint8_t(offset & 0x0f), value); break;
        case 0x0c00: cpu_.timer_w(uint8_t(offset & 1), value); break;
        case 0x1000: joypad_write(value); break;
        case 0x1400: cpu_.irq_status_w(uint8_t(offset & 3), value); break;
        default: break;
    }
}

uint8_t PcEngine::joypad_read() const {
    // CLR high holds the pad outputs low, SEL picks directions over buttons.
    const uint8_t pad = pad_clear_ ? 0x00 : pad_;
    const uint8_t data = uint8_t((pad_select_ ? pad >> 4 : pad) & 0x0f);
    // Bits 4 and 5 are pulled high; bit 6 low marks a Japanese console.
    return uint8_t(data | 0x30);
}

void PcEngine::joypad_write(uint8_t value) {
    pad_select_ = (value & 0x01) != 0;
    pad_clear_ = (value & 0x02) != 0;
}

void PcEngine::on_cpu_cycles(int cycles) {
    audio_accumulator_ += uint64_t(cycles) * uint64_t(HuC6280Psg::kSampleRate);
    while (audio_accumulator_ >= kClock) {
        audio_accumulator_ -= kClock;
        audio_.push_back(psg_.update());
    }
}

void PcEngine::blit_line(int display_line, int width) {
    const int row = display_line * 2;
    if (row < 0 || row + 1 >= kScreenHeight) return;
    uint32_t* top = framebuffer_.data() + size_t(row) * kScreenWidth;
    uint32_t* bottom = top + kScreenWidth;
    for (int x = 0; x < kScreenWidth; x++) {
        const int source = width == kScreenWidth ? x : x * width / kScreenWidth;
        const uint16_t index = line_[size_t(source)];
        const uint32_t colour = index != 0 ? vce_.colour(index) : vce_.backdrop();
        top[x] = colour;
        bottom[x] = colour;
    }
}

void PcEngine::run_frame() {
    for (int line = 0; line < kScanlines; line++) {
        // The VCE dot clock caps how many of the VDC's visible pixels fit on a line.
        const int width =
            std::min(std::min(vdc_.display_width(), vce_.active_width()), HuC6270::kMaxWidth);
        if (vdc_.scanline(line, line_.data(), width)) blit_line(vdc_.display_line(), width);
        cpu_.run(kCyclesPerLine);
    }
    vdc_.end_frame();
}

void PcEngine::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
