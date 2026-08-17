#include "machine/namco53.h"

namespace dsp {

Namco53xx::Namco53xx(uint32_t clock) : cpu_(Mb88::Type::Mb8843, clock) {
    cpu_.set_k_read([this] { return k_r(); });
    cpu_.set_o_write([this](uint8_t data) { o_w(data); });
    cpu_.set_p_write([this](uint8_t data) { p_w(data); });
    for (int i = 0; i < 4; i++) {
        cpu_.set_r_read(i, [this, i] { return r_r(i); });
        in_[i] = [] { return uint8_t(0); };
    }
    read_k_ = [] { return uint8_t(0); };
    cpu_.set_reset_line(true);
}

bool Namco53xx::load_rom(const std::vector<uint8_t>& rom, std::string* error) {
    if (rom.size() < 0x400) {
        if (error) *error = "namco 53xx ROM is too small";
        return false;
    }
    cpu_.set_program_rom(rom.data(), rom.size());
    return true;
}

void Namco53xx::set_input(int port, PortRead handler) {
    if (port >= 0 && port < 4) in_[port] = std::move(handler);
}

void Namco53xx::reset() {
    port_o_ = 0;
    cpu_.reset();
}

void Namco53xx::set_reset(bool running) { cpu_.set_reset_line(!running); }

void Namco53xx::set_chip_select(bool asserted) {
    cpu_.set_irq(asserted ? IrqLine::Assert : IrqLine::Clear);
}

void Namco53xx::run(int cycles) { cpu_.run(cycles); }

uint8_t Namco53xx::k_r() const { return uint8_t(read_k_() & 0x0f); }

uint8_t Namco53xx::r_r(int port) const { return uint8_t(in_[port]() & 0x0f); }

void Namco53xx::o_w(uint8_t data) { port_o_ = data; }

void Namco53xx::p_w(uint8_t data) {
    if (write_p_) write_p_(data);
}

}  // namespace dsp
