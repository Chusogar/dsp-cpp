#include "drivers/nes.h"

#include <algorithm>
#include <cstring>
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

}  // namespace

Nes::Nes() : cpu_(kClock), apu_(kClock) {
    cpu_.set_memory_handlers([this](uint16_t a) { return read_byte(a); },
                             [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cpu_cycles(cycles); });
    mapper_.attach(memory_.data(), &ppu_, [this](IrqLine line) {
        mapper_irq_ = line != IrqLine::Clear;
        cpu_.set_irq(line);
    });
    ppu_.set_line_ack([this](bool force) { mapper_.line_ack(force); });
    ppu_.set_ppu_read([this](uint16_t address) { mapper_.ppu_read(address); });
    apu_.set_dpcm_reader([this](uint16_t address) { return read_byte(address); });
    apu_.set_irq_handler([this]() {
        if (!mapper_irq_) cpu_.set_irq(IrqLine::Hold);
    });
}

bool Nes::init(const std::string& rom_path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!rom_path.empty() && fs::is_regular_file(rom_path, ec)) {
        std::string cart_error;
        if (!load_media(rom_path, &cart_error)) {
            if (error) *error = cart_error;
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

bool Nes::load_media(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_plain_or_zip_file(path, data, kMaxCartridge + 16 + 0x200, error)) return false;
    return load_ines(data, error);
}

void Nes::apply_crc_patches(uint32_t crc, uint32_t chr_crc, int& mapper, int& submapper) {
    switch (crc) {
        case 0x3fc29044:
        case 0x2ed79b73:
        case 0x76124d08:
            submapper = 1;
            break;
        case 0x50f66538:
            memory_[0xfffd] = 0xca;
            break;
        case 0x7a5cc019:
            memory_[0xfb14] = 0x04;
            memory_[0xfb15] = 0x04;
            break;
        case 0x42edbce2:
        case 0xacc2b74a:
        case 0xd8dfd3d1:
            submapper = 1;
            break;
        case 0x51ce0655:
        case 0x761e1fc9:
        case 0x57d8330a:
        case 0xe1539190:
            mapper = 206;
            ppu_.mirror = NesPpu::FourScreen;
            break;
        case 0x0d327f0a:
            mapper = 154;
            break;
        case 0x4433ba0a:
            mapper = 87;
            break;
        case 0x3c7b0120:
        case 0xad893bf7:
        case 0x2fb7d5b9:
        case 0x0977f982:
        case 0xd994d5ff:
        case 0xf07d31b2:
        case 0xe476313e:
        case 0x103f0755:
        case 0x63d71cda:
        case 0xa8a1c2eb:
        case 0xc8e5e815:
        case 0x6fdf50d0:
        case 0x154a31b6:
            mapper = 206;
            break;
        case 0xd122ba8d:
        case 0x62e7aec5:
        case 0x6ee61da3:
            mapper = 152;
            break;
        default:
            break;
    }
    if (mapper == 243 && chr_crc != 0x282dcb3a && chr_crc != 0x331802e2) mapper = 150;
    switch (chr_crc) {
        case 0x19c5c4aa:
            if (mapper == 25) {
                submapper = 1;
                mapper = 23;
            }
            break;
        case 0x824324fa:
        case 0x87c17609:
        case 0x3b31f998:
            if (mapper == 25) submapper = 3;
            break;
        case 0xf82b8e59:
            if (mapper == 21) submapper = 1;
            break;
        case 0xae17c652:
        case 0x23f896a7:
            if (mapper == 25) submapper = 4;
            break;
        case 0xa30927de:
        case 0x7b790220:
        case 0xc2cf279a:
        case 0x88b512d6:
        case 0xeb9fd289:
            if (mapper == 23) {
                submapper = 2;
                mapper = 21;
            }
            break;
        case 0xbd493548:
            submapper = 1;
            break;
        case 0x7ff2dc2b:
        case 0x6add6cd6:
        case 0x1557191a:
        case 0x8f03a735:
        case 0xe8d170d8:
        case 0xcc06cf3e:
            if (mapper == 33) mapper = 48;
            break;
        case 0xf47f0bca:
            if (mapper == 173) mapper = 132;
            break;
        case 0x1a145504:
        case 0x19c33692:
            if (mapper == 79) mapper = 173;
            break;
        case 0x479fb8e6:
            mapper = 133;
            break;
        default:
            break;
    }
}

bool Nes::load_ines(const std::vector<uint8_t>& data, std::string* error) {
    if (data.size() < 16 || data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1a) {
        if (error) *error = "not a valid iNES cartridge";
        return false;
    }
    const uint8_t flags6 = data[6];
    const uint8_t flags7 = data[7];
    size_t offset = 16;
    mapper_.prg_ram_enable = false;
    if (flags6 & 0x04) {
        if (data.size() < offset + 0x200) {
            if (error) *error = "truncated iNES trainer";
            return false;
        }
        std::memcpy(&memory_[0x7000], data.data() + offset, 0x200);
        mapper_.prg_ram_enable = true;
        offset += 0x200;
    }
    mapper_.prg = {};
    mapper_.chr = {};
    mapper_.last_prg = data[4];
    if (mapper_.last_prg > 32 || mapper_.last_prg == 0) {
        if (error) *error = "unsupported PRG ROM size";
        return false;
    }
    for (int f = 0; f < mapper_.last_prg; ++f) {
        if (data.size() < offset + 0x4000) {
            if (error) *error = "truncated PRG ROM";
            return false;
        }
        std::memcpy(mapper_.prg[size_t(f)].data(), data.data() + offset, 0x4000);
        offset += 0x4000;
    }
    std::memcpy(&memory_[0x8000], mapper_.prg[0].data(), 0x4000);
    if (mapper_.last_prg == 1) {
        std::memcpy(&memory_[0xc000], mapper_.prg[0].data(), 0x4000);
    } else {
        std::memcpy(&memory_[0xc000], mapper_.prg[1].data(), 0x4000);
    }
    mapper_.last_chr = data[5];
    if (mapper_.last_chr > 63) {
        if (error) *error = "unsupported CHR ROM size";
        return false;
    }
    if (mapper_.last_chr == 0) {
        ppu_.write_chr = true;
        mapper_.chr[0].fill(0);
    } else {
        ppu_.write_chr = false;
        for (int f = 0; f < mapper_.last_chr; ++f) {
            if (data.size() < offset + 0x2000) {
                if (error) *error = "truncated CHR ROM";
                return false;
            }
            std::memcpy(mapper_.chr[size_t(f)].data(), data.data() + offset, 0x2000);
            offset += 0x2000;
        }
        std::memcpy(ppu_.chr_bank(0), mapper_.chr[0].data(), 0x1000);
        std::memcpy(ppu_.chr_bank(1), mapper_.chr[0].data() + 0x1000, 0x1000);
    }

    int mapper_num = 0;
    int submapper = 0;
    if ((flags7 & 0x0c) == 8) {
        submapper = (data[8] & 0xf0) >> 4;
        mapper_num = (flags6 >> 4) | (flags7 & 0xf0) | ((data[8] & 0x0f) << 8);
    } else if (data[13] != 0 && data[14] != 0 && data[15] != 0 && data[12] != 0) {
        mapper_num = flags6 >> 4;
    } else {
        mapper_num = (flags6 >> 4) | (flags7 & 0xf0);
    }

    ppu_.mirror = NesPpu::Vertical;
    if (flags6 & 0x08) {
        ppu_.mirror = NesPpu::FourScreen;
    } else if ((flags6 & 0x01) == 0) {
        ppu_.mirror = NesPpu::Horizontal;
    }

    const uint32_t crc = crc32_of(data.data(), data.size());
    const uint32_t chr_crc = crc32_of(mapper_.chr[0].data(), 0x2000);
    apply_crc_patches(crc, chr_crc, mapper_num, submapper);

    if (!mapper_.set_mapper(mapper_num, submapper)) {
        if (error) *error = "mapper " + std::to_string(mapper_num) + " is not supported";
        return false;
    }
    reset();
    return true;
}

void Nes::reset() {
    mapper_.reset();
    cpu_.reset();
    ppu_.reset();
    apu_.reset();
    joy1_ = joy2_ = 0;
    joy1_read_ = joy2_read_ = 0;
    val_4016_ = false;
    even_frame_ = true;
    mapper_irq_ = false;
    cycle_count_ = 0;
    apu_cycles_ = 0;
    frame_irq_cycles_ = 0;
    audio_accumulator_ = 0;
    audio_.clear();
    cpu_.set_irq(IrqLine::Clear);
    cpu_.set_nmi(IrqLine::Clear);
}

void Nes::on_cpu_cycles(int cycles) {
    cycle_count_ += uint64_t(cycles);
    mapper_.add_cycles(cycles);
    apu_cycles_ += uint64_t(cycles);
    while (apu_cycles_ >= 4) {
        apu_cycles_ -= 4;
        apu_.advance();
    }
    audio_accumulator_ += uint64_t(cycles) * uint64_t(NesApu::kSampleRate);
    while (audio_accumulator_ >= kClock) {
        audio_accumulator_ -= kClock;
        audio_.push_back(apu_.update());
    }
    constexpr uint64_t kFrameIrqPeriod = uint64_t(double(kClock) / kFramesPerSecond + 0.5);
    frame_irq_cycles_ += uint64_t(cycles);
    if (frame_irq_cycles_ >= kFrameIrqPeriod) {
        frame_irq_cycles_ -= kFrameIrqPeriod;
        if (apu_.frame_irq_timer_enabled() && apu_.frame_irq() && !mapper_irq_) {
            cpu_.set_irq(IrqLine::Hold);
        }
    }
}

void Nes::run_cpu(double cycles) {
    const int n = std::max(1, int(cycles + 0.5));
    cpu_.run(n);
}

void Nes::run_frame() {
    constexpr double kLine = double(kClock) / kFramesPerSecond / kScanlines;
    constexpr double kHalfVisible = 128.0 * NesPpu::kPpuPixelTiming;
    for (ppu_.linea = 0; ppu_.linea < kScanlines; ++ppu_.linea) {
        const int line = ppu_.linea;
        if (line <= 239) {
            run_cpu(kHalfVisible);
            ppu_.end_y_coarse();
            ppu_.draw_linea(line, &framebuffer_[size_t(line) * NesPpu::kScreenWidth]);
            run_cpu(kLine - kHalfVisible);
        } else if (line == 240) {
            run_cpu(kLine);
        } else if (line == 241) {
            run_cpu(NesPpu::kPpuPixelTiming);
            ppu_.status |= 0x80;
            if (ppu_.control1 & 0x80) {
                cpu_.set_nmi(IrqLine::Pulse);
                cpu_.delay_interrupts();
            }
            run_cpu(kLine - NesPpu::kPpuPixelTiming);
        } else if (line <= 260) {
            run_cpu(kLine);
        } else {
            run_cpu(NesPpu::kPpuPixelTiming);
            ppu_.status &= 0x1f;
            ppu_.sprite_over_flow = false;
            double rest = kLine - NesPpu::kPpuPixelTiming;
            if (even_frame_) rest -= NesPpu::kPpuPixelTiming;
            even_frame_ = !even_frame_;
            run_cpu(rest);
            if (ppu_.control2 & 0x18) {
                ppu_.address = uint16_t((ppu_.address & 0x41f) | (ppu_.address_temp & 0x7be0));
            }
            mapper_.line_ack(false);
        }
    }
    ppu_.linea = 0;
}

uint8_t Nes::read_byte(uint16_t address) {
    ppu_.open_bus = uint8_t(address >> 8);
    if (address <= 0x1fff) return memory_[address & 0x7ff];
    if (address <= 0x3fff) {
        switch (address & 7) {
            case 2: {
                const uint8_t status = ppu_.status;
                ppu_.status &= 0x60;
                ppu_.dir_first = true;
                cpu_.set_nmi(IrqLine::Clear);
                return status;
            }
            case 4:
                return ppu_.sprite_ram()[ppu_.sprite_ram_pos];
            case 7:
                return ppu_.read();
            default:
                return ppu_.open_bus;
        }
    }
    if (address <= 0x4013 || address == 0x4015) return apu_.read(address);
    if (address == 0x4016) {
        const uint8_t bit = uint8_t((joy1_read_ >> joy1_) & 1);
        joy1_ = uint8_t(joy1_ + 1);
        return uint8_t((ppu_.open_bus & 0xe0) | bit);
    }
    if (address == 0x4017) {
        const uint8_t bit = uint8_t((joy2_read_ >> joy2_) & 1);
        joy2_ = uint8_t(joy2_ + 1);
        return uint8_t((ppu_.open_bus & 0xe0) | bit);
    }
    if (address <= 0x5fff) {
        if (mapper_.has_read_expansion()) return mapper_.read_expansion(address);
        return ppu_.open_bus;
    }
    if (address <= 0x7fff) return mapper_.read_prg_ram(address);
    if (mapper_.has_read_rom()) return mapper_.read_rom(address);
    return memory_[address];
}

void Nes::write_byte(uint16_t address, uint8_t value) {
    if (address <= 0x1fff) {
        memory_[address & 0x7ff] = value;
        return;
    }
    if (address <= 0x3fff) {
        switch (address & 7) {
            case 0:
                if ((ppu_.status & 0x80) != 0 && (ppu_.control1 & 0x80) == 0 && (value & 0x80) != 0) {
                    cpu_.set_nmi(IrqLine::Pulse);
                    cpu_.delay_interrupts();
                }
                ppu_.control1 = value;
                ppu_.sprite_size = uint8_t(8 << ((value >> 5) & 1));
                ppu_.pos_bg = uint8_t((value >> 4) & 1);
                ppu_.pos_spt = uint8_t((value >> 3) & 1);
                ppu_.address_temp =
                    uint16_t((ppu_.address_temp & 0x73ff) | ((value & 0x03) << 10));
                break;
            case 1:
                ppu_.control2 = value;
                ppu_.pal_mask = (value & 1) ? 0x30 : 0x3f;
                break;
            case 3:
                ppu_.sprite_ram_pos = value;
                break;
            case 4:
                if (ppu_.linea < 240) value = 0xff;
                ppu_.sprite_ram()[ppu_.sprite_ram_pos] = value;
                ppu_.sprite_ram_pos = uint8_t(ppu_.sprite_ram_pos + 1);
                break;
            case 5:
                if (ppu_.dir_first) {
                    ppu_.address_temp = uint16_t((ppu_.address_temp & 0x7fe0) | ((value & 0xf8) >> 3));
                    ppu_.tile_x_offset = value & 7;
                } else {
                    ppu_.address_temp = uint16_t((ppu_.address_temp & 0x0c1f) |
                                                 ((value & 0xf8) << 2) | ((value & 7) << 12));
                }
                ppu_.dir_first = !ppu_.dir_first;
                break;
            case 6:
                if (ppu_.dir_first) {
                    ppu_.address_temp =
                        uint16_t((ppu_.address_temp & 0x00ff) | ((value & 0x3f) << 8));
                } else {
                    ppu_.address_temp = uint16_t((ppu_.address_temp & 0x7f00) | value);
                    ppu_.address = ppu_.address_temp;
                    if ((ppu_.address & 0x1000) != 0) mapper_.line_ack(true);
                }
                ppu_.dir_first = !ppu_.dir_first;
                break;
            case 7:
                ppu_.write(value);
                if ((ppu_.address & 0x1000) != 0) mapper_.line_ack(true);
                break;
            default:
                break;
        }
        return;
    }
    if (address <= 0x4013 || address == 0x4015 || address == 0x4017) {
        apu_.write(address, value);
        return;
    }
    if (address == 0x4014) {
        ppu_.dma_spr(value, memory_.data(), [this](int n) {
            cpu_.steal_cycles(n + int(cycle_count_ & 1));
        });
        return;
    }
    if (address == 0x4016) {
        if ((value & 1) == 0 && val_4016_) {
            joy1_ = 0;
            joy2_ = 0;
        }
        val_4016_ = (value & 1) != 0;
        return;
    }
    if (address <= 0x5fff) {
        mapper_.write_expansion(address, value);
        return;
    }
    if (address <= 0x7fff) {
        mapper_.write_prg_ram(address, value);
        return;
    }
    mapper_.write_rom(address, value);
}

void Nes::set_inputs(const MachineInputs& inputs) {
    auto pack = [](const InputState& p, bool select) {
        uint8_t v = 0;
        if (p.button1) v |= 0x01;  // A
        if (p.button2) v |= 0x02;  // B
        if (select) v |= 0x04;     // SELECT
        if (p.start) v |= 0x08;
        if (p.up) v |= 0x10;
        if (p.down) v |= 0x20;
        if (p.left) v |= 0x40;
        if (p.right) v |= 0x80;
        return v;
    };
    joy1_read_ = pack(inputs.player1, inputs.coin1);
    joy2_read_ = pack(inputs.player2, inputs.coin2);
}

void Nes::set_dip_switch(int, uint8_t) {}

void Nes::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
