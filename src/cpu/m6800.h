#pragma once
#include <cstdint>
#include <functional>
#include "cpu/irq_line.h"
namespace dsp {
class M6800 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;
    explicit M6800(uint32_t clock = 894886) : clock_(clock) {}
    void set_read_handler(ReadHandler h) { read_ = std::move(h); }
    void set_write_handler(WriteHandler h) { write_ = std::move(h); }
    void set_cycle_handler(CycleHandler h) { cycle_handler_ = std::move(h); }
    void reset();
    int run(int cycles);
    void set_irq(IrqLine state) { irq_state_ = state; }
    void set_nmi(IrqLine state) { nmi_state_ = state; }
    uint32_t clock() const { return clock_; }
    uint16_t pc() const { return pc_; }
    uint8_t a = 0, b = 0, cc = 0xc0;
    uint16_t x = 0, sp = 0;
private:
    static constexpr uint8_t kC=1,kV=2,kZ=4,kN=8,kI=0x10,kH=0x20;
    uint8_t read(uint16_t a) { return read_ ? read_(a) : 0; }
    void write(uint16_t a, uint8_t v) { if (write_) write_(a, v); }
    uint8_t fetch() { return read(pc_++); }
    uint16_t fetch_word() { uint8_t h=fetch(); return uint16_t((h<<8)|fetch()); }
    void push(uint8_t v) { write(sp--, v); }
    uint8_t pop() { return read(++sp); }
    void push_w(uint16_t v) { push(uint8_t(v)); push(uint8_t(v>>8)); }
    uint16_t pop_w() { uint8_t h=pop(); return uint16_t((h<<8)|pop()); }
    void set_nz(uint8_t v) { cc=uint8_t((cc&~(kN|kZ))|(v&0x80?kN:0)|(v==0?kZ:0)); }
    int exec_one();
    int do_irq();
    uint32_t clock_;
    uint16_t pc_ = 0;
    IrqLine irq_state_ = IrqLine::Clear, nmi_state_ = IrqLine::Clear, nmi_prev_ = IrqLine::Clear;
    bool wait_ = false;
    ReadHandler read_; WriteHandler write_; CycleHandler cycle_handler_;
};
}
