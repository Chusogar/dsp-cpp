#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace dsp {

// Sharp LR35902 (Game Boy / Game Boy Color CPU), ported from
// leniad/dsp-emulator src/cpu/lr35902.pas.
//
// Differences vs Z80 that matter for the driver:
//   - No I/O space (everything is memory-mapped)
//   - No alternate register set, no IX/IY, no R/I
//   - Flags are only Z, N, H, C (bits 7,6,5,4 of F; low nibble always 0)
//   - Fixed interrupt vectors: VBlank $40, LCD STAT $48, Timer $50,
//     Serial $58, Joypad $60
//   - STOP ($10) used for GBC double-speed switch
//   - HALT leaves PC pointing at the next opcode (re-fetched while halted)
class LR35902 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    // Called after every instruction (or 4 T-state slice) with the number of
    // T-states that just elapsed. Used by the GB driver for timers / DMA / LCD.
    using CycleHandler = std::function<void(int)>;

    explicit LR35902(uint32_t clock = 4194304);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }

    void reset();

    // Run until at least `cycles` T-states have elapsed. Returns the amount
    // actually executed (always a multiple of 4).
    int run(int cycles);

    // Interrupt enable / request bits mirror the Game Boy IE / IF registers.
    // The driver sets the request flags; the CPU consumes them when IME is set.
    void set_vblank_enable(bool v) { vblank_ena_ = v; }
    void set_lcdstat_enable(bool v) { lcdstat_ena_ = v; }
    void set_timer_enable(bool v) { timer_ena_ = v; }
    void set_serial_enable(bool v) { serial_ena_ = v; }
    void set_joystick_enable(bool v) { joystick_ena_ = v; }

    void request_vblank() { vblank_req_ = true; }
    void request_lcdstat() { lcdstat_req_ = true; }
    void request_timer() { timer_req_ = true; }
    void request_serial() { serial_req_ = true; }
    void request_joystick() { joystick_req_ = true; }

    bool vblank_request() const { return vblank_req_; }
    bool lcdstat_request() const { return lcdstat_req_; }
    bool timer_request() const { return timer_req_; }
    bool serial_request() const { return serial_req_; }
    bool joystick_request() const { return joystick_req_; }

    void clear_vblank() { vblank_req_ = false; }
    void clear_lcdstat() { lcdstat_req_ = false; }
    void clear_timer() { timer_req_ = false; }
    void clear_serial() { serial_req_ = false; }
    void clear_joystick() { joystick_req_ = false; }

    // GBC double-speed: STOP with change_speed_ set toggles speed_ (0=normal, 1=double).
    void arm_speed_switch() { change_speed_ = true; }
    uint8_t speed() const { return speed_; }
    bool changed_speed() const { return changed_speed_; }
    void clear_changed_speed() { changed_speed_ = false; }

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }
    void set_pc(uint16_t value) { pc_ = value; }

    bool ime() const { return ime_; }
    bool halted() const { return halted_; }

    // Public registers for debugging / driver hooks (same style as Z80).
    uint8_t a = 0;
    uint8_t f = 0;  // only ZNHC used: Z=0x80 N=0x40 H=0x20 C=0x10
    uint8_t b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    uint16_t sp = 0;

    static constexpr uint8_t ZF = 0x80;
    static constexpr uint8_t NF = 0x40;
    static constexpr uint8_t HF = 0x20;
    static constexpr uint8_t CF = 0x10;

private:
    uint8_t rd(uint16_t addr) const { return read_(addr); }
    void wr(uint16_t addr, uint8_t value) { write_(addr, value); }
    uint8_t fetch();
    uint16_t fetch16();
    void push(uint16_t value);
    uint16_t pop();

    uint16_t bc() const { return uint16_t((b << 8) | c); }
    uint16_t de() const { return uint16_t((d << 8) | e); }
    uint16_t hl() const { return uint16_t((h << 8) | l); }
    void set_bc(uint16_t v) {
        b = uint8_t(v >> 8);
        c = uint8_t(v);
    }
    void set_de(uint16_t v) {
        d = uint8_t(v >> 8);
        e = uint8_t(v);
    }
    void set_hl(uint16_t v) {
        h = uint8_t(v >> 8);
        l = uint8_t(v);
    }

    bool flag_z() const { return (f & ZF) != 0; }
    bool flag_n() const { return (f & NF) != 0; }
    bool flag_h() const { return (f & HF) != 0; }
    bool flag_c() const { return (f & CF) != 0; }
    void set_z(bool v) {
        if (v)
            f |= ZF;
        else
            f &= uint8_t(~ZF);
    }
    void set_n(bool v) {
        if (v)
            f |= NF;
        else
            f &= uint8_t(~NF);
    }
    void set_h(bool v) {
        if (v)
            f |= HF;
        else
            f &= uint8_t(~HF);
    }
    void set_c(bool v) {
        if (v)
            f |= CF;
        else
            f &= uint8_t(~CF);
    }

    void add_a(uint8_t value);
    void adc_a(uint8_t value);
    void sub_a(uint8_t value);
    void sbc_a(uint8_t value);
    void and_a(uint8_t value);
    void xor_a(uint8_t value);
    void or_a(uint8_t value);
    void cp_a(uint8_t value);
    uint8_t inc8(uint8_t value);
    uint8_t dec8(uint8_t value);
    void add_hl(uint16_t value);

    uint8_t rlc(uint8_t value);
    uint8_t rrc(uint8_t value);
    uint8_t rl(uint8_t value);
    uint8_t rr(uint8_t value);
    uint8_t sla(uint8_t value);
    uint8_t sra(uint8_t value);
    uint8_t swap(uint8_t value);
    uint8_t srl(uint8_t value);
    void bit(uint8_t index, uint8_t value);

    void exec_cb();
    int service_interrupts();  // returns extra T-states charged
    void charge(int tstates);

    uint32_t clock_;
    ReadHandler read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;

    int cycles_ = 0;    // T-states of the current instruction (+ extras)
    int executed_ = 0;  // total in this run() call
    bool after_ei_ = false;
    bool ime_ = false;
    bool halted_ = false;

    uint8_t speed_ = 0;  // 0 = normal, 1 = double (GBC)
    bool change_speed_ = false;
    bool changed_speed_ = false;

    bool vblank_ena_ = false, lcdstat_ena_ = false, timer_ena_ = false;
    bool serial_ena_ = false, joystick_ena_ = false;
    bool vblank_req_ = false, lcdstat_req_ = false, timer_req_ = false;
    bool serial_req_ = false, joystick_req_ = false;

    uint16_t pc_ = 0;
};

}  // namespace dsp
