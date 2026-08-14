#include "drivers/gamegear.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto n = f.tellg();
    if (n <= 0) return false;
    f.seekg(0, std::ios::beg);
    out->resize(size_t(n));
    f.read(reinterpret_cast<char*>(out->data()), n);
    return bool(f);
}

bool ends_with_ci(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = s[s.size() - n + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

}  // namespace

uint32_t GameGear::crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1) + 1));
    }
    return ~crc;
}

GameGear::GameGear()
    : cpu_(kClockNtsc),
      vdp_([this](bool assert) {
          cpu_.set_irq(assert ? IrqLine::Assert : IrqLine::Clear);
      }),
      psg_(kClockNtsc),
      framebuffer_(size_t(kSmsWidth * kSmsHeight), 0) {
    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_byte(a); },
        [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_io_handlers(
        [this](uint16_t p) { return read_port(p); },
        [this](uint16_t p, uint8_t v) { write_port(p, v); });
    cpu_.set_cycle_handler([this](int c) { on_cycles(c); });

    vdp_.video_ntsc(0);
    vdp_.set_gg(true);
    cycles_per_line_ =
        int(double(kClockNtsc) / kFpsNtsc / double(kLinesNtsc) + 0.5);
}

bool GameGear::init(const std::string& rom_path, std::string* error) {
    return load_media(rom_path, error);
}

bool GameGear::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_file(path, &data)) {
        if (error) *error = "cannot open ROM: " + path;
        return false;
    }
    return load_cartridge(data.data(), data.size(), error);
}

bool GameGear::load_cartridge(const uint8_t* data, size_t length,
                              std::string* error) {
    // Skip 512-byte copier header if present.
    size_t offset = 0;
    if ((length % 0x4000) == 512) {
        offset = 512;
        length -= 512;
    }
    if (length == 0) {
        if (error) *error = "empty cartridge";
        return false;
    }

    for (auto& bank : rom_) bank.fill(0xFF);

    if (length < 0x4000) {
        std::memcpy(rom_[0].data(), data + offset, length);
        rom_banks_ = 1;
    } else {
        // Align down to 16 KB banks.
        if (length % 0x4000) {
            offset += length % 0x4000;
            length -= length % 0x4000;
        }
        rom_banks_ = int(length / 0x4000);
        if (rom_banks_ > kMaxRomBanks) {
            if (error) *error = "ROM too large";
            return false;
        }
        for (int b = 0; b < rom_banks_; b++) {
            std::memcpy(rom_[b].data(), data + offset + size_t(b) * 0x4000, 0x4000);
        }
    }

    detect_special(crc32(data + offset, length));
    set_default_banks();
    reset();
    return true;
}

void GameGear::detect_special(uint32_t crc) {
    mapper_ = Mapper::Sega;
    sms_video_ = false;
    vdp_.set_gg(true);

    // Codemasters CRC list from sega_gg.pas
    static const uint32_t kCodemasters[] = {
        0x5e53c7f7, 0xdbe8895c, 0xf7c524f6, 0xc888222b, 0xaa140c9c, 0x8813514b,
        0x9fa727a0, 0xfb481971, 0xd9a7f170, 0x76c5bdfb, 0xc1756bee, 0x6caa625b,
        0x152f0dcc, 0x72981057};
    for (uint32_t c : kCodemasters) {
        if (c == crc) {
            mapper_ = Mapper::Codemasters;
            break;
        }
    }

    // Carts that need full SMS video size
    static const uint32_t kSmsVideo[] = {
        0xe5f789b9, 0x9942b69b, 0x5877b10d, 0x59840fd6, 0xaa140c9c, 0xc8381def,
        0xc888222b, 0x76c5bdfb, 0x1d93246e, 0xce97efe8, 0xa2f9c7af, 0x3382d73f,
        0x01eab89d, 0xf037ec00, 0x2aa12d7e, 0x0189931e, 0x86e5b455, 0x45f058d6,
        0x311d2863, 0xba6344fc, 0x1c6c149c, 0x9c76fb3a, 0x56201996, 0x4902b7a2,
        0xfb481971, 0x9fa727a0, 0x10dbbef4, 0xbd1cc7df, 0x8230384e, 0xda8e95a9,
        0x6f8e46cf, 0x7bb81e3d, 0x44fbe8f6, 0x3b627808, 0x18086b70, 0x8813514b};
    for (uint32_t c : kSmsVideo) {
        if (c == crc) {
            sms_video_ = true;
            vdp_.set_gg(false);
            break;
        }
    }
}

void GameGear::set_default_banks() {
    rom_bank_[0] = 0;
    rom_bank_[1] = uint8_t(1 % std::max(1, rom_banks_));
    rom_bank_[2] = uint8_t(2 % std::max(1, rom_banks_));
    slot2_ram_enable_ = false;
    slot2_bank_ = 0;
}

void GameGear::reset() {
    cpu_.reset();
    // GG BIOS-less start values from reset_gg
    cpu_.sp = 0xDFEB;
    cpu_.a = 0x14;
    cpu_.b = 0xFF;
    cpu_.c = 0x3C;
    cpu_.h = 0x00;
    cpu_.l = 0x02;

    psg_.reset();
    vdp_.reset();
    vdp_.video_ntsc(0);
    vdp_.set_gg(!sms_video_);

    ram_.fill(0);
    for (auto& s : slot2_ram_) s.fill(0);
    io_.fill(0);
    keys0_ = 0xFF;
    keys1_ = 0x80;
    set_default_banks();
    audio_.clear();
    audio_accumulator_ = 0;
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0u);
}

uint8_t GameGear::read_byte(uint16_t address) {
    if (mapper_ == Mapper::Codemasters) {
        if (address < 0x4000)
            return rom_[rom_bank_[0] % rom_banks_][address];
        if (address < 0x8000)
            return rom_[rom_bank_[1] % rom_banks_][address & 0x3FFF];
        if (address < 0xA000)
            return rom_[rom_bank_[2] % rom_banks_][address & 0x3FFF];
        if (address < 0xC000) {
            if (slot2_ram_enable_)
                return slot2_ram_[0][address & 0x1FFF];
            return rom_[rom_bank_[2] % rom_banks_][address & 0x3FFF];
        }
        return ram_[address & 0x1FFF];
    }

    // Standard Sega mapper
    if (address < 0x4000)
        return rom_[rom_bank_[0] % rom_banks_][address];
    if (address < 0x8000)
        return rom_[rom_bank_[1] % rom_banks_][address & 0x3FFF];
    if (address < 0xC000) {
        if (slot2_ram_enable_)
            return slot2_ram_[slot2_bank_ & 1][address & 0x3FFF];
        return rom_[rom_bank_[2] % rom_banks_][address & 0x3FFF];
    }
    return ram_[address & 0x1FFF];
}

void GameGear::write_byte(uint16_t address, uint8_t value) {
    if (mapper_ == Mapper::Codemasters) {
        if (address == 0x0000) {
            rom_bank_[0] = uint8_t(value % rom_banks_);
            return;
        }
        if (address == 0x4000) {
            rom_bank_[1] = uint8_t((value & 0x7F) % rom_banks_);
            slot2_ram_enable_ = (value & 0x80) != 0;
            return;
        }
        if (address == 0x8000) {
            rom_bank_[2] = uint8_t(value % rom_banks_);
            return;
        }
        if (address >= 0xA000 && address < 0xC000 && slot2_ram_enable_) {
            slot2_ram_[0][address & 0x1FFF] = value;
            return;
        }
        if (address >= 0xC000) ram_[address & 0x1FFF] = value;
        return;
    }

    if (address < 0x8000) return;
    if (address < 0xC000) {
        if (slot2_ram_enable_)
            slot2_ram_[slot2_bank_ & 1][address & 0x3FFF] = value;
        return;
    }
    ram_[address & 0x1FFF] = value;
    if (address >= 0xFFFC) {
        switch (address & 3) {
            case 0:
                slot2_ram_enable_ = (value & 0x08) != 0;
                slot2_bank_ = uint8_t((value & 0x04) >> 2);
                break;
            case 1:
                rom_bank_[0] = uint8_t(value % rom_banks_);
                break;
            case 2:
                rom_bank_[1] = uint8_t(value % rom_banks_);
                break;
            case 3:
                rom_bank_[2] = uint8_t(value % rom_banks_);
                break;
        }
    }
}

uint8_t GameGear::read_port(uint16_t port) {
    port &= 0xFF;
    if (port == 0) return uint8_t(keys1_ | 0x40);
    if (port >= 1 && port <= 6) return io_[port];
    if ((port >= 7 && port <= 0x3F) || (port >= 0xC2 && port <= 0xDB) ||
        port >= 0xDE)
        return 0xFF;
    if (port >= 0x40 && port <= 0x7F) {
        if (port & 1) return vdp_.hpos();
        return vdp_.linea_back();
    }
    if (port >= 0x80 && port <= 0xBF) {
        if (port & 1) return uint8_t(vdp_.register_r());
        return vdp_.vram_r();
    }
    if (port == 0xC0 || port == 0xDC) return keys0_;
    if (port == 0xC1 || port == 0xDD) return 0xFF;
    return 0xFF;
}

void GameGear::write_port(uint16_t port, uint8_t value) {
    port &= 0xFF;
    if (port <= 6) {
        io_[port] = value;
        return;
    }
    if (port <= 0x3F) {
        // Memory control (GG has no BIOS disable path used in practice).
        return;
    }
    if (port <= 0x7F) {
        psg_.write(value);
        return;
    }
    if (port <= 0xBF) {
        if (port & 1)
            vdp_.register_w(value);
        else
            vdp_.vram_w(value);
        return;
    }
}

void GameGear::on_cycles(int cycles) {
    // Approximate H-counter from cycles into the current line.
    vdp_.set_hpos(cycles);
    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= int64_t(kClockNtsc)) {
        audio_accumulator_ -= int64_t(kClockNtsc);
        const int32_t s = psg_.update();
        audio_.push_back(int16_t(std::clamp(s, int32_t(-32768), int32_t(32767))));
    }
}

void GameGear::run_frame() {
    const int lines = vdp_.video_y_total() > 0 ? vdp_.video_y_total() : kLinesNtsc;
    const int width = screen_width();
    const int height = screen_height();

    // Ensure framebuffer capacity.
    if (framebuffer_.size() < size_t(width * height))
        framebuffer_.assign(size_t(width * height), 0);

    for (int line = 0; line < lines; line++) {
        cpu_.run(cycles_per_line_);
        vdp_.refresh(line);

        const uint32_t* src = vdp_.line_buffer();
        if (sms_video_) {
            // Full 284-wide lines; map into 243-tall frame like SMS.
            if (line < height) {
                uint32_t* dst = framebuffer_.data() + size_t(line) * size_t(width);
                std::memcpy(dst, src, size_t(width) * sizeof(uint32_t));
            }
        } else {
            // Crop 160×144 starting at (61, 51).
            if (line >= kCropY && line < kCropY + kGgHeight) {
                const int dy = line - kCropY;
                uint32_t* dst = framebuffer_.data() + size_t(dy) * kGgWidth;
                std::memcpy(dst, src + kCropX, size_t(kGgWidth) * sizeof(uint32_t));
            }
        }
    }
}

void GameGear::set_inputs(const MachineInputs& inputs) {
    auto set_bit = [](uint8_t& port, int bit, bool pressed) {
        if (pressed)
            port = uint8_t(port & ~(1 << bit));
        else
            port = uint8_t(port | (1 << bit));
    };
    // Active-low: up down left right b1 b2
    set_bit(keys0_, 0, inputs.player1.up);
    set_bit(keys0_, 1, inputs.player1.down);
    set_bit(keys0_, 2, inputs.player1.left);
    set_bit(keys0_, 3, inputs.player1.right);
    set_bit(keys0_, 4, inputs.player1.button1);
    set_bit(keys0_, 5, inputs.player1.button2);
    // Start on bit7 of keys1 (active low)
    set_bit(keys1_, 7, inputs.player1.start || inputs.coin1);
}

void GameGear::set_dip_switch(int /*bank*/, uint8_t /*value*/) {}

void GameGear::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
