#include "machine/namco51.h"

namespace dsp {

Namco51xx::Namco51xx(uint32_t clock) : cpu_(Mb88::Type::Mb8843, clock) {
    cpu_.set_k_read([this] { return k_r(); });
    cpu_.set_o_write([this](uint8_t data) { o_w(data); });
    cpu_.set_p_write([this](uint8_t data) { p_w(data); });
    for (int i = 0; i < 4; i++) {
        cpu_.set_r_read(i, [this, i] { return r_r(i); });
        in_[i] = [] { return uint8_t(0); };
    }
    cpu_.set_reset_line(true);
}

bool Namco51xx::load_rom(const std::vector<uint8_t>& rom, std::string* error) {
    if (rom.size() < 0x400) {
        if (error) *error = "namco 51xx ROM is too small";
        return false;
    }
    cpu_.set_program_rom(rom.data(), rom.size());
    return true;
}

void Namco51xx::set_input(int port, PortRead handler) {
    if (port >= 0 && port < 4) in_[port] = std::move(handler);
}

void Namco51xx::reset() {
    port_o_ = 0;
    rw_ = 0;
    cpu_.reset();
}

void Namco51xx::set_reset(bool running) { cpu_.set_reset_line(!running); }

void Namco51xx::set_chip_select(bool asserted) {
    cpu_.set_irq(asserted ? IrqLine::Assert : IrqLine::Clear);
}

void Namco51xx::set_rw(bool read) { rw_ = read ? 1 : 0; }

void Namco51xx::write(uint8_t data) { port_o_ = data; }

void Namco51xx::vblank(bool state) {
    // Falling /TC on entering vblank clocks the 51xx coin timer.
    cpu_.set_tc(!state);
}

void Namco51xx::run(int cycles) {
    // Galaga's Z80 is 3.072 MHz; the MB8843 is clocked at 1.536 MHz.
    // The Mb88 core consumes clock cycles directly, so execute half as many
    // MCU cycles for each host-Z80 cycle batch.
    cpu_.run(cycles / 2);
}

uint8_t Namco51xx::k_r() const { return uint8_t((rw_ << 3) | (port_o_ & 0x07)); }

uint8_t Namco51xx::r_r(int port) const { return uint8_t(in_[port]() & 0x0f); }

void Namco51xx::o_w(uint8_t data) { port_o_ = data; }

void Namco51xx::p_w(uint8_t data) {
    if (write_p_) write_p_(data);
}

}  // namespace dsp
