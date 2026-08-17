#include "cpu/m6502.h"

#include <algorithm>

namespace dsp {
namespace {

// Addressing mode and cycle tables from m6502.pas.
const uint8_t kMode[256] = {
    0x0, 0xa, 0x0, 0xa, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x1, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5,
    0x0, 0xa, 0x0, 0xa, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x1, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5,
    0x0, 0xa, 0x0, 0xa, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x1, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5,
    0x0, 0xa, 0x0, 0xa, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x1, 0x0, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5,
    0xd, 0xa, 0x1, 0xa, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x1, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x9, 0x9, 0x0, 0x6, 0x0, 0x6, 0x2, 0x4, 0x5, 0x5,
    0x1, 0xa, 0x1, 0x0, 0x7, 0x7, 0x7, 0x0, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x0,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x9, 0x0, 0x0, 0x6, 0x0, 0x0, 0x5, 0x5, 0x6, 0x0,
    0x1, 0xa, 0x1, 0xa, 0x7, 0x7, 0x7, 0x0, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5,
    0x1, 0xa, 0x1, 0xa, 0x7, 0x7, 0x7, 0x0, 0x0, 0x1, 0x0, 0x1, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0x0, 0xb, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x6, 0x5, 0x5, 0x4, 0x5};

const uint8_t kCycles[256] = {
    7, 6, 1, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6,
    2, 5, 1, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 1, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6,
    2, 5, 1, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 1, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6,
    2, 5, 1, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 1, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6,
    2, 5, 1, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    2, 6, 1, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4,
    2, 5, 1, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    2, 5, 1, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    2, 6, 2, 7, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6,
    2, 5, 1, 7, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7};

// G65SC02 / 65C02 addressing (Lynx Mikey core). Unused opcodes are NOPs of
// the documented 1/2/3-byte sizes; BBR/BBS/RMB/SMB are absent on the G65SC02.
const uint8_t kModeCmos[256] = {
    0x0, 0xa, 0x1, 0x0, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0xc, 0x0, 0x7, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x0, 0x2, 0x5, 0x5, 0x5,
    0x0, 0xa, 0x1, 0x0, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0xc, 0x0, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x0, 0x5, 0x5, 0x5, 0x5,
    0x0, 0xa, 0x1, 0x0, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0xc, 0x0, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x0, 0x2, 0x5, 0x5, 0x5,
    0x0, 0xa, 0x1, 0x0, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x0, 0x0, 0x2, 0x2, 0x2,
    0xd, 0xb, 0xc, 0x0, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x0, 0x5, 0x5, 0x5, 0x5,
    0xd, 0xa, 0x1, 0x0, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0xc, 0x0, 0x8, 0x8, 0x9, 0x9, 0x0, 0x6, 0x0, 0x0, 0x2, 0x5, 0x4, 0x5,
    0x1, 0xa, 0x1, 0x0, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0xc, 0x0, 0x8, 0x8, 0x9, 0x9, 0x0, 0x6, 0x0, 0x0, 0x5, 0x5, 0x6, 0x6,
    0x1, 0xa, 0x1, 0x0, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0xc, 0x0, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x0, 0x2, 0x5, 0x5, 0x5,
    0x1, 0xa, 0x1, 0x0, 0x7, 0x7, 0x7, 0x7, 0x0, 0x1, 0x0, 0x0, 0x2, 0x2, 0x2, 0x2,
    0xd, 0xb, 0xc, 0x0, 0x8, 0x8, 0x8, 0x8, 0x0, 0x6, 0x0, 0x0, 0x2, 0x5, 0x5, 0x5};

const uint8_t kCyclesCmos[256] = {
    7, 6, 2, 1, 5, 3, 5, 1, 3, 2, 2, 1, 6, 4, 6, 1,
    2, 5, 5, 1, 5, 4, 6, 1, 2, 4, 2, 1, 6, 4, 7, 1,
    6, 6, 2, 1, 3, 3, 5, 1, 4, 2, 2, 1, 4, 4, 6, 1,
    2, 5, 5, 1, 4, 4, 6, 1, 2, 4, 2, 1, 4, 4, 7, 1,
    6, 6, 2, 1, 3, 3, 5, 1, 3, 2, 2, 1, 3, 4, 6, 1,
    2, 5, 5, 1, 4, 4, 6, 1, 2, 4, 3, 1, 8, 4, 7, 1,
    6, 6, 2, 1, 3, 3, 5, 1, 4, 2, 2, 1, 6, 4, 6, 1,
    2, 5, 5, 1, 4, 4, 6, 1, 2, 4, 4, 1, 6, 4, 7, 1,
    3, 6, 2, 1, 3, 3, 3, 1, 2, 2, 2, 1, 4, 4, 4, 1,
    2, 6, 5, 1, 4, 4, 4, 1, 2, 5, 2, 1, 4, 5, 5, 1,
    2, 6, 2, 1, 3, 3, 3, 1, 2, 2, 2, 1, 4, 4, 4, 1,
    2, 5, 5, 1, 4, 4, 4, 1, 2, 4, 2, 1, 4, 4, 4, 1,
    2, 6, 2, 1, 3, 3, 5, 1, 2, 2, 2, 1, 4, 4, 6, 1,
    2, 5, 5, 1, 4, 4, 6, 1, 2, 4, 3, 1, 4, 4, 7, 1,
    2, 6, 2, 1, 3, 3, 5, 1, 2, 2, 2, 1, 4, 4, 6, 1,
    2, 5, 5, 1, 4, 4, 6, 1, 2, 4, 4, 1, 4, 4, 7, 1};

// Store/read-modify-write opcodes never take the page crossing penalty.
bool no_page_penalty_x(uint8_t opcode) {
    return opcode == 0x1f || opcode == 0x3f || opcode == 0x5f || opcode == 0x7f;
}

bool no_page_penalty_y(uint8_t opcode) {
    return opcode == 0x1b || opcode == 0x3b || opcode == 0x5b || opcode == 0x7b || opcode == 0x99;
}

bool no_page_penalty_iy(uint8_t opcode) {
    return opcode == 0x13 || opcode == 0x33 || opcode == 0x53 || opcode == 0x73 ||
           opcode == 0x93 || opcode == 0x91;
}

}  // namespace

M6502::M6502(uint32_t clock, Type type) : clock_(clock), type_(type) {}

void M6502::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void M6502::set_nmi(IrqLine state) {
    nmi_request_ = state;
    if (state == IrqLine::Clear) nmi_state_ = IrqLine::Clear;
}

void M6502::reset() {
    pc_ = uint16_t(read(0xfffc) | (read(0xfffd) << 8));
    a = 0;
    x = 0;
    y = 0;
    sp = 0xfd;
    p = Flags{};
    p.irq_disable = true;
    after_ei_ = false;
    halted_ = false;
    irq_request_ = IrqLine::Clear;
    nmi_request_ = IrqLine::Clear;
    nmi_state_ = IrqLine::Clear;
    stolen_cycles_ = 0;
}

uint8_t M6502::get_flags() const {
    uint8_t value = 0;
    if (p.n) value |= 0x80;
    if (p.v) value |= 0x40;
    if (p.t) value |= 0x20;
    if (p.brk) value |= 0x10;
    if (p.dec) value |= 0x08;
    if (p.irq_disable) value |= 0x04;
    if (p.z) value |= 0x02;
    if (p.c) value |= 0x01;
    return value;
}

void M6502::set_flags(uint8_t value) {
    p.n = (value & 0x80) != 0;
    p.v = (value & 0x40) != 0;
    p.t = (value & 0x20) != 0;
    p.brk = (value & 0x10) != 0;
    p.dec = (value & 0x08) != 0;
    p.irq_disable = (value & 0x04) != 0;
    p.z = (value & 0x02) != 0;
    p.c = (value & 0x01) != 0;
}

void M6502::push(uint8_t value) {
    write(uint16_t(0x100 + sp), value);
    sp -= 1;
}

uint8_t M6502::pop() {
    sp += 1;
    return read(uint16_t(0x100 + sp));
}

int M6502::call_nmi() {
    if (nmi_state_ != IrqLine::Clear) return 0;
    push(uint8_t(pc_ >> 8));
    push(uint8_t(pc_));
    push(uint8_t((get_flags() & 0xef) | 0x20));
    p.irq_disable = true;
    if (cmos_) p.dec = false;
    pc_ = uint16_t(read(0xfffa) | (read(0xfffb) << 8));
    if (nmi_request_ == IrqLine::Pulse) nmi_request_ = IrqLine::Clear;
    if (nmi_request_ == IrqLine::Assert) nmi_state_ = IrqLine::Assert;
    return 7;
}

int M6502::call_irq() {
    if (p.irq_disable) return 0;
    push(uint8_t(pc_ >> 8));
    push(uint8_t(pc_));
    push(uint8_t((get_flags() & 0xef) | 0x20));
    p.irq_disable = true;
    if (cmos_) p.dec = false;
    pc_ = uint16_t(read(0xfffe) | (read(0xffff) << 8));
    if (irq_request_ == IrqLine::Hold) irq_request_ = IrqLine::Clear;
    return 7;
}

void M6502::set_nz(uint8_t value) {
    p.z = value == 0;
    p.n = (value & 0x80) != 0;
}

void M6502::adc(uint8_t value) {
    if (p.dec && type_ != Type::Nes) {
        const uint8_t carry = p.c ? 1 : 0;
        const uint16_t binary = uint16_t(a + value + carry);
        p.v = ((~(a ^ value)) & (a ^ binary) & 0x80) != 0;
        uint16_t low = uint16_t((a & 0x0f) + (value & 0x0f) + carry);
        if (low > 9) low += 6;
        uint16_t high = uint16_t((a >> 4) + (value >> 4) + (low > 15 ? 1 : 0));
        if (high > 9) high += 6;
        p.c = high > 15;
        a = uint8_t((high << 4) | (low & 0x0f));
        if (cmos_) {
            extra_cycles_ += 1;
            set_nz(a);
        } else {
            p.z = (binary & 0xff) == 0;
            p.n = !p.z && (high & 8) != 0;
        }
    } else {
        const uint16_t result = uint16_t(a + value + (p.c ? 1 : 0));
        p.v = ((~(a ^ value)) & (a ^ result) & 0x80) != 0;
        p.c = (result & 0xff00) != 0;
        a = uint8_t(result);
        p.z = a == 0;
        p.n = !p.z && (a & 0x80) != 0;
    }
}

void M6502::sbc(uint8_t value) {
    if (p.dec && type_ != Type::Nes) {
        const uint8_t carry = p.c ? 0 : 1;
        const uint16_t diff = uint16_t(a - value - carry);
        p.v = ((a ^ value) & (a ^ diff) & 0x80) != 0;
        p.c = (diff & 0xff00) == 0;
        int8_t low = int8_t((a & 0x0f) - (value & 0x0f) - carry);
        if (low < 0) low = int8_t(low - 6);
        int8_t high = int8_t((a >> 4) - (value >> 4) - (low < 0 ? 1 : 0));
        if (high < 0) high = int8_t(high - 6);
        a = uint8_t((uint8_t(high) << 4) | (uint8_t(low) & 0x0f));
        if (cmos_) {
            extra_cycles_ += 1;
            set_nz(a);
        } else {
            p.z = (diff & 0xff) == 0;
            p.n = !p.z && (diff & 0x80) != 0;
        }
    } else {
        const uint16_t diff = uint16_t(a - value - (p.c ? 0 : 1));
        p.v = ((a ^ value) & (a ^ diff) & 0x80) != 0;
        p.c = (diff & 0xff00) == 0;
        a = uint8_t(diff);
        p.z = a == 0;
        p.n = !p.z && (a & 0x80) != 0;
    }
}

void M6502::compare(uint8_t reg, uint8_t value) {
    const uint16_t diff = uint16_t(reg - value);
    p.c = (diff & 0xff00) == 0;
    p.z = (diff & 0xff) == 0;
    p.n = (diff & 0x80) != 0;
}

void M6502::branch(bool condition, uint8_t offset) {
    if (!condition) return;
    const uint16_t target = uint16_t(pc_ + int8_t(offset));
    extra_cycles_ += ((target ^ pc_) & 0xff00) != 0 ? 2 : 1;
    pc_ = target;
}

int M6502::run(int cycles) {
    int executed = 0;
    while (executed < cycles) {
        extra_cycles_ = 0;
        if (halted_) {
            const int step = std::min(cycles - executed, 8);
            executed += step;
            if (cycle_handler_) cycle_handler_(step);
            continue;
        }
        if (!after_ei_) {
            if (nmi_request_ != IrqLine::Clear) extra_cycles_ = call_nmi();
            else if (irq_request_ != IrqLine::Clear) extra_cycles_ = call_irq();
        }
        after_ei_ = false;

        const uint8_t instruction = fetch();
        const uint8_t* const modes = cmos_ ? kModeCmos : kMode;
        const uint8_t* const cycle_table = cmos_ ? kCyclesCmos : kCycles;
        uint8_t offset = 0;
        switch (modes[instruction]) {
            case 0x0:  // implicit
                break;
            case 0x1:  // immediate
                address_ = pc_;
                pc_ += 1;
                break;
            case 0x2:  // absolute
                address_ = uint16_t(read(pc_) | (read(uint16_t(pc_ + 1)) << 8));
                pc_ += 2;
                break;
            case 0x4:  // absolute,X without page crossing penalty
                address_ = uint16_t(read(pc_) + (read(uint16_t(pc_ + 1)) << 8) + x);
                pc_ += 2;
                break;
            case 0x5: {  // absolute,X
                const uint16_t base = uint16_t(read(pc_) | (read(uint16_t(pc_ + 1)) << 8));
                if (!no_page_penalty_x(instruction) && (((base + x) ^ base) & 0xff00) != 0) {
                    extra_cycles_ += 1;
                }
                address_ = uint16_t(base + x);
                pc_ += 2;
                break;
            }
            case 0x6: {  // absolute,Y
                const uint16_t base = uint16_t(read(pc_) | (read(uint16_t(pc_ + 1)) << 8));
                if (!no_page_penalty_y(instruction) && (((base + y) ^ base) & 0xff00) != 0) {
                    extra_cycles_ += 1;
                }
                address_ = uint16_t(base + y);
                pc_ += 2;
                break;
            }
            case 0x7:  // zero page
                address_ = fetch();
                break;
            case 0x8:  // zero page,X
                address_ = uint8_t(fetch() + x);
                break;
            case 0x9:  // zero page,Y
                address_ = uint8_t(fetch() + y);
                break;
            case 0xa: {  // (zero page,X)
                const uint8_t pointer = uint8_t(fetch() + x);
                address_ = uint16_t(read(pointer) | (read(uint8_t(pointer + 1)) << 8));
                break;
            }
            case 0xb: {  // (zero page),Y
                const uint8_t pointer = fetch();
                const uint16_t base = uint16_t(read(pointer) | (read(uint8_t(pointer + 1)) << 8));
                if (!no_page_penalty_iy(instruction) && (((base + y) ^ base) & 0xff00) != 0) {
                    extra_cycles_ += 1;
                }
                address_ = uint16_t(base + y);
                break;
            }
            case 0xc: {  // (zero page)
                const uint8_t pointer = fetch();
                address_ = uint16_t(read(pointer) | (read(uint8_t(pointer + 1)) << 8));
                break;
            }
            default:  // relative
                offset = fetch();
                break;
        }

        switch (instruction) {
            case 0x00: {  // brk
                pc_ += 1;
                push(uint8_t(pc_ >> 8));
                push(uint8_t(pc_));
                push(uint8_t(get_flags() | 0x30));
                p.brk = true;
                p.irq_disable = true;
                if (cmos_) p.dec = false;
                pc_ = uint16_t(read(0xfffe) | (read(0xffff) << 8));
                break;
            }
            case 0x01: case 0x05: case 0x09: case 0x0d:
            case 0x11: case 0x15: case 0x19: case 0x1d:  // ora
                a |= read(address_);
                set_nz(a);
                break;
            case 0x12:  // ora (zp), 65C02
                if (!cmos_) break;
                a |= read(address_);
                set_nz(a);
                break;
            case 0x06: case 0x0e: case 0x16: case 0x1e: {  // asl
                const uint8_t value = read(address_);
                p.c = (value & 0x80) != 0;
                const uint8_t result = uint8_t(value << 1);
                write(address_, result);
                set_nz(result);
                break;
            }
            case 0x0a:  // asl a
                p.c = (a & 0x80) != 0;
                a = uint8_t(a << 1);
                set_nz(a);
                break;
            case 0x08:  // php
                push(uint8_t(get_flags() | 0x30));
                break;
            case 0x10:  // bpl
                branch(!p.n, offset);
                break;
            case 0x18:  // clc
                p.c = false;
                break;
            case 0x20: {  // jsr
                const uint16_t target = uint16_t(read(pc_) | (read(uint16_t(pc_ + 1)) << 8));
                pc_ += 1;
                push(uint8_t(pc_ >> 8));
                push(uint8_t(pc_));
                pc_ = target;
                break;
            }
            case 0x21: case 0x25: case 0x29: case 0x2d:
            case 0x31: case 0x35: case 0x39: case 0x3d:  // and
                a &= read(address_);
                set_nz(a);
                break;
            case 0x32:  // and (zp), 65C02
                if (!cmos_) break;
                a &= read(address_);
                set_nz(a);
                break;
            case 0x24: case 0x2c: {  // bit
                const uint8_t value = read(address_);
                p.z = (a & value) == 0;
                p.n = (value & 0x80) != 0;
                p.v = (value & 0x40) != 0;
                break;
            }
            case 0x34: case 0x3c: {  // bit zp,x / abs,x, 65C02
                if (!cmos_) break;
                const uint8_t value = read(address_);
                p.z = (a & value) == 0;
                p.n = (value & 0x80) != 0;
                p.v = (value & 0x40) != 0;
                break;
            }
            case 0x89:  // bit immediate, 65C02: only Z, N/V unchanged
                if (cmos_) p.z = (a & read(address_)) == 0;
                break;
            case 0x26: case 0x2e: case 0x36: case 0x3e: {  // rol
                const uint8_t value = read(address_);
                const uint8_t result = uint8_t((value << 1) | (p.c ? 1 : 0));
                p.c = (value & 0x80) != 0;
                write(address_, result);
                set_nz(result);
                break;
            }
            case 0x2a: {  // rol a
                const uint8_t result = uint8_t((a << 1) | (p.c ? 1 : 0));
                p.c = (a & 0x80) != 0;
                a = result;
                set_nz(a);
                break;
            }
            case 0x28:  // plp
                set_flags(pop());
                after_ei_ = true;
                break;
            case 0x30:  // bmi
                branch(p.n, offset);
                break;
            case 0x38:  // sec
                p.c = true;
                break;
            case 0x40:  // rti
                set_flags(pop());
                pc_ = pop();
                pc_ |= uint16_t(pop() << 8);
                break;
            case 0x41: case 0x45: case 0x49: case 0x4d:
            case 0x51: case 0x55: case 0x59: case 0x5d:  // eor
                a ^= read(address_);
                set_nz(a);
                break;
            case 0x52:  // eor (zp), 65C02
                if (!cmos_) break;
                a ^= read(address_);
                set_nz(a);
                break;
            case 0x46: case 0x4e: case 0x56: case 0x5e: {  // lsr
                const uint8_t value = read(address_);
                p.c = (value & 1) != 0;
                const uint8_t result = uint8_t(value >> 1);
                write(address_, result);
                set_nz(result);
                break;
            }
            case 0x4a:  // lsr a
                p.c = (a & 1) != 0;
                a = uint8_t(a >> 1);
                set_nz(a);
                break;
            case 0x48:  // pha
                push(a);
                break;
            case 0x4c:  // jmp
                pc_ = address_;
                break;
            case 0x50:  // bvc
                branch(!p.v, offset);
                break;
            case 0x58:  // cli
                p.irq_disable = false;
                after_ei_ = true;
                break;
            case 0x60:  // rts
                pc_ = pop();
                pc_ |= uint16_t(pop() << 8);
                pc_ += 1;
                break;
            case 0x61: case 0x65: case 0x69: case 0x6d:
            case 0x71: case 0x75: case 0x79: case 0x7d:  // adc
                adc(read(address_));
                break;
            case 0x72:  // adc (zp), 65C02
                if (cmos_) adc(read(address_));
                break;
            case 0x66: case 0x6e: case 0x76: case 0x7e: {  // ror
                const uint8_t value = read(address_);
                const uint8_t result = uint8_t((value >> 1) | (p.c ? 0x80 : 0));
                p.c = (value & 1) != 0;
                write(address_, result);
                set_nz(result);
                break;
            }
            case 0x6a: {  // ror a
                const uint8_t result = uint8_t((a >> 1) | (p.c ? 0x80 : 0));
                p.c = (a & 1) != 0;
                a = result;
                set_nz(a);
                break;
            }
            case 0x68:  // pla
                a = pop();
                set_nz(a);
                break;
            case 0x6c: {  // jmp (indirect)
                const uint16_t pointer = uint16_t(read(pc_) | (read(uint16_t(pc_ + 1)) << 8));
                if (cmos_) {
                    pc_ = uint16_t(read(pointer) | (read(uint16_t(pointer + 1)) << 8));
                } else {
                    const uint16_t high = uint16_t((pointer & 0xff00) | uint8_t(pointer + 1));
                    pc_ = uint16_t(read(pointer) | (read(high) << 8));
                }
                break;
            }
            case 0x7c:  // jmp (abs,x), 65C02
                if (cmos_) {
                    pc_ = uint16_t(read(address_) | (read(uint16_t(address_ + 1)) << 8));
                }
                break;
            case 0x70:  // bvs
                branch(p.v, offset);
                break;
            case 0x78:  // sei
                p.irq_disable = true;
                after_ei_ = true;
                break;
            case 0x81: case 0x85: case 0x8d: case 0x91:
            case 0x95: case 0x99: case 0x9d:  // sta
                write(address_, a);
                break;
            case 0x92:  // sta (zp), 65C02
                if (cmos_) write(address_, a);
                break;
            case 0x84: case 0x8c: case 0x94:  // sty
                write(address_, y);
                break;
            case 0x86: case 0x8e: case 0x96:  // stx
                write(address_, x);
                break;
            case 0x88:  // dey
                y -= 1;
                set_nz(y);
                break;
            case 0x8a:  // txa
                a = x;
                set_nz(a);
                break;
            case 0x90:  // bcc
                branch(!p.c, offset);
                break;
            case 0x98:  // tya
                a = y;
                set_nz(a);
                break;
            case 0x9a:  // txs
                sp = x;
                break;
            case 0xa0: case 0xa4: case 0xac: case 0xb4: case 0xbc:  // ldy
                y = read(address_);
                set_nz(y);
                break;
            case 0xa1: case 0xa5: case 0xa9: case 0xad:
            case 0xb1: case 0xb5: case 0xb9: case 0xbd:  // lda
                a = read(address_);
                set_nz(a);
                break;
            case 0xb2:  // lda (zp), 65C02
                if (!cmos_) break;
                a = read(address_);
                set_nz(a);
                break;
            case 0xa2: case 0xa6: case 0xae: case 0xb6: case 0xbe:  // ldx
                x = read(address_);
                set_nz(x);
                break;
            case 0xa8:  // tay
                y = a;
                set_nz(y);
                break;
            case 0xaa:  // tax
                x = a;
                set_nz(x);
                break;
            case 0xb0:  // bcs
                branch(p.c, offset);
                break;
            case 0xb8:  // clv
                p.v = false;
                break;
            case 0xba:  // tsx
                x = sp;
                set_nz(x);
                break;
            case 0xc0: case 0xc4: case 0xcc:  // cpy
                compare(y, read(address_));
                break;
            case 0xc1: case 0xc5: case 0xc9: case 0xcd:
            case 0xd1: case 0xd5: case 0xd9: case 0xdd:  // cmp
                compare(a, read(address_));
                break;
            case 0xd2:  // cmp (zp), 65C02
                if (cmos_) compare(a, read(address_));
                break;
            case 0xc6: case 0xce: case 0xd6: case 0xde: {  // dec
                const uint8_t result = uint8_t(read(address_) - 1);
                write(address_, result);
                set_nz(result);
                break;
            }
            case 0xc8:  // iny
                y += 1;
                set_nz(y);
                break;
            case 0xca:  // dex
                x -= 1;
                set_nz(x);
                break;
            case 0xd0:  // bne
                branch(!p.z, offset);
                break;
            case 0xd8:  // cld
                p.dec = false;
                break;
            case 0xe0: case 0xe4: case 0xec:  // cpx
                compare(x, read(address_));
                break;
            case 0xe1: case 0xe5: case 0xe9: case 0xeb: case 0xed:
            case 0xf1: case 0xf5: case 0xf9: case 0xfd:  // sbc (0xeb unofficial)
                sbc(read(address_));
                break;
            case 0xf2:  // sbc (zp), 65C02
                if (cmos_) sbc(read(address_));
                break;
            case 0xe6: case 0xee: case 0xf6: case 0xfe: {  // inc
                const uint8_t result = uint8_t(read(address_) + 1);
                write(address_, result);
                set_nz(result);
                break;
            }
            case 0xe8:  // inx
                x += 1;
                set_nz(x);
                break;
            case 0xf0:  // beq
                branch(p.z, offset);
                break;
            case 0xf8:  // sed
                p.dec = true;
                break;
            case 0x02: case 0x42:  // kil / jam
                pc_ -= 1;
            case 0x04: case 0x0c: {  // tsb zp/abs, 65C02
                if (!cmos_) break;
                const uint8_t value = read(address_);
                p.z = (value & a) == 0;
                write(address_, uint8_t(value | a));
                break;
            }
            case 0x14: case 0x1c: {  // trb zp/abs, 65C02
                if (!cmos_) break;
                const uint8_t value = read(address_);
                p.z = (value & a) == 0;
                write(address_, uint8_t(value & uint8_t(~a)));
                break;
            }
            case 0x1a:  // inc a
                if (!cmos_) break;
                a = uint8_t(a + 1);
                set_nz(a);
                break;
            case 0x3a:  // dec a
                if (!cmos_) break;
                a = uint8_t(a - 1);
                set_nz(a);
                break;
            case 0x5a:  // phy
                if (cmos_) push(y);
                break;
            case 0x7a:  // ply
                if (!cmos_) break;
                y = pop();
                set_nz(y);
                break;
            case 0x80:  // bra
                if (cmos_) branch(true, offset);
                break;
            case 0x64: case 0x74: case 0x9c: case 0x9e:  // stz
                if (cmos_) write(address_, 0);
                break;
            case 0xda:  // phx
                if (cmos_) push(x);
                break;
            case 0xfa:  // plx
                if (!cmos_) break;
                x = pop();
                set_nz(x);
                break;
            default:  // nop and the undocumented opcodes the sound code never uses
                break;
            case 0x03: case 0x07: case 0x0f: case 0x13:
            case 0x17: case 0x1b: case 0x1f: {  // slo
                uint8_t value = read(address_);
                write(address_, value);
                p.c = (value & 0x80) != 0;
                value = uint8_t(value << 1);
                a |= value;
                set_nz(a);
                write(address_, value);
                break;
            }
            case 0x0b: case 0x2b: {  // anc
                a &= read(address_);
                const uint8_t carry = p.c ? 1 : 0;
                p.c = (a & 0x80) != 0;
                a = uint8_t((a << 1) | carry);
                set_nz(a);
                break;
            }
            case 0x23: case 0x27: case 0x2f: case 0x33:
            case 0x37: case 0x3b: case 0x3f: {  // rla
                const uint8_t value = read(address_);
                write(address_, value);
                const uint16_t rotated = uint16_t((value << 1) | (p.c ? 1 : 0));
                p.c = (rotated & 0x100) != 0;
                a &= uint8_t(rotated);
                set_nz(a);
                write(address_, uint8_t(rotated));
                break;
            }
            case 0x43: case 0x47: case 0x4f: case 0x53:
            case 0x57: case 0x5b: case 0x5f: {  // sre
                uint8_t value = read(address_);
                write(address_, value);
                p.c = (value & 1) != 0;
                value = uint8_t(value >> 1);
                a ^= value;
                set_nz(a);
                write(address_, value);
                break;
            }
            case 0x4b: {  // alr
                p.c = (a & 1) != 0;
                a &= read(address_);
                p.c = (a & 1) != 0;
                a = uint8_t(a >> 1);
                p.z = a == 0;
                p.n = false;
                break;
            }
            case 0x63: case 0x67: case 0x6f: case 0x73:
            case 0x77: case 0x7b: case 0x7f: {  // rra
                uint8_t value = read(address_);
                write(address_, value);
                const uint16_t rotated = uint16_t((p.c ? 0x100 : 0) | value);
                p.c = (value & 1) != 0;
                value = uint8_t(rotated >> 1);
                adc(value);
                write(address_, value);
                break;
            }
            case 0x6b: {  // arr
                a &= read(address_);
                const uint8_t carry = p.c ? 0x80 : 0;
                p.c = (a & 1) != 0;
                a = uint8_t((a >> 1) | carry);
                set_nz(a);
                break;
            }
            case 0x83: case 0x87: case 0x8f: case 0x97:  // sax
                write(address_, uint8_t(a & x));
                break;
            case 0x8b:  // xaa
                a = uint8_t(x & read(address_));
                set_nz(a);
                break;
            case 0xa3: case 0xaf: case 0xb3:
            case 0xb7: case 0xbf:  // lax ($a7 is implicit in the Pascal table)
                a = x = read(address_);
                set_nz(a);
                break;
            case 0xc3: case 0xcf: case 0xd3:
            case 0xd7: case 0xdb: case 0xdf: {  // dcp
                uint8_t value = read(address_);
                write(address_, value);
                value = uint8_t(value - 1);
                p.c = a >= value;
                const uint8_t diff = uint8_t(a - value);
                p.z = diff == 0;
                p.n = (diff & 0x80) != 0;
                write(address_, value);
                break;
            }
            case 0xe3: case 0xef: case 0xf3:
            case 0xf7: case 0xfb: case 0xff: {  // isb / isc
                uint8_t value = read(address_);
                write(address_, value);
                value = uint8_t(value + 1);
                sbc(value);
                write(address_, value);
                break;
            }
            default:  // documented and undocumented nops
                break;
            }

        const int elapsed = cycle_table[instruction] + extra_cycles_;
        executed += elapsed;
        if (cycle_handler_) cycle_handler_(elapsed);
        if (stolen_cycles_ != 0) {
            executed += stolen_cycles_;
            if (cycle_handler_) cycle_handler_(stolen_cycles_);
            stolen_cycles_ = 0;
        }
    }
    return executed;
}

void M6502::steal_cycles(int cycles) {
    if (cycles > 0) stolen_cycles_ += cycles;
}

}  // namespace dsp
