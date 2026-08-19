#include "machine/namco52.h"

namespace dsp {

Namco52xx::Namco52xx(uint32_t clock) : cpu_(Mb88::Type::Mb8843, clock) {
    cpu_.set_k_read([this] { return k_r(); });
    cpu_.set_o_write([this](uint8_t data) { o_w(data); });
    cpu_.set_p_write([this](uint8_t data) { p_w(data); });
    cpu_.set_si_read([this] { return si_r(); });
    cpu_.set_r_read(0, [this] { return r0_r(); });
    cpu_.set_r_read(1, [this] { return r1_r(); });
    cpu_.set_r_write(2, [this](uint8_t data) { r2_w(data); });
    cpu_.set_r_write(3, [this](uint8_t data) { r3_w(data); });
    cpu_.set_reset_line(true);
}

bool Namco52xx::load_rom(const std::vector<uint8_t>& rom, std::string* error) {
    if (rom.size() < 0x400) {
        if (error) *error = "namco 52xx ROM is too small";
        return false;
    }
    cpu_.set_program_rom(rom.data(), rom.size());
    return true;
}

void Namco52xx::reset() {
    latched_cmd_ = 0;
    address_ = 0;
    dac_ = 0;
    cpu_.reset();
}

void Namco52xx::set_reset(bool running) { cpu_.set_reset_line(!running); }

void Namco52xx::set_chip_select(bool asserted) {
    cpu_.set_irq(asserted ? IrqLine::Assert : IrqLine::Clear);
}

void Namco52xx::write(uint8_t data) { latched_cmd_ = data; }

void Namco52xx::run(int cycles) { cpu_.run(cycles); }

uint8_t Namco52xx::r0_r() const {
    const uint8_t value = rom_read_ ? rom_read_(address_) : 0xff;
    return uint8_t(value & 0x0f);
}

uint8_t Namco52xx::r1_r() const {
    const uint8_t value = rom_read_ ? rom_read_(address_) : 0xff;
    return uint8_t(value >> 4);
}

void Namco52xx::r2_w(uint8_t data) {
    address_ = uint16_t((address_ & 0xfff0) | (data & 0x0f));
}

void Namco52xx::r3_w(uint8_t data) {
    address_ = uint16_t((address_ & 0xff0f) | (uint16_t(data & 0x0f) << 4));
}

void Namco52xx::o_w(uint8_t data) { address_ = uint16_t((address_ & 0x00ff) | (uint16_t(data) << 8)); }

}  // namespace dsp
