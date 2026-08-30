#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// Konami-1 CPU (052001), ported from konami.pas.
// Same register file as the 6809, but a different opcode map and indexing mode.
// Clock is the chip input; internally the core runs at clock/4 (like the Pascal port).
class KonamiCpu {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;
    using SetLinesHandler = std::function<void(uint8_t)>;

    struct Flags {
        bool e = false, f = false, h = false, i = false;
        bool n = false, z = false, v = false, c = false;
    };

    explicit KonamiCpu(uint32_t clock);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }
    void set_lines_handler(SetLinesHandler handler) { set_lines_ = std::move(handler); }

    void reset();
    int run(int cycles);

    void set_irq(IrqLine state) { irq_state_ = state; }
    void set_firq(IrqLine state) { firq_state_ = state; }
    void set_nmi(IrqLine state);

    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }
    void set_pc(uint16_t pc) { pc_ = pc; }

    uint8_t a = 0, b = 0, dp = 0;
    uint16_t x = 0, y = 0, u = 0, s = 0;
    Flags cc;

private:
    uint16_t d() const { return uint16_t((uint16_t(a) << 8) | b); }
    void set_d(uint16_t value) {
        a = uint8_t(value >> 8);
        b = uint8_t(value);
    }

    uint8_t read(uint16_t address) { return read_(address); }
    void write(uint16_t address, uint8_t value) { write_(address, value); }
    uint16_t read_word(uint16_t address);
    void write_word(uint16_t address, uint16_t value);
    uint8_t fetch();
    uint16_t fetch_word();

    void push_s(uint8_t value);
    uint8_t pop_s();
    void push_sw(uint16_t value);
    uint16_t pop_sw();
    void push_u(uint8_t value);
    uint8_t pop_u();
    void push_uw(uint16_t value);
    uint16_t pop_uw();

    uint8_t get_cc() const;
    void set_cc(uint8_t value);

    int call_irq();
    int call_firq();
    int call_nmi();

    uint16_t get_indexed();
    void trf(uint8_t value);
    void trf_ex(uint8_t value);

    uint8_t op_neg(uint8_t v);
    uint8_t op_com(uint8_t v);
    uint8_t op_lsr(uint8_t v);
    uint8_t op_ror(uint8_t v);
    uint8_t op_asr(uint8_t v);
    uint8_t op_asl(uint8_t v);
    uint8_t op_rol(uint8_t v);
    uint8_t op_dec(uint8_t v);
    uint8_t op_inc(uint8_t v);
    void op_tst(uint8_t v);
    uint8_t op_sub8(uint8_t left, uint8_t right);
    uint16_t op_sub16(uint16_t left, uint16_t right);
    uint8_t op_sbc(uint8_t left, uint8_t right);
    uint8_t op_and(uint8_t left, uint8_t right);
    uint8_t op_eor(uint8_t left, uint8_t right);
    uint8_t op_adc(uint8_t left, uint8_t right);
    uint8_t op_or(uint8_t left, uint8_t right);
    uint8_t op_add8(uint8_t left, uint8_t right);
    uint16_t op_add16(uint16_t left, uint16_t right);
    uint8_t op_ld_st8(uint8_t value);
    uint16_t op_ld_st16(uint16_t value);
    uint16_t op_neg16(uint16_t v);
    uint16_t op_inc16(uint16_t v);
    uint16_t op_dec16(uint16_t v);
    void op_tst16(uint16_t v);
    uint8_t op_abs8(uint8_t v);
    uint16_t op_abs16(uint16_t v);
    uint16_t op_lsrd(uint16_t v, uint8_t count);
    uint16_t op_asrd(uint16_t v, uint8_t count);
    uint16_t op_asld(uint16_t v, uint8_t count);
    uint16_t op_lsr16(uint16_t v);
    uint16_t op_asr16(uint16_t v);
    uint16_t op_asl16(uint16_t v);

    uint32_t clock_;
    uint16_t pc_ = 0;
    int extra_cycles_ = 0;
    IrqLine irq_state_ = IrqLine::Clear;
    IrqLine firq_state_ = IrqLine::Clear;
    IrqLine nmi_state_ = IrqLine::Clear;
    IrqLine nmi_latched_ = IrqLine::Clear;

    ReadHandler read_;
    WriteHandler write_;
    CycleHandler cycle_handler_;
    SetLinesHandler set_lines_;
};

}  // namespace dsp
