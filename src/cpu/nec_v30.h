#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// NEC V20/V30/V33, ported from nec_v20_v30.pas (MAME nec.cpp instruction set).
// 20-bit segmented addressing, 8086-compatible opcodes plus the V30 0x0F bit
// operations used by Irem M72.
class NecV30 {
public:
    using ReadHandler = std::function<uint8_t(uint32_t)>;
    using WriteHandler = std::function<void(uint32_t, uint8_t)>;
    using In8Handler = std::function<uint8_t(uint16_t)>;
    using Out8Handler = std::function<void(uint16_t, uint8_t)>;
    using In16Handler = std::function<uint16_t(uint32_t)>;
    using Out16Handler = std::function<void(uint32_t, uint16_t)>;
    using CycleHandler = std::function<void(int)>;

    enum class Type { V20, V30, V33 };

    explicit NecV30(uint32_t clock, Type type = Type::V30);

    void set_memory_handlers(ReadHandler read, WriteHandler write);
    void set_io_handlers(In8Handler in, Out8Handler out);
    void set_io16_handlers(In16Handler in, Out16Handler out);
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }

    void reset();
    // Runs until at least `cycles` cycles have elapsed, returns the amount executed.
    int run(int cycles);

    void set_irq(IrqLine state, uint8_t vector = 0xff);
    void set_nmi(IrqLine state);

    uint32_t clock() const { return clock_; }
    uint32_t pc() const { return ((uint32_t(ps_) << 4) + ip_) & 0xfffff; }
    uint16_t ip() const { return ip_; }

private:
    static constexpr int kDs1 = 0;
    static constexpr int kPs = 1;
    static constexpr int kSs = 2;
    static constexpr int kDs0 = 3;
    static constexpr uint8_t kNmiIrq = 1;
    static constexpr uint8_t kIntIrq = 2;
    static constexpr uint16_t kNmiVector = 2;
    static constexpr uint16_t kDivideVector = 0;
    static constexpr uint16_t kChkindVector = 5;
    static constexpr uint16_t kBrkvVector = 4;

    uint8_t read_byte(uint32_t address) { return read_(address & 0xfffff); }
    void write_byte(uint32_t address, uint8_t value) { write_(address & 0xfffff, value); }
    uint16_t read_word(uint32_t address);
    void write_word(uint32_t address, uint16_t value);

    uint8_t in_byte(uint16_t port);
    void out_byte(uint16_t port, uint8_t value);
    uint16_t in_word(uint16_t port);
    void out_word(uint16_t port, uint16_t value);

    uint8_t fetch();
    uint16_t fetch_word();

    uint32_t default_base(int seg) const;
    uint8_t get_mem_b(int seg, uint16_t off) { return read_byte(default_base(seg) + off); }
    uint16_t get_mem_w(int seg, uint16_t off) { return read_word(default_base(seg) + off); }
    void put_mem_b(int seg, uint16_t off, uint8_t x) { write_byte(default_base(seg) + off, x); }
    void put_mem_w(int seg, uint16_t off, uint16_t x) { write_word(default_base(seg) + off, x); }

    void get_ea(uint8_t modrm);
    uint16_t get_next_rm_word() { return read_word(ea_ + 2); }

    uint8_t get_breg(int index) const;
    void set_breg(int index, uint8_t value);
    uint16_t get_wreg(int index) const;
    void set_wreg(int index, uint16_t value);
    uint8_t reg_byte(uint8_t modrm) const { return get_breg((modrm >> 3) & 7); }
    uint16_t reg_word(uint8_t modrm) const { return get_wreg((modrm >> 3) & 7); }
    void set_reg_byte(uint8_t modrm, uint8_t value) { set_breg((modrm >> 3) & 7, value); }
    void set_reg_word(uint8_t modrm, uint16_t value) { set_wreg((modrm >> 3) & 7, value); }

    uint8_t get_rm_byte(uint8_t modrm);
    uint16_t get_rm_word(uint8_t modrm);
    void put_rm_byte(uint8_t modrm, uint8_t value);
    void put_rm_word(uint8_t modrm, uint16_t value);
    void putback_rm_byte(uint8_t modrm, uint8_t value);
    void putback_rm_word(uint8_t modrm, uint16_t value);
    void put_imm_rm_byte(uint8_t modrm);
    void put_imm_rm_word(uint8_t modrm);

    void clk(int n) { icount_ += n; }
    void clks(int v20, int v30, int v33);
    void clkw(int v20o, int v30o, int v33o, int v20e, int v30e, int v33e, uint16_t addr);
    void clkm(int v20, int v30, int v33, int v20m, int v30m, int v33m, uint8_t modrm);
    void clkr(int v20o, int v30o, int v33o, int v20e, int v30e, int v33e, int vall, uint8_t modrm);

    void do_prefetch(int previous_icount);

    void set_szpf_byte(uint32_t x);
    void set_szpf_word(uint32_t x);
    uint16_t compress_flags() const;
    void expand_flags(uint16_t value);

    bool cf() const { return carry_val_ != 0; }
    bool pf() const { return parity_val_ != 0; }
    bool af() const { return aux_val_ != 0; }
    bool zf() const { return zero_val_ == 0; }
    bool sf() const { return sign_val_ < 0; }
    bool of() const { return over_val_ != 0; }

    uint8_t add_b(uint32_t dst, uint32_t src);
    uint16_t add_w(uint32_t dst, uint32_t src);
    uint8_t sub_b(uint32_t dst, uint32_t src);
    uint16_t sub_w(uint32_t dst, uint32_t src);
    uint8_t and_b(uint32_t dst, uint32_t src);
    uint16_t and_w(uint32_t dst, uint32_t src);
    uint8_t or_b(uint32_t dst, uint32_t src);
    uint16_t or_w(uint32_t dst, uint32_t src);
    uint8_t xor_b(uint32_t dst, uint32_t src);
    uint16_t xor_w(uint32_t dst, uint32_t src);

    uint8_t rol_b(uint8_t dst);
    uint16_t rol_w(uint16_t dst);
    uint8_t ror_b(uint8_t dst);
    uint16_t ror_w(uint16_t dst);
    uint8_t rolc_b(uint8_t dst);
    uint16_t rolc_w(uint16_t dst);
    uint8_t rorc_b(uint8_t dst);
    uint16_t rorc_w(uint16_t dst);
    uint8_t shl_b(uint8_t dst, uint8_t c);
    uint16_t shl_w(uint16_t dst, uint8_t c);
    uint8_t shr_b(uint8_t dst, uint8_t c);
    uint16_t shr_w(uint16_t dst, uint8_t c);
    uint8_t shra_b(uint8_t dst, uint8_t c);
    uint16_t shra_w(uint16_t dst, uint8_t c);

    void push(uint16_t value);
    uint16_t pop();
    void i_pushf();
    void i_popf();

    void nec_interrupt(uint16_t vector);
    void adj4(int param1, int param2);
    void adjb(int param1, int param2);

    void i_movsb();
    void i_movsw();
    void i_cmpsb();
    void i_cmpsw();
    void i_stosb();
    void i_stosw();
    void i_lodsb();
    void i_lodsw();
    void i_scasb();
    void i_scasw();
    void i_insb();
    void i_insw();
    void i_outsb();
    void i_outsw();
    void i_jmp(bool flag);
    void add4s();
    void sub4s();
    void cmp4s();
    void i_pre_nec();
    void repeat_string(uint8_t opcode, bool check_zf, bool zf_must_be);

    void execute_op(uint8_t op);
    uint8_t alu_b(uint8_t op, uint32_t dst, uint32_t src);
    uint16_t alu_w(uint8_t op, uint32_t dst, uint32_t src);
    int dir() const { return df_ ? -1 : 1; }
    bool i_jcc(bool flag);

    uint32_t clock_;
    Type type_;
    uint8_t prefetch_size_ = 6;
    uint8_t prefetch_cycles_ = 2;

    ReadHandler read_;
    WriteHandler write_;
    In8Handler in8_;
    Out8Handler out8_;
    In16Handler in16_;
    Out16Handler out16_;
    CycleHandler cycle_handler_;

    uint16_t aw_ = 0, cw_ = 0, dw_ = 0, bw_ = 0;
    uint16_t sp_ = 0, bp_ = 0, ix_ = 0, iy_ = 0;
    uint16_t ds1_ = 0, ps_ = 0xffff, ss_ = 0, ds0_ = 0;
    uint16_t ip_ = 0;
    uint16_t eo_ = 0;
    uint32_t ea_ = 0;

    int32_t sign_val_ = 0;
    uint32_t aux_val_ = 0;
    uint32_t over_val_ = 0;
    uint32_t zero_val_ = 1;
    uint32_t carry_val_ = 0;
    uint32_t parity_val_ = 1;
    bool tf_ = false;
    bool iff_ = false;
    bool df_ = false;
    bool mf_ = true;

    uint32_t prefix_base_ = 0;
    bool seg_prefix_ = false;
    bool prefetch_reset_ = false;
    int prefetch_count_ = 0;
    bool no_interrupt_ = false;
    uint8_t irq_pending_ = 0;
    uint8_t irq_vector_ = 0xff;
    IrqLine nmi_state_ = IrqLine::Clear;
    bool halted_ = false;

    int icount_ = 0;
    int executed_ = 0;
};

}  // namespace dsp
