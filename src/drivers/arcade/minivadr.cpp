#include "drivers/arcade/minivadr.h"

#include "core/rom_loader.h"

namespace dsp {
namespace {

// Copied from the `..._rom` tables of minivadr_hw.pas. A crc of 0 skips the check and
// alternative file names are separated by '|'.
const std::vector<RomEntry> kMainRoms = {
    {"d26-01.bin", 0x2000, 0x0000, 0xa96c823d},
};


}  // namespace

Minivadr::Minivadr() : cpu_(kCpuClock), psg_(kSoundClock) {
    framebuffer_.assign(size_t(kScreenWidth) * kScreenHeight, 0xff000000u);
    cpu_.set_memory_handlers([this](uint16_t address) { return read_byte(address); },
                             [this](uint16_t address, uint8_t value) { write_byte(address, value); });
    cpu_.set_cycle_handler([this](int cycles) { on_cycles(cycles); });
}

bool Minivadr::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;

    std::vector<uint8_t> main_rom;
    if (!loader.load(kMainRoms, main_rom, error)) return false;
    std::copy(main_rom.begin(), main_rom.end(), memory_.begin());
    
    for (const std::string& warning : loader.warnings()) warnings_.push_back(warning);
    reset();
    return true;
}

void Minivadr::reset() {
    cpu_.reset();    
}

uint8_t Minivadr::read_byte(uint16_t address) {
    // TODO: follow the `..._getbyte` memory map of minivadr_hw.pas.
    return memory_[address];
}

void Minivadr::write_byte(uint16_t address, uint8_t value) {
    // TODO: follow the `..._putbyte` memory map of minivadr_hw.pas.
    if (address >= 0x8000) memory_[address] = value;
}

void Minivadr::on_cycles(int cycles) {
    audio_accumulator_ += int64_t(cycles) * AY8910::kSampleRate;
    while (audio_accumulator_ >= int64_t(kCpuClock)) {
        audio_accumulator_ -= int64_t(kCpuClock);
        audio_.push_back(int16_t(psg_.update()));
    }
}

void Minivadr::decode_graphics(const std::vector<uint8_t>& gfx_rom) {
    //chars_.decode(char_layout(), gfx_rom);
}

void Minivadr::build_palette(const std::vector<uint8_t>& prom) {
    palette_[0] = 0xff000000u;
	palette_[1] = 0xffffffffu;
}

void Minivadr::update_video() {
    // TODO: draw the tilemaps and the sprites into framebuffer_.
}

void Minivadr::run_frame() {
    const int cycles_per_line = int(kCpuClock / kFramesPerSecond / kScanlines);
    for (int line = 0; line < kScanlines; line++) {
        if (line == kScreenHeight) {
            cpu_.set_irq(IrqLine::Hold);
            update_video();
        }
        cpu_.run(cycles_per_line);
    }
}

void Minivadr::set_inputs(const MachineInputs& inputs) {
    in0_ = 0xff;
    in1_ = 0xff;
    if (inputs.coin1) in0_ &= 0xfe;
    if (inputs.player1.left) in0_ &= 0xfd;
    if (inputs.player1.right) in0_ &= 0xfb;
    if (inputs.player1.button1) in0_ &= 0xf7;
    if (inputs.player1.start) in1_ &= 0xfe;
}

void Minivadr::set_dip_switch(int bank, uint8_t value) {
    if (bank == 0) dsw_ = value;
}

void Minivadr::drain_audio(std::vector<int16_t>& out) {
    //out.insert(out.end(), audio_.begin(), audio_.end());
    //audio_.clear();
}

}  // namespace dsp
