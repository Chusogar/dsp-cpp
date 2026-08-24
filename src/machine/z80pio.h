#pragma once

#include <cstdint>
#include <functional>

namespace dsp {

// Zilog Z80 PIO (parallel I/O), ported from z80pio.pas.
//
// Two independent 8 bit ports (A and B), each configurable in one of four
// modes (output, input, bidirectional -- port A only, or bit control), with
// its own ready/strobe handshake and interrupt logic. Register access uses
// the classic "cd/ba" address layout used by most Z80 PIO designs: bit 0 of
// the offset selects port A/B, bit 1 selects the control/data register.
class Z80Pio {
public:
    static constexpr int kPortA = 0;
    static constexpr int kPortB = 1;

    using InHandler = std::function<uint8_t()>;
    using OutHandler = std::function<void(uint8_t)>;
    using ReadyHandler = std::function<void(bool)>;
    using InterruptHandler = std::function<void(bool)>;

    void reset();

    // Wires the external handlers for one port. Any of them can be null.
    void set_port_handlers(int port, InHandler in, OutHandler out, ReadyHandler ready);
    void set_interrupt_handler(InterruptHandler handler) { interrupt_ = std::move(handler); }

    // CPU-facing register access, "cd/ba" layout: offset bit0 selects
    // port A(0)/B(1), bit1 selects control(1)/data(0).
    uint8_t read(uint16_t offset);
    void write(uint16_t offset, uint8_t value);

    // Direct data-register access some boards use to read a port's output
    // latch without going through the CPU-visible register window.
    uint8_t port_output(int port) const { return port_[port & 1].output; }

    // External strobe lines (ASTB/BSTB).
    void strobe_a(bool state) { strobe(kPortA, state); }
    void strobe_b(bool state) { strobe(kPortB, state); }

private:
    enum Mode : uint8_t { kModeOutput = 0, kModeInput = 1, kModeBidirectional = 2, kModeBitControl = 3 };
    enum NextWord : uint8_t { kNextAny = 0, kNextIor = 1, kNextMask = 2 };

    struct Port {
        uint8_t mode = kModeInput;
        uint8_t next_control_word = kNextAny;
        uint8_t input = 0;
        uint8_t output = 0;
        uint8_t ior = 0;
        bool rdy = false;
        bool stb = false;
        bool ie = false;
        bool ip = false;
        bool ius = false;
        uint8_t icw = 0;
        uint8_t vector = 0;
        uint8_t mask = 0xff;
        bool match = false;

        InHandler in_func;
        OutHandler out_func;
        ReadyHandler ready_func;
    };

    void reset_port(int port);
    void set_mode(int port, uint8_t mode);
    void set_rdy(int port, bool state);
    void check_interrupts();
    bool interrupt_signalled(int port);
    void strobe(int port, bool state);
    void trigger_interrupt(int port);
    uint8_t control_read();
    void control_write(int port, uint8_t value);
    uint8_t data_read(int port);
    void data_write(int port, uint8_t value);

    Port port_[2];
    InterruptHandler interrupt_;
};

}  // namespace dsp
