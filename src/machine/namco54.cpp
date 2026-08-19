#include "machine/namco54.h"

namespace dsp {

Namco54xx::Namco54xx(uint32_t clock) : cpu_(Mb88::Type::Mb8844, clock) {
    cpu_.set_k_read([this] { return k_r(); });
    cpu_.set_o_write([this](uint8_t data) { o_w(data); });
    cpu_.set_r_read(0, [this] { return r0_r(); });
    cpu_.set_r_write(1, [this](uint8_t data) { r1_w(data); });
    cpu_.set_reset_line(true);
}

bool Namco54xx::load_rom(const std::vector<uint8_t>& rom, std::string* error) {
    if (rom.size() < 0x400) {
        if (error) *error = "namco 54xx ROM is too small";
        return false;
    }
    cpu_.set_program_rom(rom.data(), rom.size());
    return true;
}

void Namco54xx::reset() {
    latched_cmd_ = 0;
    chanl1_ = 0;
    chanl2_ = 0;
    chanl3_ = 0;
    cpu_.reset();
}

void Namco54xx::set_reset(bool running) { cpu_.set_reset_line(!running); }

void Namco54xx::set_chip_select(bool asserted) {
    cpu_.set_irq(asserted ? IrqLine::Assert : IrqLine::Clear);
}

void Namco54xx::write(uint8_t data) { latched_cmd_ = data; }

void Namco54xx::run(int cycles) { cpu_.run(cycles); }

void Namco54xx::o_w(uint8_t data) {
    chanl3_ = uint8_t(data & 0x0f);
    chanl2_ = uint8_t(data >> 4);
}

void Namco54xx::r1_w(uint8_t data) { chanl1_ = uint8_t(data & 0x0f); }

}  // namespace dsp
