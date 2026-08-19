#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cpu/mb88xx.h"

namespace dsp {

// Namco 51xx: MB8843 MCU used as I/O + coin management.
class Namco51xx {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;

    explicit Namco51xx(uint32_t clock);

    bool load_rom(const std::vector<uint8_t>& rom, std::string* error);
    void set_input(int port, PortRead handler);
    void set_output(PortWrite handler) { write_p_ = std::move(handler); }

    void reset();
    void set_reset(bool running);  // LS259 Q1: high = run, low = reset
    void set_chip_select(bool asserted);
    void set_rw(bool read);
    void write(uint8_t data);
    uint8_t read() const { return port_o_; }
    void vblank(bool state);
    void run(int cycles);

    uint16_t debug_pc() const { return cpu_.pc(); }

private:
    uint8_t k_r() const;
    uint8_t r_r(int port) const;
    void o_w(uint8_t data);
    void p_w(uint8_t data);

    Mb88 cpu_;
    uint8_t port_o_ = 0;
    uint8_t rw_ = 0;
    PortRead in_[4];
    PortWrite write_p_;
};

}  // namespace dsp
