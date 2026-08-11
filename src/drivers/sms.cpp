#include "drivers/sms.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "core/rom_loader.h"
#include "cpu/irq_line.h"

namespace dsp {
namespace {

// Official BIOS CRC32 values used by the original driver.
constexpr uint32_t kBiosCrc[] = {
    0x81c3476b, 0xcf4a09ea, 0x9c5bad91, 0x8edf7ac6, 0x91e93385, 0xe79bb689,
};

bool is_bios_crc(uint32_t crc) {
    for (uint32_t known : kBiosCrc) {
        if (crc == known) return true;
    }
    return false;
}

bool ends_with_ci(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(),
                      [](char a, char b) {
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

}  // namespace

Sms::Sms(Model model)
    : model_(model),
      cpu_(model == Model::Pal ? kClockPal : kClockNtsc),
      vdp_([this](bool assert) {
          cpu_.set_irq(assert ? IrqLine::Hold : IrqLine::Clear);
      }),
      psg_(model == Model::Pal ? kClockPal : kClockNtsc),
      // SMS FM unit clock ≈ NTSC colour clock; original uses 10738635/3.
      opll_(10738635u / 3) {
    const int height = screen_height();
    framebuffer_.assign(size_t(kScreenWidth) * size_t(height), 0xff000000u);

    cpu_.set_memory_handlers(
        [this](uint16_t a) { return read_byte(a); },
        [this](uint16_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_io_handlers(
        [this](uint16_t p) { return read_port(p); },
        [this](uint16_t p, uint8_t v) { write_port(p, v); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });

    apply_model_timing();
}

void Sms::apply_model_timing() {
    if (model_ == Model::Pal) {
        vdp_.video_pal(0);
        lines_per_frame_ = SegaVdp::kLinesPal;
        // ~228 T-states per line at PAL clock / (lines * fps)
        cycles_per_line_ = int(double(kClockPal) / kFpsPal / lines_per_frame_);
    } else {
        vdp_.video_ntsc(0);
        lines_per_frame_ = SegaVdp::kLinesNtsc;
        cycles_per_line_ = int(double(kClockNtsc) / kFpsNtsc / lines_per_frame_);
    }
    if (cycles_per_line_ < 1) cycles_per_line_ = 228;
}

bool Sms::init(const std::string& rom_path, std::string* error) {
    // Optional BIOS from a directory/zip named like the official sets.
    RomLoader loader;
    if (loader.open(rom_path, error)) {
        // Try common BIOS filenames; ignore failures.
        const std::vector<RomEntry> bios_entries = {
            {"mpr-12808.ic2|bios.sms|bios.bin", 0x2000, 0, 0},
        };
        std::vector<uint8_t> bios;
        std::string bios_error;
        if (loader.load(bios_entries, bios, &bios_error) && bios.size() >= 0x2000) {
            std::memset(bios_.data(), 0, sizeof(bios_));
            const size_t banks = std::min(bios.size() / 0x4000, size_t(kMaxBiosBanks));
            if (banks == 0) {
                std::memcpy(bios_[0].data(), bios.data(),
                            std::min(bios.size(), size_t(0x4000)));
                bios_banks_ = 1;
            } else {
                for (size_t b = 0; b < banks; ++b) {
                    std::memcpy(bios_[b].data(), bios.data() + b * 0x4000, 0x4000);
                }
                bios_banks_ = int(banks);
            }
            has_bios_ = true;
        }
        for (const std::string& w : loader.warnings()) warnings_.push_back(w);
    }

    // Cartridge: path may be a raw .sms/.bin, a directory or a zip.
    if (ends_with_ci(rom_path, ".sms") || ends_with_ci(rom_path, ".bin") ||
        ends_with_ci(rom_path, ".rom") || ends_with_ci(rom_path, ".gg")) {
        auto data = read_file_bytes(rom_path, error);
        if (data.empty()) return false;
        if (!load_cartridge(data.data(), data.size(), error)) return false;
    } else {
        // Treat as directory/zip holding a single cartridge dump. Look for
        // common extensions via RomLoader by reading the whole archive is
        // awkward; fall back to load_media semantics: try opening as file first.
        auto data = read_file_bytes(rom_path, nullptr);
        if (!data.empty()) {
            if (!load_cartridge(data.data(), data.size(), error)) return false;
        } else {
            // No cartridge yet — BIOS-only boot is still valid.
            cart_enabled_ = false;
            bios_enabled_ = has_bios_;
            if (!has_bios_) {
                if (error) {
                    *error = "no cartridge and no BIOS found at " + rom_path;
                }
                return false;
            }
        }
    }

    reset();
    return true;
}

bool Sms::load_media(const std::string& path, std::string* error) {
    auto data = read_file_bytes(path, error);
    if (data.empty()) return false;
    if (!load_cartridge(data.data(), data.size(), error)) return false;
    reset();
    return true;
}

bool Sms::load_cartridge(const uint8_t* data, size_t length, std::string* error) {
    if (length == 0) {
        if (error) *error = "empty cartridge image";
        return false;
    }

    // Strip optional 512-byte header used by some dumps.
    size_t offset = 0;
    if ((length % 0x4000) == 512) {
        offset = 512;
        length -= 512;
    }

    for (auto& bank : rom_) bank.fill(0);

    if (length < 0x4000) {
        std::memcpy(rom_[0].data(), data + offset, length);
        rom_banks_ = 1;
    } else {
        rom_banks_ = int(length / 0x4000);
        if (rom_banks_ > kMaxRomBanks) {
            if (error) *error = "cartridge larger than 1 MiB";
            return false;
        }
        for (int b = 0; b < rom_banks_; ++b) {
            std::memcpy(rom_[b].data(), data + offset + size_t(b) * 0x4000, 0x4000);
        }
    }

    const uint32_t crc = crc32_of(data + offset, length);
    detect_mapper(crc);

    if (is_bios_crc(crc)) {
        // Image is itself a BIOS.
        std::memset(bios_.data(), 0, sizeof(bios_));
        bios_banks_ = rom_banks_;
        for (int b = 0; b < rom_banks_; ++b) bios_[b] = rom_[b];
        has_bios_ = true;
        cart_enabled_ = false;
        bios_enabled_ = true;
    } else {
        cart_enabled_ = true;
        bios_enabled_ = has_bios_;
    }
    return true;
}

void Sms::detect_mapper(uint32_t crc) {
    mapper_ = Mapper::Sega;
    switch (crc) {
        // Codemasters
        case 0x58fa27c6:
        case 0xa577ce46:
        case 0x29822980:
        case 0xea5c3a6f:
        case 0x8813514b:
        case 0xb9664ae1:
            mapper_ = Mapper::Codemasters;
            break;
        // Korean
        case 0x565c799f:
        case 0xdbbf4dd1:
        case 0x18fb98a3:
        case 0x97d03541:
        case 0x89b79e77:
        case 0x060d6a7c:
            mapper_ = Mapper::Korean;
            break;
        // 4-pack
        case 0xa67f2a5c:
            mapper_ = Mapper::FourPack;
            break;
        // Cyborg-Z family
        case 0x0a77fa5e:
        case 0xa05258f5:
        case 0x9195c34c:
        case 0x83f0eede:
        case 0x5ac99fc4:
        case 0x445525e2:
        case 0xf89af3cc:
        case 0x77efe84a:
        case 0x06965ed9:
            mapper_ = Mapper::CyborgZ;
            break;
        // Nemesis
        case 0xe316c06d:
            mapper_ = Mapper::Nemesis;
            break;
        default:
            break;
    }
}

void Sms::set_default_banks() {
    rom_bank_[0] = 0;
    if (rom_banks_ > 1) {
        rom_bank_[1] = 1;
        rom_bank_[2] = 2 % rom_banks_;
    } else {
        rom_bank_[1] = 0;
        rom_bank_[2] = 0;
    }
    rom_bank_[3] = 0;

    bios_bank_[0] = 0;
    if (bios_banks_ > 1) {
        bios_bank_[1] = 1;
        bios_bank_[2] = 2 % bios_banks_;
    } else {
        bios_bank_[1] = 0;
        bios_bank_[2] = 0;
    }
    bios_bank_[3] = 0;
    slot2_bank_ = 0;
    bslot2_ram_ = false;
}

void Sms::reset() {
    keys_[0] = 0xff;
    keys_[1] = 0xff;
    push_pause_ = false;
    old_3f_ = 0;
    old_f2_ = 0;
    io_enabled_ = true;
    // Power-on: BIOS maps first when present; games disable it via port $3E.
    bios_enabled_ = has_bios_;
    cart_enabled_ = rom_banks_ > 0 && !bios_enabled_;

    set_default_banks();
    // Alibaba and others expect RAM filled with $F0.
    ram_.fill(0xf0);
    for (auto& bank : slot2_ram_) bank.fill(0);

    audio_.clear();
    audio_accumulator_ = 0;

    vdp_.reset();
    psg_.reset();
    opll_.reset();
    cpu_.reset();
    apply_model_timing();
}

// ---------------------------------------------------------------------------
// Memory map
// ---------------------------------------------------------------------------
uint8_t Sms::read_sega(uint16_t address) const {
    switch (address >> 14) {
        case 0:  // 0000-3FFF
            if (bios_enabled_) {
                if (address < 0x400) return bios_[0][address];
                return bios_[bios_bank_[0] % bios_banks_][address & 0x3fff];
            }
            if (cart_enabled_) {
                if (address < 0x400) return rom_[0][address];
                return rom_[rom_bank_[0] % rom_banks_][address & 0x3fff];
            }
            return 0xff;
        case 1:  // 4000-7FFF
            if (bios_enabled_) return bios_[bios_bank_[1] % bios_banks_][address & 0x3fff];
            return rom_[rom_bank_[1] % rom_banks_][address & 0x3fff];
        case 2:  // 8000-BFFF
            if (bios_enabled_) return bios_[bios_bank_[2] % bios_banks_][address & 0x3fff];
            if (bslot2_ram_ == true) return slot2_ram_[slot2_bank_ & 1][address & 0x3fff];
            return rom_[rom_bank_[2] % rom_banks_][address & 0x3fff];
        default:  // C000-FFFF
            return ram_[address & 0x1fff];
    }
}

void Sms::write_sega(uint16_t address, uint8_t value) {
    if (address < 0x8000) return;
    if (address < 0xc000) {
        if (bslot2_ram_) slot2_ram_[slot2_bank_ & 1][address & 0x3fff] = value;
        return;
    }
    ram_[address & 0x1fff] = value;
    if (address < 0xfffc) return;

    // Banking registers mirrored at $FFFC-$FFFF.
    switch (address & 3) {
        case 0:
            if (cart_enabled_) {
                bslot2_ram_ = (value & 0x08) != 0;
                slot2_bank_ = uint8_t((value & 0x04) >> 2);
            }
            break;
        case 1:
            if (cart_enabled_) rom_bank_[0] = uint8_t(value % rom_banks_);
            if (bios_enabled_) bios_bank_[0] = uint8_t(value % bios_banks_);
            break;
        case 2:
            if (cart_enabled_) rom_bank_[1] = uint8_t(value % rom_banks_);
            if (bios_enabled_) bios_bank_[1] = uint8_t(value % bios_banks_);
            break;
        case 3:
            if (cart_enabled_) rom_bank_[2] = uint8_t(value % rom_banks_);
            if (bios_enabled_) bios_bank_[2] = uint8_t(value % bios_banks_);
            break;
    }
}

uint8_t Sms::read_no_sega(uint16_t address) const {
    switch (address >> 14) {
        case 0:
            return rom_[rom_bank_[0] % rom_banks_][address & 0x3fff];
        case 1:
            return rom_[rom_bank_[1] % rom_banks_][address & 0x3fff];
        case 2:
            return rom_[rom_bank_[2] % rom_banks_][address & 0x3fff];
        default:
            return ram_[address & 0x1fff];
    }
}

void Sms::write_codemasters(uint16_t address, uint8_t value) {
    switch (address) {
        case 0x0000:
            rom_bank_[0] = uint8_t(value % rom_banks_);
            break;
        case 0x4000:
            rom_bank_[1] = uint8_t(value % rom_banks_);
            break;
        case 0x8000:
            rom_bank_[2] = uint8_t(value % rom_banks_);
            break;
        default:
            if (address >= 0xc000) ram_[address & 0x1fff] = value;
            break;
    }
}

void Sms::write_korean(uint16_t address, uint8_t value) {
    switch (address) {
        case 0x4000:
            rom_bank_[1] = uint8_t(value % rom_banks_);
            break;
        case 0xa000:
            rom_bank_[2] = uint8_t(value % rom_banks_);
            break;
        default:
            if (address >= 0xc000) ram_[address & 0x1fff] = value;
            break;
    }
}

void Sms::write_four_pack(uint16_t address, uint8_t value) {
    switch (address) {
        case 0x3ffe:
            rom_bank_[0] = uint8_t(value % rom_banks_);
            rom_bank_[2] = uint8_t(((value & 0x30) + rom_bank_[2]) % rom_banks_);
            break;
        case 0x7fff:
            rom_bank_[1] = uint8_t(value % rom_banks_);
            break;
        case 0xbfff:
            rom_bank_[2] = uint8_t(((rom_bank_[0] & 0x30) + value) % rom_banks_);
            break;
        default:
            if (address >= 0xc000) ram_[address & 0x1fff] = value;
            break;
    }
}

uint8_t Sms::read_cyborgz(uint16_t address) const {
    const int half = rom_banks_ > 0 ? rom_banks_ : 1;
    auto bank_byte = [&](int slot, uint16_t off) -> uint8_t {
        const int bank = rom_bank_[slot] % (half * 2);
        return rom_[bank >> 1][(off & 0x1fff) + 0x2000 * (bank & 1)];
    };
    switch (address >> 13) {
        case 0:
        case 1:
            return rom_[0][address & 0x3fff];
        case 2:
            return bank_byte(0, address);
        case 3:
            return bank_byte(1, address);
        case 4:
            return bank_byte(2, address);
        case 5:
            return bank_byte(3, address);
        default:
            return ram_[address & 0x1fff];
    }
}

void Sms::write_cyborgz(uint16_t address, uint8_t value) {
    const int max = rom_banks_ << 1;
    switch (address) {
        case 0:
            rom_bank_[2] = uint8_t(value % max);
            break;
        case 1:
            rom_bank_[3] = uint8_t(value % max);
            break;
        case 2:
            rom_bank_[0] = uint8_t(value % max);
            break;
        case 3:
            rom_bank_[1] = uint8_t(value % max);
            break;
        default:
            if (address >= 0xc000) ram_[address & 0x1fff] = value;
            break;
    }
}

uint8_t Sms::read_nemesis(uint16_t address) const {
    const int half = rom_banks_ > 0 ? rom_banks_ : 1;
    auto bank_byte = [&](int slot, uint16_t off) -> uint8_t {
        const int bank = rom_bank_[slot] % (half * 2);
        return rom_[bank >> 1][(off & 0x1fff) + 0x2000 * (bank & 1)];
    };
    switch (address >> 13) {
        case 0:
            return rom_[rom_banks_ - 1][(address & 0x1fff) + 0x2000];
        case 1:
            return rom_[0][(address & 0x1fff) + 0x2000];
        case 2:
            return bank_byte(0, address);
        case 3:
            return bank_byte(1, address);
        case 4:
            return bank_byte(2, address);
        case 5:
            return bank_byte(3, address);
        default:
            return ram_[address & 0x1fff];
    }
}

uint8_t Sms::read_byte(uint16_t address) const {
    switch (mapper_) {
        case Mapper::Sega:
            return read_sega(address);
        case Mapper::Codemasters:
        case Mapper::Korean:
        case Mapper::FourPack:
            return read_no_sega(address);
        case Mapper::CyborgZ:
            return read_cyborgz(address);
        case Mapper::Nemesis:
            return read_nemesis(address);
    }
    return 0xff;
}

void Sms::write_byte(uint16_t address, uint8_t value) {
    switch (mapper_) {
        case Mapper::Sega:
            write_sega(address, value);
            break;
        case Mapper::Codemasters:
            write_codemasters(address, value);
            break;
        case Mapper::Korean:
            write_korean(address, value);
            break;
        case Mapper::FourPack:
            write_four_pack(address, value);
            break;
        case Mapper::CyborgZ:
        case Mapper::Nemesis:
            write_cyborgz(address, value);
            break;
    }
}

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------
void Sms::config_io(uint8_t value) {
    // Bits 2/0: region / TH control.
    if ((value & 5) == 5) {
        keys_[1] = uint8_t(keys_[1] & 0x7f);
        if (model_ == Model::Japan) {
            keys_[1] = uint8_t(keys_[1] | (~value & 0x80));
        } else {
            keys_[1] = uint8_t(keys_[1] | (value & 0x80));
        }
        keys_[1] = uint8_t(keys_[1] & 0xbf);
        if (model_ == Model::Japan) {
            keys_[1] = uint8_t(keys_[1] | ((~value & 0x20) << 1));
        } else {
            keys_[1] = uint8_t(keys_[1] | ((value & 0x20) << 1));
        }
    }
    // Rising edge on TH latches H-counter.
    if ((old_3f_ & 0x02) == 0 && (value & 0x02) != 0) vdp_.set_hpos(0);  // latch current
    if ((old_3f_ & 0x08) == 0 && (value & 0x08) != 0) {
        // Prefer the temp latched by the cycle handler if available.
        // (Driver may call set_hpos from on_cycles in a finer port later.)
    }
    old_3f_ = value;
}

uint8_t Sms::read_port(uint16_t port) {
    const uint8_t p = uint8_t(port & 0xff);
    switch (p >> 6) {
        case 0:  // 00-3F: last instruction byte (approximate as $FF)
            return 0xff;
        case 1:  // 40-7F: V/H counter
            if (p & 1) return vdp_.hpos();
            return vdp_.linea_back();
        case 2:  // 80-BF: VDP data / status
            if (p & 1) return uint8_t(vdp_.register_r());
            return vdp_.vram_r();
        default:  // C0-FF: I/O / controllers
            if (io_enabled_) {
                if (p & 1) return keys_[1];
                return keys_[0];
            }
            if (p == 0xf2) return old_f2_;
            return 0xff;
    }
}

void Sms::write_port(uint16_t port, uint8_t value) {
    const uint8_t p = uint8_t(port & 0xff);
    switch (p >> 6) {
        case 0:  // 00-3F
            if (p & 1) {
                config_io(value);
            } else {
                bios_enabled_ = (value & 0x08) == 0;
                io_enabled_ = (value & 0x04) == 0;
                cart_enabled_ = (value & 0xe0) != 0xe0;
            }
            break;
        case 1:  // 40-7F: PSG
            psg_.write(value);
            break;
        case 2:  // 80-BF: VDP
            if (p & 1) {
                vdp_.register_w(value);
            } else {
                vdp_.vram_w(value);
            }
            break;
        default:  // C0-FF: YM2413 address/data + detection
            if (p == 0xf0) {
                opll_.address(value);
            } else if (p == 0xf1) {
                opll_.write(value);
            } else if (p == 0xf2) {
                old_f2_ = value;
            }
            break;
    }
}

void Sms::on_cycles(int cycles) {
    // Approximate H-counter from cycles within the current line.
    // The driver does not track intra-line cycles precisely yet; the VDP
    // latches via set_hpos when TH rises. Audio resampling:
    const uint32_t clock = model_ == Model::Pal ? kClockPal : kClockNtsc;
    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= int64_t(clock)) {
        audio_accumulator_ -= int64_t(clock);
        const int32_t mixed = psg_.update() + opll_.update();
        audio_.push_back(int16_t(std::clamp(mixed, int32_t(-32768), int32_t(32767))));
    }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void Sms::run_frame() {
    const int height = screen_height();
    const int visible_lines = vdp_.video_visible_y_total();
    const int top_border = vdp_.lines_top_border();

    for (int line = 0; line < lines_per_frame_; ++line) {
        cpu_.run(cycles_per_line_);
        vdp_.refresh(line);

        // Map VDP line into the visible framebuffer.
        int fb_y = -1;
        if (line < vdp_.y_pixels()) {
            fb_y = line + top_border;
        } else if (line < vdp_.y_pixels() + (visible_lines - top_border - vdp_.y_pixels())) {
            // Lower border region already filled by VDP line_buffer.
            fb_y = line + top_border;
            if (fb_y >= height) fb_y = -1;
        } else {
            // Upper border is drawn on the last lines of the frame in the
            // original; for simplicity paint top border lines when we pass them.
            const int upper = line - (lines_per_frame_ - top_border);
            if (upper >= 0 && upper < top_border) fb_y = upper;
        }

        if (fb_y >= 0 && fb_y < height) {
            uint32_t* dst = framebuffer_.data() + size_t(fb_y) * kScreenWidth;
            const uint32_t* src = vdp_.line_buffer();
            std::memcpy(dst, src, size_t(kScreenWidth) * sizeof(uint32_t));
        }
    }
}

void Sms::set_inputs(const MachineInputs& inputs) {
    // Active-low controller ports.
    auto set_bit = [](uint8_t& port, int bit, bool pressed) {
        if (pressed) {
            port = uint8_t(port & ~(1 << bit));
        } else {
            port = uint8_t(port | (1 << bit));
        }
    };

    // Port A: P1 up/down/left/right/b1/b2, P2 up/down
    set_bit(keys_[0], 0, inputs.player1.up);
    set_bit(keys_[0], 1, inputs.player1.down);
    set_bit(keys_[0], 2, inputs.player1.left);
    set_bit(keys_[0], 3, inputs.player1.right);
    set_bit(keys_[0], 4, inputs.player1.button1);
    set_bit(keys_[0], 5, inputs.player1.button2);
    set_bit(keys_[0], 6, inputs.player2.up);
    set_bit(keys_[0], 7, inputs.player2.down);

    // Port B: P2 left/right/b1/b2, reset, ...
    set_bit(keys_[1], 0, inputs.player2.left);
    set_bit(keys_[1], 1, inputs.player2.right);
    set_bit(keys_[1], 2, inputs.player2.button1);
    set_bit(keys_[1], 3, inputs.player2.button2);

    // Pause → NMI (coin1 used as pause in the original arcade mapping).
    if (inputs.coin1 || inputs.player1.start) {
        if (!push_pause_) {
            push_pause_ = true;
            cpu_.set_nmi(IrqLine::Pulse);
        }
    } else {
        push_pause_ = false;
    }
}

void Sms::set_dip_switch(int bank, uint8_t value) {
    // bank 0: model 0=PAL, 1=Japan, 2=Export
    if (bank == 0) {
        if (value <= 2) {
            model_ = Model(value);
            apply_model_timing();
        }
    }
}

void Sms::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
