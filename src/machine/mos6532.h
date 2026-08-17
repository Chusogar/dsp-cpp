#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// MOS 6532 RIOT (128 bytes RAM + two ports + timer), enough for Star Wars.
class Mos6532 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;
    using IrqCallback = std::function<void(IrqLine)>;

    void set_pa(PortRead in, PortWrite out = nullptr);
    void set_pb(PortRead in, PortWrite out = nullptr);
    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }

    void reset();
    uint8_t ram_read(uint8_t offset) const { return ram_[offset & 0x7f]; }
    void ram_write(uint8_t offset, uint8_t value) { ram_[offset & 0x7f] = value; }
    uint8_t io_read(uint8_t offset);
    void io_write(uint8_t offset, uint8_t value);

    void tick(int cycles);
    void set_pa_in_bit(int bit, bool level);

private:
    void update_pa();
    void update_pb();
    void update_irq();
    uint8_t timer_value() const;
    void timer_start(uint8_t data);

    std::array<uint8_t, 0x80> ram_{};
    uint8_t pa_in_ = 0xff, pa_out_ = 0, pa_ddr_ = 0;
    uint8_t pb_in_ = 0xff, pb_out_ = 0, pb_ddr_ = 0;

    int timer_count_ = 0;
    int timer_shift_ = 10;
    bool timer_counting_ = true;
    bool ie_timer_ = false;
    bool irq_timer_ = false;

    PortRead pa_in_cb_, pb_in_cb_;
    PortWrite pa_out_cb_, pb_out_cb_;
    IrqCallback irq_cb_;
};

}  // namespace dsp
