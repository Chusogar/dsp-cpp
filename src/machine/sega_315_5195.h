#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"

namespace dsp {

// Sega 315-5195 memory mapper / I/O, ported from sega_315_5195.pas.
class Sega3155195 {
public:
    using SoundLatch = std::function<void(uint8_t)>;
    using OpenBus = std::function<uint8_t()>;
    using BusRead = std::function<uint16_t(uint32_t)>;
    using BusWrite = std::function<void(uint32_t, uint16_t)>;
    using ResetLine = std::function<void(IrqLine)>;
    using IrqLineFn = std::function<void(int level, IrqLine state)>;

    void set_sound_latch(SoundLatch handler) { sound_latch_ = std::move(handler); }
    void set_open_bus(OpenBus handler) { open_bus_ = std::move(handler); }
    void set_bus_handlers(BusRead read, BusWrite write) {
        bus_read_ = std::move(read);
        bus_write_ = std::move(write);
    }
    void set_reset_handler(ResetLine handler) { reset_line_ = std::move(handler); }
    void set_irq_handler(IrqLineFn handler) { irq_line_ = std::move(handler); }

    void reset();
    void rebuild_map();

    uint8_t read_reg(uint8_t address);
    void write_reg(uint8_t address, uint8_t value);

    uint32_t dirs_start(int region) const { return dirs_start_[size_t(region)]; }
    uint32_t dirs_end(int region) const { return dirs_end_[size_t(region)]; }
    bool contains(int region, uint32_t address) const {
        return address >= dirs_start_[size_t(region)] && address < dirs_end_[size_t(region)];
    }

    void set_from_sound(uint8_t value) { from_sound_ = value; }
    uint8_t from_sound() const { return from_sound_; }

private:
    std::array<uint8_t, 0x20> regs_{};
    std::array<uint32_t, 8> dirs_start_{};
    std::array<uint32_t, 8> dirs_end_{};
    uint8_t from_sound_ = 0;

    SoundLatch sound_latch_;
    OpenBus open_bus_;
    BusRead bus_read_;
    BusWrite bus_write_;
    ResetLine reset_line_;
    IrqLineFn irq_line_;
};

}  // namespace dsp
