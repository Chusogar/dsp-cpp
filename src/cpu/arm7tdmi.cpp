#include "cpu/arm7tdmi.h"

namespace dsp {
namespace {

inline uint32_t ror32(uint32_t value, uint32_t amount) {
    amount &= 31;
    return amount ? (value >> amount) | (value << (32 - amount)) : value;
}

inline int popcount16(uint32_t v) {
    int n = 0;
    for (; v; v &= v - 1) n++;
    return n;
}

}  // namespace

Arm7tdmi::Bank Arm7tdmi::bank_of(uint32_t mode) {
    switch (mode & 0x1F) {
        case kFiq: return kBankFiq;
        case kIrq: return kBankIrq;
        case kSvc: return kBankSvc;
        case kAbort: return kBankAbort;
        case kUndefined: return kBankUndefined;
        default: return kBankUser;
    }
}

void Arm7tdmi::reset() {
    r_.fill(0);
    bank_r13_.fill(0);
    bank_r14_.fill(0);
    bank_spsr_.fill(0);
    fiq_r8_12_.fill(0);
    user_r8_12_.fill(0);
    n_ = z_ = c_ = v_ = false;
    mode_ = kSvc;
    i_ = f_ = true;
    t_ = false;
    pc_ = 0;
    r_[15] = 8;
    irq_line_ = false;
}

void Arm7tdmi::switch_mode(uint32_t mode) {
    mode = (mode & 0x1F) | 0x10;
    const Bank from = bank_of(mode_);
    const Bank to = bank_of(mode);
    if (from != to) {
        bank_r13_[from] = r_[13];
        bank_r14_[from] = r_[14];
        if (from == kBankFiq || to == kBankFiq) {
            auto& save = from == kBankFiq ? fiq_r8_12_ : user_r8_12_;
            for (int i = 0; i < 5; i++) save[size_t(i)] = r_[size_t(8 + i)];
            const auto& load = to == kBankFiq ? fiq_r8_12_ : user_r8_12_;
            for (int i = 0; i < 5; i++) r_[size_t(8 + i)] = load[size_t(i)];
        }
        r_[13] = bank_r13_[to];
        r_[14] = bank_r14_[to];
    }
    mode_ = mode;
}

uint32_t Arm7tdmi::cpsr() const {
    return (n_ ? 0x80000000u : 0) | (z_ ? 0x40000000u : 0) | (c_ ? 0x20000000u : 0) |
           (v_ ? 0x10000000u : 0) | (i_ ? 0x80u : 0) | (f_ ? 0x40u : 0) | (t_ ? 0x20u : 0) | mode_;
}

void Arm7tdmi::set_cpsr(uint32_t value) {
    n_ = (value >> 31) & 1;
    z_ = (value >> 30) & 1;
    c_ = (value >> 29) & 1;
    v_ = (value >> 28) & 1;
    i_ = (value >> 7) & 1;
    f_ = (value >> 6) & 1;
    t_ = (value >> 5) & 1;
    if (((value & 0x1F) | 0x10) != mode_) switch_mode(value);
}

uint32_t Arm7tdmi::spsr() const {
    const Bank b = bank_of(mode_);
    return b == kBankUser ? cpsr() : bank_spsr_[b];
}

void Arm7tdmi::set_spsr(uint32_t value) {
    const Bank b = bank_of(mode_);
    if (b != kBankUser) bank_spsr_[b] = value;
}

void Arm7tdmi::set_reg(int n, uint32_t value) {
    if (n == 15) {
        pc_ = value & (t_ ? ~1u : ~3u);
        r_[15] = pc_;
    } else {
        r_[size_t(n)] = value;
    }
}

void Arm7tdmi::set_pc(uint32_t value, bool thumb) {
    t_ = thumb;
    set_reg(15, value);
}

void Arm7tdmi::write_pc(uint32_t value) {
    pc_ = value & (t_ ? ~1u : ~3u);
    r_[15] = pc_;
    branched_ = true;
    bus_.idle(2);  // pipeline refill
}

void Arm7tdmi::exception(uint32_t vector, uint32_t mode, uint32_t return_address, bool disable_fiq) {
    const uint32_t saved = cpsr();
    switch_mode(mode);
    set_spsr(saved);
    r_[14] = return_address;
    t_ = false;
    i_ = true;
    if (disable_fiq) f_ = true;
    write_pc(vector);
}

bool Arm7tdmi::condition(uint32_t cond) const {
    switch (cond & 15) {
        case 0x0: return z_;
        case 0x1: return !z_;
        case 0x2: return c_;
        case 0x3: return !c_;
        case 0x4: return n_;
        case 0x5: return !n_;
        case 0x6: return v_;
        case 0x7: return !v_;
        case 0x8: return c_ && !z_;
        case 0x9: return !c_ || z_;
        case 0xA: return n_ == v_;
        case 0xB: return n_ != v_;
        case 0xC: return !z_ && n_ == v_;
        case 0xD: return z_ || n_ != v_;
        case 0xE: return true;
        default: return false;  // NV: never on ARMv4
    }
}

void Arm7tdmi::step() {
    if (irq_line_ && !i_) {
        // LR_irq = next instruction + 4 (SUBS PC, LR, #4 returns), both states.
        exception(0x18, kIrq, pc_ + 4, false);
        return;
    }
    exec_addr_ = pc_;
    branched_ = false;
    if (t_) {
        const uint16_t op = bus_.read16(pc_, true);
        r_[15] = pc_ + 4;
        exec_thumb(op);
        if (!branched_) pc_ = exec_addr_ + 2;
    } else {
        const uint32_t op = bus_.read32(pc_, true);
        r_[15] = pc_ + 8;
        if (condition(op >> 28)) exec_arm(op);
        if (!branched_) pc_ = exec_addr_ + 4;
    }
}

uint32_t Arm7tdmi::add_flags(uint32_t a, uint32_t b, uint32_t carry_in, bool set) {
    const uint64_t wide = uint64_t(a) + uint64_t(b) + uint64_t(carry_in);
    const uint32_t result = uint32_t(wide);
    if (set) {
        set_nz(result);
        c_ = (wide >> 32) != 0;
        v_ = ((~(a ^ b) & (a ^ result)) >> 31) != 0;
    }
    return result;
}

uint32_t Arm7tdmi::sub_flags(uint32_t a, uint32_t b, uint32_t carry_in, bool set) {
    // a - b - !carry == a + ~b + carry; C is "no borrow".
    return add_flags(a, ~b, carry_in, set);
}

uint32_t Arm7tdmi::shift(uint32_t type, uint32_t value, uint32_t amount, bool by_register, bool& carry) {
    if (by_register) {
        if (amount == 0) return value;
        switch (type) {
            case 0:  // LSL
                if (amount < 32) {
                    carry = (value >> (32 - amount)) & 1;
                    return value << amount;
                }
                carry = amount == 32 ? (value & 1) : false;
                return 0;
            case 1:  // LSR
                if (amount < 32) {
                    carry = (value >> (amount - 1)) & 1;
                    return value >> amount;
                }
                carry = amount == 32 ? (value >> 31) : false;
                return 0;
            case 2:  // ASR
                if (amount < 32) {
                    carry = (uint32_t(int32_t(value) >> (amount - 1))) & 1;
                    return uint32_t(int32_t(value) >> amount);
                }
                carry = value >> 31;
                return carry ? 0xFFFFFFFFu : 0;
            default: {  // ROR
                const uint32_t r = ror32(value, amount);
                carry = r >> 31;
                return r;
            }
        }
    }
    switch (type) {
        case 0:
            if (amount == 0) return value;
            carry = (value >> (32 - amount)) & 1;
            return value << amount;
        case 1:
            if (amount == 0) {
                carry = value >> 31;
                return 0;
            }
            carry = (value >> (amount - 1)) & 1;
            return value >> amount;
        case 2:
            if (amount == 0) {
                carry = value >> 31;
                return carry ? 0xFFFFFFFFu : 0;
            }
            carry = (uint32_t(int32_t(value) >> (amount - 1))) & 1;
            return uint32_t(int32_t(value) >> amount);
        default:
            if (amount == 0) {  // RRX
                const bool out = value & 1;
                const uint32_t r = (c_ ? 0x80000000u : 0) | (value >> 1);
                carry = out;
                return r;
            }
            {
                const uint32_t r = ror32(value, amount);
                carry = r >> 31;
                return r;
            }
    }
}

uint32_t Arm7tdmi::read_word_rotated(uint32_t address) {
    // The bus sees the unaligned address (8-bit SRAM uses the low bits).
    const uint32_t v = bus_.read32(address, false);
    return ror32(v, (address & 3) * 8);
}

void Arm7tdmi::multiply_cycles(uint32_t rs, bool sign_extend) {
    int m = 4;
    if ((rs >> 8) == 0 || (sign_extend && (rs >> 8) == 0xFFFFFF)) m = 1;
    else if ((rs >> 16) == 0 || (sign_extend && (rs >> 16) == 0xFFFF)) m = 2;
    else if ((rs >> 24) == 0 || (sign_extend && (rs >> 24) == 0xFF)) m = 3;
    bus_.idle(m);
}

// ---------------------------------------------------------------------------
// ARM
// ---------------------------------------------------------------------------

void Arm7tdmi::exec_arm(uint32_t op) {
    if ((op & 0x0FFFFFF0u) == 0x012FFF10u) {  // BX
        const uint32_t target = r_[op & 15];
        t_ = (target & 1) != 0;
        write_pc(target);
        return;
    }
    if ((op & 0x0E000000u) == 0x0A000000u) {  // B / BL
        int32_t offset = int32_t(op << 8) >> 6;
        if (op & (1u << 24)) r_[14] = exec_addr_ + 4;
        write_pc(r_[15] + uint32_t(offset));
        return;
    }
    if ((op & 0x0F000000u) == 0x0F000000u) {  // SWI
        exception(0x08, kSvc, exec_addr_ + 4, false);
        return;
    }
    if ((op & 0x0C000000u) == 0x04000000u) {
        if ((op & 0x02000010u) == 0x02000010u) {  // undefined
            exception(0x04, kUndefined, exec_addr_ + 4, false);
            return;
        }
        arm_single_transfer(op);
        return;
    }
    if ((op & 0x0E000000u) == 0x08000000u) {
        arm_block_transfer(op);
        return;
    }
    if ((op & 0x0C000000u) == 0x00000000u) {
        if ((op & 0x0FC000F0u) == 0x00000090u) return arm_multiply(op);
        if ((op & 0x0F8000F0u) == 0x00800090u) return arm_multiply_long(op);
        if ((op & 0x0FB00FF0u) == 0x01000090u) return arm_swap(op);
        if ((op & 0x0E000090u) == 0x00000090u && (op & 0x60u) != 0) return arm_halfword(op);
        if ((op & 0x0FBF0FFFu) == 0x010F0000u || (op & 0x0DB0F000u) == 0x0120F000u) {
            return arm_psr_transfer(op);
        }
        arm_data_processing(op);
        return;
    }
    // Coprocessor instructions: no coprocessor on the GBA.
    exception(0x04, kUndefined, exec_addr_ + 4, false);
}

void Arm7tdmi::arm_data_processing(uint32_t op) {
    const uint32_t opcode = (op >> 21) & 15;
    const bool s = (op >> 20) & 1;
    const uint32_t rn = (op >> 16) & 15;
    const uint32_t rd = (op >> 12) & 15;
    bool carry = c_;
    uint32_t op2;
    uint32_t a = r_[rn];
    if (op & (1u << 25)) {
        const uint32_t rot = (op >> 7) & 0x1E;
        op2 = ror32(op & 0xFF, rot);
        if (rot) carry = op2 >> 31;
    } else {
        const uint32_t rm = op & 15;
        const uint32_t type = (op >> 5) & 3;
        uint32_t value = r_[rm];
        if (op & 0x10) {
            // Register-specified shift: one extra cycle, r15 reads + 12.
            bus_.idle(1);
            if (rm == 15) value += 4;
            if (rn == 15) a += 4;
            op2 = shift(type, value, r_[(op >> 8) & 15] & 0xFF, true, carry);
        } else {
            op2 = shift(type, value, (op >> 7) & 31, false, carry);
        }
    }
    const bool flags = s && rd != 15;
    uint32_t result = 0;
    bool write = true;
    switch (opcode) {
        case 0x0: result = a & op2; break;
        case 0x1: result = a ^ op2; break;
        case 0x2: result = sub_flags(a, op2, 1, flags); break;
        case 0x3: result = sub_flags(op2, a, 1, flags); break;
        case 0x4: result = add_flags(a, op2, 0, flags); break;
        case 0x5: result = add_flags(a, op2, c_ ? 1 : 0, flags); break;
        case 0x6: result = sub_flags(a, op2, c_ ? 1 : 0, flags); break;
        case 0x7: result = sub_flags(op2, a, c_ ? 1 : 0, flags); break;
        case 0x8: result = a & op2; write = false; break;
        case 0x9: result = a ^ op2; write = false; break;
        case 0xA: result = sub_flags(a, op2, 1, s); write = false; break;
        case 0xB: result = add_flags(a, op2, 0, s); write = false; break;
        case 0xC: result = a | op2; break;
        case 0xD: result = op2; break;
        case 0xE: result = a & ~op2; break;
        default: result = ~op2; break;
    }
    const bool logical = opcode <= 1 || (opcode >= 8 && opcode <= 9) || opcode >= 0xC;
    if (logical && (flags || (!write && s))) {
        set_nz(result);
        c_ = carry;
    }
    if (!write) {
        if (rd == 15 && s) set_cpsr(spsr());  // TSTP & co (ARMv2 leftovers)
        return;
    }
    if (rd == 15) {
        if (s) set_cpsr(spsr());  // exception return: restores mode and T
        write_pc(result);
    } else {
        r_[rd] = result;
    }
}

void Arm7tdmi::arm_psr_transfer(uint32_t op) {
    const bool use_spsr = (op >> 22) & 1;
    if ((op & 0x0FBF0FFFu) == 0x010F0000u) {  // MRS
        r_[(op >> 12) & 15] = use_spsr ? spsr() : cpsr();
        return;
    }
    uint32_t value;
    if (op & (1u << 25)) value = ror32(op & 0xFF, (op >> 7) & 0x1E);
    else value = r_[op & 15];
    uint32_t mask = 0;
    if (op & (1u << 19)) mask |= 0xFF000000u;
    if (op & (1u << 16)) mask |= 0x000000FFu;
    if (use_spsr) {
        set_spsr((spsr() & ~mask) | (value & mask));
        return;
    }
    if (mode_ == kUser) mask &= 0xFF000000u;
    uint32_t next = (cpsr() & ~mask) | (value & mask);
    next = (next & ~0x20u) | (t_ ? 0x20u : 0);  // MSR cannot change the state
    set_cpsr(next);
}

void Arm7tdmi::arm_multiply(uint32_t op) {
    const uint32_t rd = (op >> 16) & 15;
    const uint32_t rs = r_[(op >> 8) & 15];
    uint32_t result = r_[op & 15] * rs;
    multiply_cycles(rs, true);
    if (op & (1u << 21)) {
        result += r_[(op >> 12) & 15];
        bus_.idle(1);
    }
    r_[rd] = result;
    if (op & (1u << 20)) set_nz(result);
}

void Arm7tdmi::arm_multiply_long(uint32_t op) {
    const uint32_t hi = (op >> 16) & 15;
    const uint32_t lo = (op >> 12) & 15;
    const uint32_t rs = r_[(op >> 8) & 15];
    const uint32_t rm = r_[op & 15];
    const bool sign = (op >> 22) & 1;
    uint64_t result;
    if (sign) result = uint64_t(int64_t(int32_t(rm)) * int64_t(int32_t(rs)));
    else result = uint64_t(rm) * uint64_t(rs);
    multiply_cycles(rs, sign);
    bus_.idle(1);
    if (op & (1u << 21)) {
        result += (uint64_t(r_[hi]) << 32) | r_[lo];
        bus_.idle(1);
    }
    r_[lo] = uint32_t(result);
    r_[hi] = uint32_t(result >> 32);
    if (op & (1u << 20)) {
        n_ = (result >> 63) != 0;
        z_ = result == 0;
    }
}

void Arm7tdmi::arm_swap(uint32_t op) {
    const uint32_t address = r_[(op >> 16) & 15];
    const uint32_t rd = (op >> 12) & 15;
    const uint32_t source = r_[op & 15];
    if (op & (1u << 22)) {
        const uint8_t old = bus_.read8(address);
        bus_.write8(address, uint8_t(source));
        r_[rd] = old;
    } else {
        const uint32_t old = read_word_rotated(address);
        bus_.write32(address & ~3u, source);
        r_[rd] = old;
    }
    bus_.idle(1);
}

void Arm7tdmi::arm_halfword(uint32_t op) {
    const bool pre = (op >> 24) & 1;
    const bool up = (op >> 23) & 1;
    const bool writeback = !pre || ((op >> 21) & 1);
    const bool load = (op >> 20) & 1;
    const uint32_t rn = (op >> 16) & 15;
    const uint32_t rd = (op >> 12) & 15;
    const uint32_t sh = (op >> 5) & 3;
    const uint32_t offset = (op & (1u << 22)) ? (((op >> 4) & 0xF0) | (op & 0x0F)) : r_[op & 15];
    const uint32_t base = r_[rn];
    const uint32_t moved = up ? base + offset : base - offset;
    const uint32_t address = pre ? moved : base;
    if (load) {
        uint32_t value;
        if (sh == 1) {
            value = ror32(bus_.read16(address, false), (address & 1) * 8);
        } else if (sh == 2) {
            value = uint32_t(int32_t(int8_t(bus_.read8(address))));
        } else if (address & 1) {
            value = uint32_t(int32_t(int8_t(bus_.read8(address))));  // misaligned LDRSH
        } else {
            value = uint32_t(int32_t(int16_t(bus_.read16(address, false))));
        }
        bus_.idle(1);
        if (writeback && rn != rd && rn != 15) r_[rn] = moved;
        if (rd == 15) write_pc(value);
        else r_[rd] = value;
    } else {
        if (sh == 1) {
            uint32_t value = r_[rd];
            if (rd == 15) value += 4;
            bus_.write16(address, uint16_t(value));
        }
        // sh 2/3 with L=0 are ARMv5 LDRD/STRD: no effect on ARMv4.
        if (writeback && rn != 15) r_[rn] = moved;
    }
}

void Arm7tdmi::arm_single_transfer(uint32_t op) {
    const bool reg_offset = (op >> 25) & 1;
    const bool pre = (op >> 24) & 1;
    const bool up = (op >> 23) & 1;
    const bool byte = (op >> 22) & 1;
    const bool writeback = !pre || ((op >> 21) & 1);
    const bool load = (op >> 20) & 1;
    const uint32_t rn = (op >> 16) & 15;
    const uint32_t rd = (op >> 12) & 15;
    uint32_t offset;
    if (reg_offset) {
        bool dummy = c_;
        offset = shift((op >> 5) & 3, r_[op & 15], (op >> 7) & 31, false, dummy);
    } else {
        offset = op & 0xFFF;
    }
    const uint32_t base = r_[rn];
    const uint32_t moved = up ? base + offset : base - offset;
    const uint32_t address = pre ? moved : base;
    if (load) {
        const uint32_t value = byte ? bus_.read8(address) : read_word_rotated(address);
        bus_.idle(1);
        if (writeback && rn != rd && rn != 15) r_[rn] = moved;
        if (rd == 15) write_pc(value);
        else r_[rd] = value;
    } else {
        uint32_t value = r_[rd];
        if (rd == 15) value += 4;  // STR PC stores the instruction + 12
        if (byte) bus_.write8(address, uint8_t(value));
        else bus_.write32(address, value);
        if (writeback && rn != 15) r_[rn] = moved;
    }
}

void Arm7tdmi::arm_block_transfer(uint32_t op) {
    const bool pre = (op >> 24) & 1;
    const bool up = (op >> 23) & 1;
    const bool psr = (op >> 22) & 1;
    const bool writeback = (op >> 21) & 1;
    const bool load = (op >> 20) & 1;
    const uint32_t rn = (op >> 16) & 15;
    uint32_t list = op & 0xFFFF;
    int count = popcount16(list);
    if (list == 0) {  // ARMv4 quirk: transfers r15, base moves by 0x40
        list = 0x8000;
        count = 16;
    }
    const uint32_t base = r_[rn];
    uint32_t address;
    uint32_t final_base;
    if (up) {
        address = base + (pre ? 4 : 0);
        final_base = base + uint32_t(count) * 4;
    } else {
        address = base - uint32_t(count) * 4 + (pre ? 0 : 4);
        final_base = base - uint32_t(count) * 4;
    }
    // S bit without r15 in an LDM (or any STM): user-bank registers.
    const bool user_bank = psr && !(load && (list & 0x8000));
    const Bank bank = bank_of(mode_);
    auto user_reg = [&](int i) -> uint32_t& {
        if (i >= 8 && i <= 12 && bank == kBankFiq) return user_r8_12_[size_t(i - 8)];
        if ((i == 13 || i == 14) && bank != kBankUser) {
            return i == 13 ? bank_r13_[kBankUser] : bank_r14_[kBankUser];
        }
        return r_[size_t(i)];
    };

    if (load) {
        bus_.idle(1);
        if (writeback && !(list & (1u << rn))) r_[rn] = final_base;
        for (int i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            const uint32_t value = bus_.read32(address & ~3u, false);
            address += 4;
            if (i == 15) {
                if (psr) set_cpsr(spsr());
                write_pc(value);
            } else if (user_bank) {
                user_reg(i) = value;
            } else {
                r_[size_t(i)] = value;
            }
        }
        return;
    }
    bool first = true;
    for (int i = 0; i < 16; i++) {
        if (!(list & (1u << i))) continue;
        uint32_t value = user_bank ? user_reg(i) : r_[size_t(i)];
        if (i == 15) value += 4;
        bus_.write32(address & ~3u, value);
        address += 4;
        if (first) {
            // The base is written back after the first transfer: a base that is
            // the lowest listed register is stored unchanged, later ones see
            // the new value.
            if (writeback) r_[rn] = final_base;
            first = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Thumb
// ---------------------------------------------------------------------------

void Arm7tdmi::exec_thumb(uint16_t op) {
    switch (op >> 13) {
        case 0: {
            if (((op >> 11) & 3) == 3) {  // format 2: ADD/SUB
                const uint32_t rd = op & 7;
                const uint32_t a = r_[(op >> 3) & 7];
                const uint32_t b = (op & 0x0400) ? uint32_t((op >> 6) & 7) : r_[(op >> 6) & 7];
                r_[rd] = (op & 0x0200) ? sub_flags(a, b, 1, true) : add_flags(a, b, 0, true);
                return;
            }
            // format 1: shift by immediate
            bool carry = c_;
            const uint32_t result = shift((op >> 11) & 3, r_[(op >> 3) & 7], (op >> 6) & 31, false, carry);
            r_[op & 7] = result;
            set_nz(result);
            c_ = carry;
            return;
        }
        case 1: {  // format 3: MOV/CMP/ADD/SUB imm8
            const uint32_t rd = (op >> 8) & 7;
            const uint32_t imm = op & 0xFF;
            switch ((op >> 11) & 3) {
                case 0: r_[rd] = imm; set_nz(imm); break;
                case 1: sub_flags(r_[rd], imm, 1, true); break;
                case 2: r_[rd] = add_flags(r_[rd], imm, 0, true); break;
                default: r_[rd] = sub_flags(r_[rd], imm, 1, true); break;
            }
            return;
        }
        case 2: {
            if ((op & 0xFC00) == 0x4000) {  // format 4: ALU
                const uint32_t rd = op & 7;
                const uint32_t rs = r_[(op >> 3) & 7];
                uint32_t& d = r_[rd];
                bool carry = c_;
                switch ((op >> 6) & 15) {
                    case 0x0: d &= rs; set_nz(d); break;
                    case 0x1: d ^= rs; set_nz(d); break;
                    case 0x2: bus_.idle(1); d = shift(0, d, rs & 0xFF, true, carry); set_nz(d); c_ = carry; break;
                    case 0x3: bus_.idle(1); d = shift(1, d, rs & 0xFF, true, carry); set_nz(d); c_ = carry; break;
                    case 0x4: bus_.idle(1); d = shift(2, d, rs & 0xFF, true, carry); set_nz(d); c_ = carry; break;
                    case 0x5: d = add_flags(d, rs, c_ ? 1 : 0, true); break;
                    case 0x6: d = sub_flags(d, rs, c_ ? 1 : 0, true); break;
                    case 0x7: bus_.idle(1); d = shift(3, d, rs & 0xFF, true, carry); set_nz(d); c_ = carry; break;
                    case 0x8: set_nz(d & rs); break;
                    case 0x9: d = sub_flags(0, rs, 1, true); break;
                    case 0xA: sub_flags(d, rs, 1, true); break;
                    case 0xB: add_flags(d, rs, 0, true); break;
                    case 0xC: d |= rs; set_nz(d); break;
                    case 0xD: multiply_cycles(d, true); d *= rs; set_nz(d); break;
                    case 0xE: d &= ~rs; set_nz(d); break;
                    default: d = ~rs; set_nz(d); break;
                }
                return;
            }
            if ((op & 0xFC00) == 0x4400) {  // format 5: hi registers / BX
                const uint32_t rd = (op & 7) | ((op >> 4) & 8);
                const uint32_t rs = (op >> 3) & 15;
                const uint32_t value = r_[rs];
                switch ((op >> 8) & 3) {
                    case 0:
                        if (rd == 15) write_pc(r_[15] + value);
                        else r_[rd] += value;
                        break;
                    case 1: sub_flags(r_[rd], value, 1, true); break;
                    case 2:
                        if (rd == 15) write_pc(value);
                        else r_[rd] = value;
                        break;
                    default:
                        t_ = (value & 1) != 0;
                        write_pc(value);
                        break;
                }
                return;
            }
            if ((op & 0xF800) == 0x4800) {  // format 6: LDR PC-relative
                r_[(op >> 8) & 7] = bus_.read32((r_[15] & ~2u) + (op & 0xFF) * 4u, false);
                bus_.idle(1);
                return;
            }
            // formats 7/8: register offset
            const uint32_t address = r_[(op >> 3) & 7] + r_[(op >> 6) & 7];
            const uint32_t rd = op & 7;
            if (!(op & 0x0200)) {
                switch ((op >> 10) & 3) {
                    case 0: bus_.write32(address, r_[rd]); break;
                    case 1: bus_.write8(address, uint8_t(r_[rd])); break;
                    case 2: r_[rd] = read_word_rotated(address); bus_.idle(1); break;
                    default: r_[rd] = bus_.read8(address); bus_.idle(1); break;
                }
            } else {
                switch ((op >> 10) & 3) {
                    case 0: bus_.write16(address, uint16_t(r_[rd])); break;
                    case 1: r_[rd] = uint32_t(int32_t(int8_t(bus_.read8(address)))); bus_.idle(1); break;
                    case 2:
                        r_[rd] = ror32(bus_.read16(address, false), (address & 1) * 8);
                        bus_.idle(1);
                        break;
                    default:
                        if (address & 1) r_[rd] = uint32_t(int32_t(int8_t(bus_.read8(address))));
                        else r_[rd] = uint32_t(int32_t(int16_t(bus_.read16(address, false))));
                        bus_.idle(1);
                        break;
                }
            }
            return;
        }
        case 3: {  // format 9: immediate offset
            const uint32_t rd = op & 7;
            const uint32_t base = r_[(op >> 3) & 7];
            const uint32_t imm = (op >> 6) & 31;
            const bool byte = (op >> 12) & 1;
            const bool load = (op >> 11) & 1;
            const uint32_t address = base + (byte ? imm : imm * 4);
            if (load) {
                r_[rd] = byte ? bus_.read8(address) : read_word_rotated(address);
                bus_.idle(1);
            } else if (byte) {
                bus_.write8(address, uint8_t(r_[rd]));
            } else {
                bus_.write32(address, r_[rd]);
            }
            return;
        }
        case 4: {
            if (!(op & 0x1000)) {  // format 10: halfword immediate
                const uint32_t rd = op & 7;
                const uint32_t address = r_[(op >> 3) & 7] + ((op >> 6) & 31) * 2u;
                if (op & 0x0800) {
                    r_[rd] = ror32(bus_.read16(address, false), (address & 1) * 8);
                    bus_.idle(1);
                } else {
                    bus_.write16(address, uint16_t(r_[rd]));
                }
                return;
            }
            // format 11: SP-relative
            const uint32_t rd = (op >> 8) & 7;
            const uint32_t address = r_[13] + (op & 0xFF) * 4u;
            if (op & 0x0800) {
                r_[rd] = read_word_rotated(address);
                bus_.idle(1);
            } else {
                bus_.write32(address, r_[rd]);
            }
            return;
        }
        case 5: {
            if (!(op & 0x1000)) {  // format 12: load address
                const uint32_t rd = (op >> 8) & 7;
                const uint32_t base = (op & 0x0800) ? r_[13] : (r_[15] & ~2u);
                r_[rd] = base + (op & 0xFF) * 4u;
                return;
            }
            if ((op & 0xFF00) == 0xB000) {  // format 13: ADD SP
                const uint32_t imm = (op & 0x7F) * 4u;
                r_[13] = (op & 0x80) ? r_[13] - imm : r_[13] + imm;
                return;
            }
            if ((op & 0xF600) == 0xB400) {  // format 14: PUSH/POP
                const bool pop = (op >> 11) & 1;
                const bool extra = (op >> 8) & 1;
                const uint32_t list = op & 0xFF;
                if (pop) {
                    uint32_t address = r_[13];
                    bus_.idle(1);
                    for (int i = 0; i < 8; i++) {
                        if (!(list & (1u << i))) continue;
                        r_[size_t(i)] = bus_.read32(address & ~3u, false);
                        address += 4;
                    }
                    if (extra) {
                        const uint32_t value = bus_.read32(address & ~3u, false);
                        address += 4;
                        r_[13] = address;
                        write_pc(value);  // ARMv4: stays in Thumb
                        return;
                    }
                    r_[13] = address;
                } else {
                    const int count = popcount16(list) + (extra ? 1 : 0);
                    uint32_t address = r_[13] - uint32_t(count) * 4;
                    r_[13] = address;
                    for (int i = 0; i < 8; i++) {
                        if (!(list & (1u << i))) continue;
                        bus_.write32(address & ~3u, r_[size_t(i)]);
                        address += 4;
                    }
                    if (extra) bus_.write32(address & ~3u, r_[14]);
                }
                return;
            }
            // Unused (ARMv5 BKPT etc.)
            exception(0x04, kUndefined, exec_addr_ + 2, false);
            return;
        }
        case 6: {
            if (!(op & 0x1000)) {  // format 15: LDMIA/STMIA
                const uint32_t rb = (op >> 8) & 7;
                uint32_t list = op & 0xFF;
                uint32_t address = r_[rb];
                if (list == 0) {  // ARMv4 quirk: r15, base += 0x40
                    if (op & 0x0800) {
                        const uint32_t value = bus_.read32(address & ~3u, false);
                        r_[rb] = address + 0x40;
                        write_pc(value);
                    } else {
                        bus_.write32(address & ~3u, r_[15] + 2);
                        r_[rb] = address + 0x40;
                    }
                    return;
                }
                const uint32_t final_base = address + uint32_t(popcount16(list)) * 4;
                if (op & 0x0800) {
                    bus_.idle(1);
                    for (int i = 0; i < 8; i++) {
                        if (!(list & (1u << i))) continue;
                        r_[size_t(i)] = bus_.read32(address & ~3u, false);
                        address += 4;
                    }
                    if (!(list & (1u << rb))) r_[rb] = final_base;
                } else {
                    bool first = true;
                    for (int i = 0; i < 8; i++) {
                        if (!(list & (1u << i))) continue;
                        bus_.write32(address & ~3u, r_[size_t(i)]);
                        address += 4;
                        if (first) {
                            r_[rb] = final_base;
                            first = false;
                        }
                    }
                }
                return;
            }
            const uint32_t cond = (op >> 8) & 15;
            if (cond == 15) {  // format 17: SWI
                exception(0x08, kSvc, exec_addr_ + 2, false);
                return;
            }
            if (cond == 14) {
                exception(0x04, kUndefined, exec_addr_ + 2, false);
                return;
            }
            if (condition(cond)) {  // format 16
                const int32_t offset = int32_t(int8_t(op & 0xFF)) * 2;
                write_pc(r_[15] + uint32_t(offset));
            }
            return;
        }
        default: {
            if (!(op & 0x1000)) {
                if (op & 0x0800) {  // ARMv5 BLX suffix: undefined here
                    exception(0x04, kUndefined, exec_addr_ + 2, false);
                    return;
                }
                const int32_t offset = (int32_t(uint32_t(op) << 21) >> 20);  // format 18
                write_pc(r_[15] + uint32_t(offset));
                return;
            }
            // format 19: BL, two halves
            if (!(op & 0x0800)) {
                const int32_t offset = int32_t(uint32_t(op) << 21) >> 9;
                r_[14] = r_[15] + uint32_t(offset);
            } else {
                const uint32_t next = exec_addr_ + 2;
                write_pc(r_[14] + (uint32_t(op & 0x7FF) << 1));
                r_[14] = next | 1;
            }
            return;
        }
    }
}

}  // namespace dsp
