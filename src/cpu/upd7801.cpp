#include "cpu/upd7801.h"
#include <utility>

namespace dsp {

// Approximate size/cycle tables (main path only; prefix ops use size 2).
const uint8_t Upd7801::kOpSize[256] = {
    1,2,1,1,3,3,1,2,1,1,1,1,1,1,1,1, 1,1,1,1,3,3,2,2,1,1,1,1,1,1,1,1,
    2,1,1,1,3,3,2,2,1,2,2,2,2,2,2,2, 2,2,1,1,3,3,2,2,1,2,2,2,2,2,2,2,
    3,1,1,1,3,3,2,2,1,2,2,2,1,1,2,2, 2,1,1,1,3,3,2,2,2,2,2,2,2,2,2,2,
    1,1,1,2,1,3,2,2,2,2,2,2,2,2,2,2, 1,3,1,1,1,3,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,2, 1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
};
const uint8_t Upd7801::kOpCycles[256] = {
    4,10,7,7,10,19,4,7,4,4,4,4,4,4,4,4, 4,4,7,7,10,19,7,7,4,4,4,4,4,4,4,4,
    16,4,7,7,10,19,7,7,4,7,7,7,7,7,7,7, 16,8,7,7,10,19,7,7,4,7,7,7,7,7,7,7,
    16,4,4,4,10,19,7,7,0,10,10,10,0,4,10,10, 13,4,4,4,10,19,7,7,13,8,8,13,8,10,8,8,
    0,4,13,10,0,19,7,7,7,7,7,7,7,7,7,7, 0,13,16,0,0,19,7,7,13,13,13,13,13,13,13,13,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
    10,10,10,10,10,4,4,4,7,7,4,7,7,7,7,7, 13,13,13,13,13,4,4,4,10,10,4,7,7,7,7,7,
    10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10, 10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
    10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10, 10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
};

Upd7801::Upd7801(uint32_t clock) : clock_(clock) {}

void Upd7801::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void Upd7801::set_port_in(PortInHandler port_b, PortInHandler port_c) {
    port_b_in_ = std::move(port_b);
    port_c_in_ = std::move(port_c);
}

void Upd7801::set_port_out(PortOutHandler port_a, PortOutHandler port_c) {
    port_a_out_ = std::move(port_a);
    port_c_out_ = std::move(port_c);
}

void Upd7801::reset() {
    pc_ = 0;
    sp_ = 0xFFFE;
    v_ = a_ = b_ = c_ = d_ = e_ = h_ = l_ = 0;
    v2_ = a2_ = b2_ = c2_ = d2_ = e2_ = h2_ = l2_ = 0;
    ea_ = 0;
    zf_ = cy_ = sk_ = hc_ = false;
    iff_ = iff_pending_ = false;
    int1_ = int2_ = 0;
    mkl_ = 0xFF;
    pa_out_ = pc_out_ = 0;
    iram.fill(0);
}

void Upd7801::set_input_line(int irqline, IrqLine state) {
    const uint8_t v = (state == IrqLine::Clear) ? 0 : 1;
    if (irqline == kIntf1) int1_ = v;
    else if (irqline == kIntf2) int2_ = v;
}

void Upd7801::take_irq() {
    if (!iff_) return;
    // INTF2 (VBlank) has priority in SCV usage.
    if (int2_ && (mkl_ & 0x20) == 0) {
        iff_ = false;
        wr(--sp_, uint8_t(pc_ >> 8));
        wr(--sp_, uint8_t(pc_));
        pc_ = 0x0004;  // 7801 INTF2 vector (simplified)
        int2_ = 0;
        return;
    }
    if (int1_ && (mkl_ & 0x10) == 0) {
        iff_ = false;
        wr(--sp_, uint8_t(pc_ >> 8));
        wr(--sp_, uint8_t(pc_));
        pc_ = 0x0002;
        int1_ = 0;
    }
}

int Upd7801::execute_one() {
    if (iff_pending_) {
        iff_ = true;
        iff_pending_ = false;
    }
    take_irq();

    ppc_ = pc_;
    const uint8_t op = fetch();
    int cycles = kOpCycles[op] ? kOpCycles[op] : 4;

    // Skip prefix: if SK set, skip instruction body.
    if (sk_ && op != 0x72) {
        sk_ = false;
        const int extra = kOpSize[op] - 1;
        for (int i = 0; i < extra; i++) fetch();
        return cycles;
    }
    sk_ = false;

    auto set_z = [this](uint8_t v) { zf_ = (v == 0); };

    switch (op) {
        case 0x00: break;  // NOP
        case 0x48: case 0x4C: case 0x4D: case 0x60: case 0x64: case 0x70: case 0x74: {
            // Prefixed ops — consume second byte, no-op for unimplemented bodies.
            (void)fetch();
            cycles = 8;
            break;
        }
        case 0x54: {  // JR
            const int8_t d = int8_t(fetch());
            pc_ = uint16_t(pc_ + d);
            break;
        }
        case 0xC3: {  // JMP wa  (if present in 7801 map as 3-byte)
            pc_ = fetch16();
            break;
        }
        case 0xCD: {  // CALL wa
            const uint16_t a = fetch16();
            wr(--sp_, uint8_t(pc_ >> 8));
            wr(--sp_, uint8_t(pc_));
            pc_ = a;
            break;
        }
        case 0xC9: {  // RET
            const uint8_t lo = rd(sp_++);
            const uint8_t hi = rd(sp_++);
            pc_ = uint16_t(lo | (hi << 8));
            break;
        }
        case 0xFB:  // EI
            iff_pending_ = true;
            break;
        case 0xF3:  // DI
            iff_ = iff_pending_ = false;
            break;
        case 0x7D:  // MOV A,L
            a_ = l_;
            break;
        case 0x7C:  // MOV A,H
            a_ = h_;
            break;
        case 0x78:  // MOV A,B
            a_ = b_;
            break;
        case 0x79:  // MOV A,C
            a_ = c_;
            break;
        case 0x47:  // MOV B,A
            b_ = a_;
            break;
        case 0x4F:  // MOV C,A
            c_ = a_;
            break;
        case 0x67:  // MOV H,A
            h_ = a_;
            break;
        case 0x6F:  // MOV L,A
            l_ = a_;
            break;
        case 0x3E:  // MVI A,n
            a_ = fetch();
            break;
        case 0x06:  // MVI B,n
            b_ = fetch();
            break;
        case 0x0E:  // MVI C,n
            c_ = fetch();
            break;
        case 0x26:  // MVI H,n
            h_ = fetch();
            break;
        case 0x2E:  // MVI L,n
            l_ = fetch();
            break;
        case 0x3A: {  // LDA wa
            const uint16_t a = fetch16();
            a_ = rd(a);
            break;
        }
        case 0x32: {  // STA wa
            const uint16_t a = fetch16();
            wr(a, a_);
            break;
        }
        case 0x7E: {  // MOV A,M (HL)
            a_ = rd(uint16_t((h_ << 8) | l_));
            break;
        }
        case 0x77: {  // MOV M,A
            wr(uint16_t((h_ << 8) | l_), a_);
            break;
        }
        case 0xA7:  // ANA A
            a_ &= a_;
            set_z(a_);
            cy_ = false;
            break;
        case 0xB6: {  // ORA M
            a_ = uint8_t(a_ | rd(uint16_t((h_ << 8) | l_)));
            set_z(a_);
            cy_ = false;
            break;
        }
        case 0xB7:  // ORA A
            set_z(a_);
            cy_ = false;
            break;
        case 0xAF:  // XRA A
            a_ = 0;
            set_z(a_);
            cy_ = false;
            break;
        case 0x3C:  // INR A
            a_++;
            set_z(a_);
            break;
        case 0x3D:  // DCR A
            a_--;
            set_z(a_);
            break;
        case 0x09: {  // DAD B
            const uint32_t hl = uint32_t((h_ << 8) | l_) + uint32_t((b_ << 8) | c_);
            h_ = uint8_t(hl >> 8);
            l_ = uint8_t(hl);
            cy_ = (hl > 0xFFFF);
            break;
        }
        case 0xEB:  // XCHG
            std::swap(d_, h_);
            std::swap(e_, l_);
            break;
        case 0xE3: {  // XTHL
            const uint8_t lo = rd(sp_);
            const uint8_t hi = rd(uint16_t(sp_ + 1));
            wr(sp_, l_);
            wr(uint16_t(sp_ + 1), h_);
            l_ = lo;
            h_ = hi;
            break;
        }
        case 0xF9:  // SPHL
            sp_ = uint16_t((h_ << 8) | l_);
            break;
        case 0xE9:  // PCHL
            pc_ = uint16_t((h_ << 8) | l_);
            break;
        // Port I/O helpers used by SCV (approximate 7801 encodings)
        case 0xD3: {  // OUT (n),A — not exact 7801 but kept as escape
            const uint8_t p = fetch();
            if (p == 0 && port_a_out_) {
                pa_out_ = a_;
                port_a_out_(a_);
            }
            if (p == 2 && port_c_out_) {
                pc_out_ = a_;
                port_c_out_(a_);
            }
            break;
        }
        case 0xDB: {  // IN A,(n)
            const uint8_t p = fetch();
            if (p == 1 && port_b_in_) a_ = port_b_in_(0xFF);
            if (p == 2 && port_c_in_) a_ = port_c_in_(0xFF);
            break;
        }
        default: {
            // Unimplemented: skip remaining bytes of the instruction.
            const int extra = kOpSize[op] - 1;
            for (int i = 0; i < extra; i++) fetch();
            break;
        }
    }
    return cycles;
}

int Upd7801::run(int cycles) {
    int done = 0;
    while (done < cycles) {
        const int c = execute_one();
        done += c;
        if (cycle_handler_) cycle_handler_(c);
    }
    return done;
}

}  // namespace dsp
