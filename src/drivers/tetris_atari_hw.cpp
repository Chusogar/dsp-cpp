#include "drivers/tetris_atari_hw.h"

#include <algorithm>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const RomEntry kMainRom{"136066-1100.45f", 0x10000, 0, 0x2acbdb09};
const RomEntry kCharRom{"136066-1101.35a", 0x10000, 0, 0x84a1939f};

// pc_x / pc_y from tetris_atari_hw.pas. 4bpp nibble-interleaved bit planes.
inline uint8_t pal3bit(uint8_t bits) {
    bits &= 7;
    return uint8_t((bits << 5) | (bits << 2) | (bits >> 1));
}
inline uint8_t pal2bit(uint8_t bits) {
    bits &= 3;
    return uint8_t((bits << 6) | (bits << 4) | (bits << 2) | bits);
}

}  // namespace

AtariTetris::AtariTetris()
    : cpu_(kCpuClock),
      slapstic_(101, nullptr),
      pokey0_(kCpuClock),
      pokey1_(kCpuClock) {
    framebuffer_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0xff000000u);

    cpu_.set_memory_handlers([this](uint16_t address) { return main_read(address); },
                             [this](uint16_t address, uint8_t value) {
                                 main_write(address, value);
                             });
    cpu_.set_cycle_handler([this](int cycles) { on_sound_cycles(cycles); });

    pokey0_.set_allpot_handler([this](uint8_t) { return uint8_t(in0_ | dsw_a_); });
    pokey1_.set_allpot_handler([this](uint8_t) { return in1_; });
}

bool AtariTetris::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    // Main program: 64 KB split into bank 0 (0x0000-0x3fff), bank 1
    // (0x4000-0x7fff) and the fixed 32 KB tail (0x8000-0xffff).
    std::vector<uint8_t> main_rom(0x10000, 0);
    if (!loader.load({kMainRom}, main_rom, error)) return false;
    std::copy(main_rom.begin(), main_rom.begin() + 0x4000, rom_mem_[0].begin());
    std::copy(main_rom.begin() + 0x4000, main_rom.begin() + 0x8000, rom_mem_[1].begin());
    std::copy(main_rom.begin() + 0x8000, main_rom.end(), memoria_.begin() + 0x8000);

    // Character graphics: 0x800 8x8 tiles, 4bpp.
    std::vector<uint8_t> char_rom(0x10000, 0);
    if (!loader.load({kCharRom}, char_rom, error)) return false;
    {
        GfxLayout layout;
        layout.width = 8;
        layout.height = 8;
        layout.total = 0x800;
        layout.planes = 4;
        layout.char_increment = 8 * 8 * 4;
        layout.plane_offsets = {0, 1, 2, 3};
        layout.x_offsets = {0, 4, 8, 12, 16, 20, 24, 28};
        layout.y_offsets = {0, 32, 64, 96, 128, 160, 192, 224};
        chars_.decode(layout, char_rom);
    }

    nv_ram_.fill(0xff);
    for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);

    reset();
    return true;
}

void AtariTetris::reset() {
    slapstic_.reset();
    rom_bank_ = uint8_t(slapstic_.current_bank() & 1);
    cpu_.reset();
    pokey0_.reset();
    pokey1_.reset();
    in0_ = 0x40;
    in1_ = 0;
    std::copy(rom_mem_[rom_bank_ & 1].begin(), rom_mem_[rom_bank_ & 1].end(),
              memoria_.begin() + 0x4000);
    nvram_write_enable_ = false;
    framebuffer_.assign(size_t(kScreenWidth) * size_t(kScreenHeight), 0xff000000u);
    audio_accumulator_ = 0;
    audio_.clear();
}

void AtariTetris::set_palette(int index, uint8_t value) {
    const uint8_t r = pal3bit(uint8_t(value >> 5));
    const uint8_t g = pal3bit(uint8_t((value >> 2) & 7));
    const uint8_t b = pal2bit(uint8_t(value & 3));
    palette_[size_t(index)] =
        0xff000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

uint8_t AtariTetris::main_read(uint16_t address) {
    if (address <= 0x1fff) return memoria_[address];
    if (address >= 0x2000 && address <= 0x23ff) return buffer_paleta_[address & 0xff];
    if (address >= 0x2400 && address <= 0x27ff) return nv_ram_[address & 0x1ff];
    if (address >= 0x2800 && address <= 0x2bff) {
        if ((address & 0x1f) < 0x10) return pokey0_.read(address & 0x0f);
        return pokey1_.read(address & 0x0f);
    }
    if (address >= 0x4000 && address <= 0x5fff) return memoria_[address];
    if (address >= 0x6000 && address <= 0x7fff) {
        const uint8_t result = memoria_[address];
        const uint8_t new_bank = uint8_t(slapstic_.tweak(address & 0x1fff) & 1);
        if (new_bank != rom_bank_) {
            rom_bank_ = new_bank;
            std::copy(rom_mem_[rom_bank_ & 1].begin(), rom_mem_[rom_bank_ & 1].end(),
                      memoria_.begin() + 0x4000);
        }
        return result;
    }
    if (address >= 0x8000) return memoria_[address];
    return 0;
}

void AtariTetris::main_write(uint16_t address, uint8_t value) {
    if (address <= 0x0fff) {
        memoria_[address] = value;
        return;
    }
    if (address >= 0x1000 && address <= 0x1fff) {
        memoria_[address] = value;  // tile attribute memory, redrawn each frame
        return;
    }
    if (address >= 0x2000 && address <= 0x23ff) {
        const int index = address & 0xff;
        if (buffer_paleta_[size_t(index)] != value) {
            buffer_paleta_[size_t(index)] = value;
            set_palette(index, value);
        }
        return;
    }
    if (address >= 0x2400 && address <= 0x27ff) {
        if (nvram_write_enable_) nv_ram_[address & 0x1ff] = value;
        nvram_write_enable_ = false;
        return;
    }
    if (address >= 0x2800 && address <= 0x2bff) {
        if ((address & 0x1f) < 0x10) pokey0_.write(address & 0x0f, value);
        else pokey1_.write(address & 0x0f, value);
        return;
    }
    if (address >= 0x3000 && address <= 0x33ff) return;  // watchdog
    if (address >= 0x3400 && address <= 0x37ff) {
        nvram_write_enable_ = true;
        return;
    }
    if (address >= 0x3800 && address <= 0x3bff) {
        cpu_.set_irq(IrqLine::Clear);
        return;
    }
    if (address >= 0x3c00 && address <= 0x3fff) return;  // coin counter
    // $4000-$ffff is ROM, writes ignored.
}

void AtariTetris::on_sound_cycles(int cycles) {
    pokey0_.run(cycles);
    pokey1_.run(cycles);
    audio_accumulator_ += int64_t(cycles) * kSampleRate;
    while (audio_accumulator_ >= int64_t(kCpuClock)) {
        audio_accumulator_ -= int64_t(kCpuClock);
        const int32_t sample = pokey0_.update() + pokey1_.update();
        audio_.push_back(int16_t(std::clamp(sample, -32768, 32767)));
    }
}

void AtariTetris::update_video() {
    // Redraw the 64x32 tilemap (opaque put_gfx) into the framebuffer.
    for (int f = 0; f < 0x800; f++) {
        const uint8_t atrib = memoria_[0x1001 + size_t(f) * 2];
        const int color = atrib >> 4;
        const uint16_t nchar =
            uint16_t(memoria_[0x1000 + size_t(f) * 2] + uint16_t((atrib & 7) << 8));
        const int x = f % 64;
        const int y = f / 64;
        const uint8_t* pixels = chars_.element(nchar);
        const int base = color << 4;
        for (int row = 0; row < 8; row++) {
            const int dest_y = y * 8 + row;
            if (dest_y >= kScreenHeight) break;
            const size_t line = size_t(dest_y) * kScreenWidth;
            for (int column = 0; column < 8; column++) {
                const int dest_x = x * 8 + column;
                if (dest_x >= kScreenWidth) break;
                const uint8_t pen = pixels[size_t(row * 8 + column)];
                framebuffer_[line + size_t(dest_x)] = palette_[size_t(base + pen)];
            }
        }
    }
}

void AtariTetris::run_frame() {
    const int main_cycles = int(double(kCpuClock) / kFramesPerSecond / kScanlines + 0.5);
    for (int line = 0; line < kScanlines; line++) {
        switch (line) {
            case 0: in0_ = uint8_t(in0_ | 0x40); break;
            case 48:
            case 112:
            case 176: cpu_.set_irq(IrqLine::Assert); break;
            case 240:
                update_video();
                in0_ = uint8_t(in0_ & ~0x40);
                cpu_.set_irq(IrqLine::Assert);
                break;
            default: break;
        }
        cpu_.run(main_cycles);
    }
}

void AtariTetris::set_inputs(const MachineInputs& inputs) {
    in0_ = 0x40;
    if (inputs.coin1) in0_ = uint8_t(in0_ | 0x02);
    if (inputs.coin2) in0_ = uint8_t(in0_ | 0x01);

    in1_ = 0;
    if (inputs.player1.button1) in1_ |= 0x01;
    if (inputs.player1.down) in1_ |= 0x02;
    if (inputs.player1.right) in1_ |= 0x04;
    if (inputs.player1.left) in1_ |= 0x08;
    if (inputs.player2.button1) in1_ |= 0x10;
    if (inputs.player2.down) in1_ |= 0x20;
    if (inputs.player2.right) in1_ |= 0x40;
    if (inputs.player2.left) in1_ |= 0x80;
}

void AtariTetris::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_a_ = value;
}

void AtariTetris::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp