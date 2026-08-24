#include "drivers/pcengine.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

bool ends_ci(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = s[s.size() - n + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

std::vector<uint8_t> read_file(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return {};
    }
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (n <= 0) {
        if (error) *error = "empty " + path;
        return {};
    }
    std::vector<uint8_t> data(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(data.data()), n);
    return data;
}

}  // namespace

PcEngine::PcEngine(bool supergrafx)
    : supergrafx_(supergrafx),
      cpu_(kCpuClock),
      psg_(kCpuClock),
      cycles_per_line_(int(double(kCpuClock) / kFramesPerSecond / kLinesPerFrame)) {
    cpu_.set_memory_handlers(
        [this](uint32_t a) { return read_physical(a); },
        [this](uint32_t a, uint8_t v) { write_physical(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });

    // HuC6280: line 0 → $FFF8 (IRQ1), line 1 → $FFF6 (IRQ2), line 2 → timer.
    // Commercial HuCards (incl. Ninja Warriors) put the VDC service routine at
    // $FFF8; wire the VDC there so VBlank/RCR actually run game code.
    vdc0_.set_irq_handler([this](bool assert) {
        vdc0_irq_ = assert;
        update_vdc_irq();
    });
    vdc1_.set_irq_handler([this](bool assert) {
        vdc1_irq_ = assert;
        update_vdc_irq();
    });
}

void PcEngine::update_vdc_irq() {
    // OR both VDCs into IRQ1 ($FFF8) — matches HuCard service routines.
    const bool any = vdc0_irq_ || (supergrafx_ && vdc1_irq_);
    cpu_.set_irq_line(0, any ? IrqLine::Assert : IrqLine::Clear);
}

bool PcEngine::init(const std::string& rom_path, std::string* error) {
    if (ends_ci(rom_path, ".sgx")) supergrafx_ = true;
    return load_media(rom_path, error);
}

bool PcEngine::load_media(const std::string& path, std::string* error) {
    if (ends_ci(path, ".sgx")) supergrafx_ = true;
    auto data = read_file(path, error);
    if (data.empty()) return false;
    return load_hucard(data, error);
}

bool PcEngine::load_hucard(const std::vector<uint8_t>& data, std::string* error) {
    size_t offset = 0;
    // Only strip 512-byte header when size is 512 past a bank multiple
    if (data.size() > 0x200 && (data.size() % 0x2000) == 0x200) offset = 0x200;
    const size_t cart = data.size() - offset;
    if (cart < 0x2000) {
        if (error) *error = "HuCard too small";
        return false;
    }
    if (cart > kMaxRom) {
        if (error) *error = "HuCard too large";
        return false;
    }
    rom_.assign(kMaxRom, 0xFF);
    for (size_t i = 0; i < kMaxRom; ++i) rom_[i] = data[offset + (i % cart)];
    reset();
    return true;
}

void PcEngine::reset() {
    ram_.fill(0);
    vdc0_.reset();
    vdc1_.reset();
    vpc_.reset();
    vce_.reset();
    psg_.reset();
    joy_data_ = 0xFF;
    joy_sel_ = joy_clr_ = 0;
    vdc0_irq_ = vdc1_irq_ = false;
    width_ = 256;
    audio_.clear();
    framebuffer_.fill(0xFF000000u);
    cpu_.reset();
}

uint8_t PcEngine::read_physical(uint32_t address) {
    const uint8_t page = uint8_t(address >> 13);
    const uint16_t offset = uint16_t(address & 0x1FFF);

    if (page < 0x80) {
        return rom_[(size_t(page) << 13 | offset) & (kMaxRom - 1)];
    }
    // RAM: $F8-$FB (8KB) or SuperGrafx $F8-$FF partial
    if (page >= 0xF8 && page <= 0xFB) {
        return ram_[offset & 0x1FFF];
    }
    if (supergrafx_ && page >= 0xF8 && page <= 0xFF) {
        // Extra RAM on SuperGrafx in some maps — keep simple 32KB window
        const size_t idx = (size_t(page - 0xF8) << 13) | offset;
        if (idx < kRamSize) return ram_[idx];
    }

    if (page == 0xFF) {
        if (offset < 0x0400) return vdc0_.read(uint8_t(offset & 3));
        if (offset < 0x0800) return vce_.read(uint8_t(offset & 3));
        if (offset < 0x0C00) return 0x00;  // PSG write-only
        if (offset < 0x1000) return cpu_.timer_r(uint8_t(offset & 1));
        if (offset < 0x1400) {
            if (joy_clr_ & 2) return 0xFF;
            if (joy_sel_ & 1) return uint8_t(0xF0 | ((joy_data_ >> 4) & 0x0F));
            return uint8_t(0xF0 | (joy_data_ & 0x0F));
        }
        if (offset < 0x1800) return cpu_.irq_status_r(uint8_t(offset & 3));
        // SuperGrafx: VDC1 at $0000 mirror via VPC map — $1FE100 area
        if (supergrafx_) {
            if (offset >= 0x0100 && offset < 0x0140)
                return vdc1_.read(uint8_t(offset & 3));
            if (offset >= 0x0800 && offset < 0x0810)
                return vpc_.read(uint8_t(offset & 7));
        }
    }
    return 0xFF;
}

void PcEngine::write_physical(uint32_t address, uint8_t value) {
    const uint8_t page = uint8_t(address >> 13);
    const uint16_t offset = uint16_t(address & 0x1FFF);

    if (page >= 0xF8 && page <= 0xFB) {
        ram_[offset & 0x1FFF] = value;
        return;
    }
    if (supergrafx_ && page >= 0xF8) {
        const size_t idx = (size_t(page - 0xF8) << 13) | offset;
        if (idx < kRamSize) {
            ram_[idx] = value;
            return;
        }
    }

    if (page == 0xFF) {
        if (offset < 0x0400) {
            vdc0_.write(uint8_t(offset & 3), value);
            return;
        }
        if (offset < 0x0800) {
            vce_.write(uint8_t(offset & 3), value);
            width_ = vce_.display_width_for_mode();
            // Prefer VDC HDR width when smaller
            width_ = std::min(width_, vdc0_.display_width());
            if (width_ < 256) width_ = 256;
            return;
        }
        if (offset < 0x0C00) {
            psg_.write(uint8_t(offset & 0x0F), value);
            return;
        }
        if (offset < 0x1000) {
            cpu_.timer_w(uint8_t(offset & 1), value);
            return;
        }
        if (offset < 0x1400) {
            joy_sel_ = value & 1;
            joy_clr_ = value & 2;
            return;
        }
        if (offset < 0x1800) {
            cpu_.irq_status_w(uint8_t(offset & 3), value);
            return;
        }
        if (supergrafx_) {
            if (offset >= 0x0100 && offset < 0x0140) {
                vdc1_.write(uint8_t(offset & 3), value);
                return;
            }
            if (offset >= 0x0800 && offset < 0x0810) {
                vpc_.write(uint8_t(offset & 7), value);
                return;
            }
        }
    }
}

void PcEngine::on_cycles(int cycles) {
    if (cycles > 0) psg_.update(cycles, audio_);
}

void PcEngine::run_frame() {
    width_ = std::max(256, std::min(kMaxWidth, vce_.display_width_for_mode()));
    width_ = std::min(width_, std::max(256, vdc0_.display_width()));

    for (int line = 0; line < kLinesPerFrame; ++line) {
        const int active_start = 14;
        uint16_t* out0 = nullptr;
        uint16_t* out1 = nullptr;
        if (line >= active_start && line < active_start + kMaxHeight) {
            out0 = line0_.data();
            out1 = line1_.data();
        }
        vdc0_.run_line(line, out0, width_);
        if (supergrafx_) vdc1_.run_line(line, out1, width_);

        if (out0) {
            const int row = line - active_start;
            uint32_t* dst = framebuffer_.data() + size_t(row) * kMaxWidth;
            for (int x = 0; x < width_; ++x) {
                uint16_t pix = line0_[x];
                if (supergrafx_) pix = vpc_.mix(line0_[x], line1_[x], x);
                dst[x] = vce_.color(pix);
            }
            for (int x = width_; x < kMaxWidth; ++x) dst[x] = 0xFF000000u;
        }
        cpu_.run(cycles_per_line_);
    }
}

void PcEngine::set_inputs(const MachineInputs& inputs) {
    uint8_t d = 0xFF;
    auto clear = [&](int bit) { d = uint8_t(d & ~(1 << bit)); };
    const auto& p = inputs.player1;
    if (p.up) clear(0);
    if (p.down) clear(1);
    if (p.left) clear(2);
    if (p.right) clear(3);
    if (p.button1) clear(4);
    if (p.button2) clear(5);
    if (p.button3) clear(6);
    if (p.start) clear(7);
    joy_data_ = d;
}

void PcEngine::set_dip_switch(int, uint8_t) {}

void PcEngine::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
