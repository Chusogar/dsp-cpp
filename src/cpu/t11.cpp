#include "cpu/t11.h"

// The opcode bodies are a direct port of MAME's t11 core and use its macro
// shorthand for registers and memory, so the unused `op` parameter of the
// operand-less opcodes stays as it is there.
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace dsp {

// registers of various sizes
#define REGD(x) int(reg_.w[x])
#define REGW(x) reg_.w[x]
#define REGB(x) reg_.b[(x) * 2]

// PC, SP, and PSW definitions
#define SP REGW(6)
#define PC REGW(7)
#define SPD REGD(6)
#define PCD REGD(7)
#define PSW psw_

namespace {

// Initial PC selected by the top three bits of the mode register.
constexpr uint16_t kInitialPc[8] = {0xc000, 0x8000, 0x4000, 0x2000,
                                    0x1000, 0x0000, 0xf600, 0xf400};

struct IrqTableEntry {
    uint8_t priority;
    uint8_t vector;
};

// Priority and (non-vectored) vector for every combination of CP lines.
constexpr IrqTableEntry kIrqTable[16] = {
    {0 << 5, 0000}, {4 << 5, 0070}, {4 << 5, 0064}, {4 << 5, 0060},
    {5 << 5, 0134}, {5 << 5, 0130}, {5 << 5, 0124}, {5 << 5, 0120},
    {6 << 5, 0114}, {6 << 5, 0110}, {6 << 5, 0104}, {6 << 5, 0100},
    {7 << 5, 0154}, {7 << 5, 0150}, {7 << 5, 0144}, {7 << 5, 0140}};

}  // namespace

T11::T11(uint32_t clock, uint16_t initial_mode) : clock_(clock) {
    set_initial_mode(initial_mode);
}

void T11::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void T11::set_initial_mode(uint16_t mode) {
    initial_mode_ = mode;
    initial_pc_ = kInitialPc[mode >> 13];
}

void T11::reset() {
    for (auto& value : reg_.w) value = 0;

    // initial SP is 376 octal, or 0xfe
    SP = 0376;
    PC = initial_pc_;
    // PSW starts off at highest priority
    PSW = 0340;

    wait_state_ = 0;
    power_fail_ = false;
    bus_error_ = false;
    ext_halt_ = false;
    cp_state_ = 0;
    pulse_mask_ = 0;
    icount_ = 0;
}

int T11::ROPCODE() {
    PC &= 0xfffe;
    const int val = RWORD(PC);
    PC += 2;
    return val;
}

int T11::RBYTE(int addr) {
    const uint16_t word = uint16_t(read_(uint16_t(addr) & 0xfffe));
    return (addr & 1) ? (word >> 8) : (word & 0xff);
}

void T11::WBYTE(int addr, int data) {
    const uint16_t address = uint16_t(addr) & 0xfffe;
    if (addr & 1) write_(address, uint16_t(uint8_t(data) << 8), 0xff00);
    else write_(address, uint16_t(uint8_t(data)), 0x00ff);
}

int T11::RWORD(int addr) { return read_(uint16_t(addr) & 0xfffe); }

void T11::WWORD(int addr, int data) {
    write_(uint16_t(addr) & 0xfffe, uint16_t(data), 0xffff);
}

void T11::PUSH(int val) {
    SP -= 2;
    WWORD(SPD, val);
}

int T11::POP() {
    const int result = RWORD(SPD);
    SP += 2;
    return result;
}

// flag definitions
#define CFLAG 1
#define VFLAG 2
#define ZFLAG 4
#define NFLAG 8

#define GET_C (PSW & CFLAG)
#define GET_V (PSW & VFLAG)
#define GET_Z (PSW & ZFLAG)
#define GET_N (PSW & NFLAG)

#define CLR_C (PSW &= ~CFLAG)
#define CLR_V (PSW &= ~VFLAG)
#define CLR_Z (PSW &= ~ZFLAG)
#define CLR_N (PSW &= ~NFLAG)

#define SET_C (PSW |= CFLAG)
#define SET_V (PSW |= VFLAG)
#define SET_Z (PSW |= ZFLAG)
#define SET_N (PSW |= NFLAG)

void T11::t11_check_irqs() {
    // HLT is nonmaskable
    if (ext_halt_) {
        ext_halt_ = false;
        PUSH(PSW);
        PUSH(PC);
        PC = uint16_t(initial_pc_ + 4);
        PSW = 0340;
        icount_ -= 114;
        wait_state_ = 0;
        return;
    }

    if (bus_error_) {
        bus_error_ = false;
        take_interrupt(0004);
        return;
    }
    if (power_fail_) {
        power_fail_ = false;
        take_interrupt(0024);
        return;
    }

    // compare the priority of the CP interrupt to the PSW
    const IrqTableEntry& irq = kIrqTable[cp_state_ & 15];
    if (irq.priority > (PSW & 0340)) {
        cp_state_ &= uint8_t(~pulse_mask_);
        pulse_mask_ = 0;
        take_interrupt(irq.vector);
    }
}

void T11::take_interrupt(uint8_t vector) {
    const uint16_t new_pc = uint16_t(RWORD(vector));
    const uint16_t new_psw = uint16_t(RWORD(vector + 2));

    PUSH(PSW);
    PUSH(PC);
    PC = new_pc;
    PSW = uint8_t(new_psw);

    icount_ -= 114;
    wait_state_ = 0;
}

void T11::set_irq(int line, IrqLine state) {
    if (line < CP0_LINE || line > CP3_LINE) return;
    const uint8_t mask = uint8_t(1 << line);
    switch (state) {
        case IrqLine::Clear:
            cp_state_ &= uint8_t(~mask);
            pulse_mask_ &= uint8_t(~mask);
            break;
        case IrqLine::Pulse:
            cp_state_ |= mask;
            pulse_mask_ |= mask;
            break;
        case IrqLine::Assert:
        case IrqLine::Hold:
            cp_state_ |= mask;
            pulse_mask_ &= uint8_t(~mask);
            break;
    }
}

// the master opcode table and the actual opcode implementations
#include "cpu/t11table.inc"
#include "cpu/t11ops.inc"

int T11::run(int cycles) {
    icount_ = cycles;

    while (icount_ > 0) {
        t11_check_irqs();
        if (wait_state_) {
            icount_ = 0;
            break;
        }
        const uint16_t op = uint16_t(ROPCODE());
        (this->*s_opcode_table[op >> 3])(op);
    }

    return cycles - icount_;
}

}  // namespace dsp
