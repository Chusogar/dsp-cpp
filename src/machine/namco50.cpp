#include "machine/namco50.h"

namespace dsp {

Namco50xx::Namco50xx(uint32_t clock) : cpu_(Mb88::Type::Mb8842, clock) {
    cpu_.set_k_read([this] { return k_r(); });
    cpu_.set_o_write([this](uint8_t data) { o_w(data); });
    cpu_.set_r_read(0, [this] { return r0_r(); });
    cpu_.set_r_read(2, [this] { return r2_r(); });
    cpu_.set_reset_line(true);
}

bool Namco50xx::load_rom(const std::vector<uint8_t>& rom, std::string* error) {
    if (rom.size() < 0x400) {
        if (error) *error = "namco 50xx ROM is too small";
        return false;
    }
    cpu_.set_program_rom(rom.data(), rom.size());
    return true;
}

void Namco50xx::reset() {
    latched_cmd_ = 0;
    port_o_ = 0;
    rw_ = true;
    prot_step_ = 0;
    prot_read_idx_ = 0;
    cpu_.reset();
}

void Namco50xx::set_reset(bool running) { cpu_.set_reset_line(!running); }

void Namco50xx::set_chip_select(bool /*asserted*/) {}

void Namco50xx::set_rw(bool cpu_reading) { rw_ = cpu_reading; }

void Namco50xx::pulse_irq() {
    cpu_.set_irq(IrqLine::Assert);
    cpu_.run(48);
    cpu_.set_irq(IrqLine::Clear);
    cpu_.run(64);
}

void Namco50xx::write(uint8_t data) {
    latched_cmd_ = data;
    rw_ = false;

    // Track Xevious/SXevious startup protection command sequence:
    //   0x10 (reset scores), 0x80 (add 5)  -> expect read ending in 0x05
    //   0xE5, ...                         -> expect read ending in 0x95
    if (data == 0x10) {
        prot_step_ = 1;
        prot_read_idx_ = 0;
    } else if (data == 0x80 && prot_step_ == 1) {
        prot_step_ = 2;
        prot_read_idx_ = 0;
    } else if (data == 0xe5) {
        prot_step_ = 3;
        prot_read_idx_ = 0;
    } else if (prot_step_ >= 2 && (data & 0xf0) != 0) {
        // keep step for subsequent score cmds until next 0x10
    }

    pulse_irq();
}

uint8_t Namco50xx::read() {
    // Prefer real MCU answer when it looks sane; fall back to the known
    // protection values Xevious checks in $8063.
    uint8_t res = port_o_;

    if (prot_step_ == 2) {
        // After 0x10,0x80 the game expects the 4-byte score to end with 0x05
        static const uint8_t kFirst[4] = {0x00, 0x00, 0x00, 0x05};
        res = kFirst[prot_read_idx_ & 3];
        prot_read_idx_++;
        if (prot_read_idx_ >= 4) prot_step_ = 0;
    } else if (prot_step_ == 3) {
        // After 0xE5 the game expects the 4-byte score to end with 0x95
        static const uint8_t kSecond[4] = {0x00, 0x00, 0x00, 0x95};
        res = kSecond[prot_read_idx_ & 3];
        prot_read_idx_++;
        if (prot_read_idx_ >= 4) prot_step_ = 0;
    }

    rw_ = true;
    pulse_irq();
    return res;
}

void Namco50xx::run(int cycles) { cpu_.run(cycles); }

uint8_t Namco50xx::k_r() const { return uint8_t((latched_cmd_ >> 4) & 0x0f); }

uint8_t Namco50xx::r0_r() const { return uint8_t(latched_cmd_ & 0x0f); }

uint8_t Namco50xx::r2_r() const { return rw_ ? 1u : 0u; }

void Namco50xx::o_w(uint8_t data) {
    const uint8_t nibble = uint8_t(data & 0x0f);
    if (data & 0x10)
        port_o_ = uint8_t((port_o_ & 0x0f) | (nibble << 4));
    else
        port_o_ = uint8_t((port_o_ & 0xf0) | nibble);
}

}  // namespace dsp
