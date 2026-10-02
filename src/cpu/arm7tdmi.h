#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dsp {

// Memory interface of the ARM7TDMI. The bus charges its own wait states;
// `code` marks opcode fetches (open bus / BIOS protection on the GBA).
class ArmBus {
public:
    virtual ~ArmBus() = default;
    virtual uint8_t read8(uint32_t address) = 0;
    virtual uint16_t read16(uint32_t address, bool code) = 0;
    virtual uint32_t read32(uint32_t address, bool code) = 0;
    virtual void write8(uint32_t address, uint8_t value) = 0;
    virtual void write16(uint32_t address, uint16_t value) = 0;
    virtual void write32(uint32_t address, uint32_t value) = 0;
    // Internal (I) cycles: multiplies, register shifts, load write-back.
    virtual void idle(int cycles) = 0;
};

// ARM7TDMI (ARMv4T): ARM and Thumb instruction sets, the seven processor
// modes with their banked registers, IRQ/FIQ/SWI/undefined exceptions.
// Interpreted one instruction at a time; r15 reads as the address of the
// executing instruction + 8 (ARM) or + 4 (Thumb), as the pipeline makes it.
class Arm7tdmi {
public:
    enum Mode : uint32_t {
        kUser = 0x10, kFiq = 0x11, kIrq = 0x12, kSvc = 0x13,
        kAbort = 0x17, kUndefined = 0x1B, kSystem = 0x1F,
    };

    explicit Arm7tdmi(ArmBus& bus) : bus_(bus) {}

    void reset();
    // Executes one instruction (or takes a pending IRQ first).
    void step();

    void set_irq(bool asserted) { irq_line_ = asserted; }
    bool irq_line() const { return irq_line_; }

    // Register access for debuggers, HLE and tests (current bank).
    uint32_t reg(int n) const { return n == 15 ? pc_ : r_[size_t(n)]; }
    void set_reg(int n, uint32_t value);
    uint32_t pc() const { return pc_; }
    void set_pc(uint32_t value, bool thumb);
    uint32_t cpsr() const;
    void set_cpsr(uint32_t value);
    bool thumb() const { return t_; }
    uint32_t mode() const { return mode_; }

private:
    // Banked register storage, indexed by bank_of(mode).
    enum Bank { kBankUser, kBankFiq, kBankIrq, kBankSvc, kBankAbort, kBankUndefined, kBankCount };
    static Bank bank_of(uint32_t mode);
    void switch_mode(uint32_t mode);
    uint32_t spsr() const;
    void set_spsr(uint32_t value);
    void exception(uint32_t vector, uint32_t mode, uint32_t return_address, bool disable_fiq);

    bool condition(uint32_t cond) const;
    void write_pc(uint32_t value);
    void set_nz(uint32_t value) {
        n_ = (value >> 31) != 0;
        z_ = value == 0;
    }
    uint32_t add_flags(uint32_t a, uint32_t b, uint32_t carry_in, bool set);
    uint32_t sub_flags(uint32_t a, uint32_t b, uint32_t borrow_in, bool set);
    uint32_t shift(uint32_t type, uint32_t value, uint32_t amount, bool by_register, bool& carry);
    uint32_t read_word_rotated(uint32_t address);
    void multiply_cycles(uint32_t rs, bool sign_extend);

    void exec_arm(uint32_t op);
    void arm_data_processing(uint32_t op);
    void arm_psr_transfer(uint32_t op);
    void arm_multiply(uint32_t op);
    void arm_multiply_long(uint32_t op);
    void arm_swap(uint32_t op);
    void arm_halfword(uint32_t op);
    void arm_single_transfer(uint32_t op);
    void arm_block_transfer(uint32_t op);

    void exec_thumb(uint16_t op);

    ArmBus& bus_;
    std::array<uint32_t, 16> r_{};
    uint32_t pc_ = 0;          // address of the next instruction to execute
    uint32_t exec_addr_ = 0;   // address of the executing instruction
    bool branched_ = false;
    bool n_ = false, z_ = false, c_ = false, v_ = false;
    bool i_ = true, f_ = true, t_ = false;
    uint32_t mode_ = kSvc;
    std::array<uint32_t, kBankCount> bank_r13_{};
    std::array<uint32_t, kBankCount> bank_r14_{};
    std::array<uint32_t, kBankCount> bank_spsr_{};
    std::array<uint32_t, 5> fiq_r8_12_{};   // FIQ r8-r12
    std::array<uint32_t, 5> user_r8_12_{};  // everyone else's r8-r12
    bool irq_line_ = false;
};

}  // namespace dsp
