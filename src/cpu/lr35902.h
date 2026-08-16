#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace dsp {

// Sharp LR35902 (the Game Boy's CPU, a Z80/8080 hybrid), ported from
// lr35902.pas. No IX/IY, no shadow register set, no ED-prefixed block
// instructions; adds 8-bit-relative LD (HL+/-),A / LDH forms and STOP.
//
// Unlike Z80, interrupts are not a single IRQ line: the Game Boy has five
// independent sources (VBlank/LCD STAT/Timer/Serial/Joypad), each with its
// own enable bit (IE, $ffff) and request/flag bit (IF, $ff0f). Since IE/IF
// are ordinary memory locations on this machine (unlike the Z80's separate
// port space), the driver reads/writes them through the normal memory
// handlers and pokes the five booleans below directly, mirroring how
// gb.pas's leer_io/escribe_io $0f and $ff handlers touch lr35902_0's public
// fields instead of going through a CPU method.
class LR35902 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;

    explicit LR35902(uint32_t clock);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    // Called after every instruction with the number of elapsed T states
    // (matches gb_despues_instruccion's estados_t, but delivered once per
    // instruction rather than pre-chopped into 4-cycle steps; see gameboy.cpp
    // for how the driver reconstructs the same STAT-timing checkpoints from
    // that with threshold-crossing tests instead of exact equality).
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }

    void reset();
    // Runs until at least `cycles` T states have elapsed, returns the amount executed.
    int run(int cycles);

    // Extra T-states charged to the current (or next) instruction, matching
    // lr35902.pas's `estados_demas`. Used by CGB general-purpose VRAM DMA,
    // which stalls the CPU for `(220 shr speed) + 8*(length/16)`.
    void add_stall_cycles(int n) { stall_cycles_ += n; }

    uint32_t clock() const { return clock_; }

    // Registers, public to keep driver hooks simple (matches Z80's style).
    uint8_t a = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    uint16_t sp = 0, pc = 0;
    bool fz = false, fn = false, fh = false, fc = false;

    // CGB double-speed mode.
    uint8_t speed = 0;         // 0 = normal, 1 = double speed
    bool change_speed = false;   // KEY1 bit 0 (armed by writing to $ff4d)
    bool changed_speed = false;  // set for one instruction after STOP toggles speed

    bool ime = false;
    bool halted() const { return halt_; }
    uint32_t interrupts_serviced = 0;  // debug counter
    std::function<void(uint16_t)> on_fetch;  // debug: called with PC before every instruction

    // The five interrupt sources; IE (enable) and IF (request) bits, kept as
    // plain booleans and driven directly by the driver's I/O handlers.
    bool vblank_ena = false, lcdstat_ena = false, timer_ena = false;
    bool serial_ena = false, joystick_ena = false;
    bool vblank_req = false, lcdstat_req = false, timer_req = false;
    bool serial_req = false, joystick_req = false;

private:
    uint8_t rd(uint16_t addr) const { return read_(addr); }
    void wr(uint16_t addr, uint8_t value) const { write_(addr, value); }
    uint8_t fetch8();
    uint16_t fetch16();
    void push16(uint16_t value);
    uint16_t pop16();

    uint8_t get_f() const;
    void set_f(uint8_t value);

    // ALU helpers, named after their Pascal counterparts.
    uint8_t inc8(uint8_t v);
    uint8_t dec8(uint8_t v);
    void add_a(uint8_t v);
    void adc_a(uint8_t v);
    void sub_a(uint8_t v);
    void sbc_a(uint8_t v);
    void and_a(uint8_t v);
    void or_a(uint8_t v);
    void xor_a(uint8_t v);
    void cp_a(uint8_t v);
    void add_hl(uint16_t v);
    uint8_t rlc(uint8_t v);
    uint8_t rrc(uint8_t v);
    uint8_t rl(uint8_t v);
    uint8_t rr(uint8_t v);
    uint8_t sla(uint8_t v);
    uint8_t sra(uint8_t v);
    uint8_t srl(uint8_t v);
    uint8_t swap(uint8_t v);
    void bit(uint8_t n, uint8_t v);

    void exec(uint8_t opcode);
    void exec_cb();
    uint8_t reg8(uint8_t index) const;
    void set_reg8(uint8_t index, uint8_t value);
    int service_interrupt();  // returns extra T states if one was serviced, else 0

    ReadHandler read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;
    uint32_t clock_;

    bool halt_ = false;
    bool after_ei_ = false;  // suppresses the interrupt check for one instruction after EI
    int extra_cycles_ = 0;   // conditional-branch bonus / CB-prefixed instruction cost
    int stall_cycles_ = 0;   // estados_demas: GDMA and similar machine stalls

    static const uint8_t kCycles[256];
    static const uint8_t kCyclesCb[256];
};

}  // namespace dsp
