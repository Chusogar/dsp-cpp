#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "cpu/irq_line.h"

namespace dsp {

// Motorola 68000/68010, ported from m68000.pas.
class M68000 {
public:
    using ReadWordHandler = std::function<uint16_t(uint32_t)>;
    using WriteWordHandler = std::function<void(uint32_t, uint16_t)>;
    using ReadByteHandler = std::function<uint8_t(uint32_t)>;
    using WriteByteHandler = std::function<void(uint32_t, uint8_t)>;
    using CycleHandler = std::function<void(int)>;
    // Fired when the CPU executes the RESET instruction. Real 68000 hardware
    // pulses an external reset line to peripherals when this runs; it does
    // NOT reset the executing CPU itself.
    using ResetInstructionHandler = std::function<void()>;
    // Optional: return the 68000 vector number (0-255) for an IPL. When unset,
    // autovectors 25-31 are used. The Atari ST MFP needs this for IPL 6.

    enum class Type { M68000, M68010, M68020 };

    // 32 bit register with the byte/word views the Pascal code relies on.
    struct Reg32 {
        uint32_t l = 0;

        uint8_t l0() const { return uint8_t(l); }
        void set_l0(uint8_t value) { l = (l & 0xffffff00u) | value; }
        uint16_t wl() const { return uint16_t(l); }
        void set_wl(uint16_t value) { l = (l & 0xffff0000u) | value; }
        uint16_t wh() const { return uint16_t(l >> 16); }
        void set_wh(uint16_t value) { l = (l & 0x0000ffffu) | (uint32_t(value) << 16); }
    };

    struct Flags {
        bool t = false, s = false;
        bool x = false, n = false, z = false, v = false, c = false;
        uint8_t im = 0;
    };

    explicit M68000(uint32_t clock, Type type = Type::M68000);

    void set_memory_handlers(ReadWordHandler read, WriteWordHandler write);
    // Optional byte accessors. Without them, byte ops read-modify-write a word,
    // which is wrong for write-only ports (Genesis VDP, I/O).
    void set_byte_handlers(ReadByteHandler read, WriteByteHandler write) {
        read_byte_ = std::move(read);
        write_byte_ = std::move(write);
    }
    void set_cycle_handler(CycleHandler handler) { cycle_handler_ = std::move(handler); }
    void set_reset_instruction_handler(ResetInstructionHandler handler) {
        reset_instruction_handler_ = std::move(handler);
    }
    void set_irq_acknowledge(std::function<int(int)> handler) {
        irq_ack_ = std::move(handler);
    }
    void set_address_mask(uint32_t mask) { address_mask_ = mask & ~1u; }
    // Optional: 4 KB pages of the first megabyte (after the address mask)
    // that are plain memory, big-endian bytes, which the core then reads
    // (`read`) and writes (`write`) itself instead of calling the handlers;
    // null = through the handlers. Only for memory without side effects, as
    // the handlers would do exactly the same: it saves a std::function call
    // per access (the instruction fetches above all).
    static constexpr uint32_t kDirectPages = 256;
    static constexpr uint32_t kDirectSpan = kDirectPages << 12;
    void set_direct_page(uint32_t page, const uint8_t* read, uint8_t* write);
    // A read-only page held in two byte-wide ROMs, as most 68000 boards
    // have: `even` gives the high bytes, `odd` the low bytes (2 KB each).
    void set_direct_page_pair(uint32_t page, const uint8_t* even, const uint8_t* odd);
    // Model the 68000's two-word prefetch queue: instruction words already
    // fetched are not re-read when the program overwrites them. Self-
    // modifying copy protections (Atari ST loaders) depend on it.
    void set_prefetch_emulation(bool on) {
        prefetch_ = on;
        pq_count_ = 0;
    }
    using ExceptionHandler = std::function<void(uint32_t vector, uint32_t pc)>;
    void set_exception_handler(ExceptionHandler h) { exception_handler_ = std::move(h); }
    // Debug hook called with the PC before each instruction fetch.
    using InstructionHook = std::function<void(uint32_t pc)>;
    void set_instruction_hook(InstructionHook h) { instruction_hook_ = std::move(h); }
    using AlineHandler = std::function<bool(uint16_t opcode, uint32_t pc)>;
    void set_aline_handler(AlineHandler h) { aline_handler_ = std::move(h); }
    using EmulOpHandler = std::function<bool(uint16_t opcode)>;
    void set_emul_op_handler(EmulOpHandler h) { emul_op_handler_ = std::move(h); }
    // FD1094: CMPI.L #$xxxxFFFF, Dn is used to change encryption state.
    using CmpildHandler = std::function<void(uint8_t reg, uint32_t data)>;
    void set_cmpild_handler(CmpildHandler h) { cmpild_handler_ = std::move(h); }
    // FD1094: RTE leaves interrupt encryption mode.
    using RteHandler = std::function<void()>;
    void set_rte_handler(RteHandler h) { rte_handler_ = std::move(h); }
    // FD1094: fired when an IRQ is taken (before vector fetch).
    using IrqTakenHandler = std::function<void(int level)>;
    void set_irq_taken_handler(IrqTakenHandler h) { irq_taken_handler_ = std::move(h); }

    void reset();
    // Runs until at least `cycles` cycles have elapsed, returns the amount executed.
    int run(int cycles);

    void set_irq(int level, IrqLine state);
    void set_reset_line(IrqLine state) { reset_request_ = state; }
    void set_halt_line(IrqLine state) { halt_request_ = state; }

    uint32_t clock() const { return clock_; }
    uint32_t pc() const { return pc_.l; }
    uint32_t ppc() const { return ppc_.l; }
    uint16_t peek_word(uint32_t address) { return getword(address); }
    // True only while fetching instructions / extension words (the opcode
    // stream of the FD1089/FD1094/DECO 102 encrypted ROMs).
    bool opcode() const { return opcode_; }

    std::array<Reg32, 8> d{};
    std::array<Reg32, 8> a{};
    Reg32 pc_{};
    Reg32 ppc_{};
    Flags cc;

private:
    uint16_t getword(uint32_t address) {
        address &= address_mask_;
        if (address < kDirectSpan) {
            if (const uint8_t* page = direct_read_[address >> 12]) {
                const uint32_t o = address & 0xfff;  // even: a word never crosses a page
                if (const uint8_t* odd = direct_odd_[address >> 12])
                    return uint16_t((uint16_t(page[o >> 1]) << 8) | odd[o >> 1]);
                return uint16_t((uint16_t(page[o]) << 8) | page[o + 1]);
            }
        }
        return read_(address);
    }
    void putword(uint32_t address, uint16_t value) {
        address &= address_mask_;
        if (address < kDirectSpan) {
            if (uint8_t* page = direct_write_[address >> 12]) {
                const uint32_t o = address & 0xfff;
                page[o] = uint8_t(value >> 8);
                page[o + 1] = uint8_t(value);
                return;
            }
        }
        write_(address, value);
    }
    uint8_t getbyte(uint32_t address);
    void putbyte(uint32_t address, uint8_t value);

    uint16_t fetch_word();
    // Instruction-stream word: from the prefetch queue when it holds it.
    uint16_t prog_word(uint32_t address);
    uint32_t fetch_long();

    uint16_t get_flags() const;
    void set_flags(uint16_t value);

    // Addressing modes, `dir` is the 6 bit mode+register field.
    uint8_t read_b(uint8_t dir);
    void write_b2(uint8_t dir, uint8_t value);
    void write_b(uint8_t dir, uint8_t value);
    uint16_t read_w(uint8_t dir);
    void write_w2(uint8_t dir, uint16_t value);
    void write_w(uint8_t dir, uint16_t value);
    uint32_t read_l(uint8_t dir);
    void write_l2(uint8_t dir, uint32_t value);
    void write_l(uint8_t dir, uint32_t value);
    uint32_t read_ea(uint8_t dir);
    // Shared decoding of the extension word of d(An,ix)/d(PC,ix).
    uint32_t indexed_offset(uint32_t base);

    bool condition(uint8_t code) const;
    // One method per opcode group (the first nibble of the instruction).
    void group_0(uint16_t instruction);
    void group_1(uint16_t instruction);
    void group_2(uint16_t instruction);
    void group_3(uint16_t instruction);
    void group_4(uint16_t instruction);
    void group_5(uint16_t instruction);
    void group_6(uint16_t instruction);
    void group_7(uint16_t instruction);
    void group_8(uint16_t instruction);
    void group_9(uint16_t instruction);
    void group_a(uint16_t instruction);
    void group_b(uint16_t instruction);
    void group_c(uint16_t instruction);
    void group_d(uint16_t instruction);
    void group_e(uint16_t instruction);
    void group_f(uint16_t instruction);
    bool take_irq();
    void move_bw(uint16_t instruction, bool byte_size);
    void move_l(uint16_t instruction);
    void exception(uint32_t vector, int cycles);
    bool check_supervisor();

    static constexpr uint32_t kAddressMask = 0xfffffeu;
    uint32_t address_mask_ = kAddressMask;
    uint32_t vbr_ = 0;
    ExceptionHandler exception_handler_;
    InstructionHook instruction_hook_;
    AlineHandler aline_handler_;
    EmulOpHandler emul_op_handler_;
    CmpildHandler cmpild_handler_;
    RteHandler rte_handler_;
    IrqTakenHandler irq_taken_handler_;

    uint32_t clock_;
    Type type_;
    bool opcode_ = false;
    bool prefetch_ = false;
    uint32_t exceptions_ = 0;  // exceptions taken through exception()
    uint32_t pq_addr_ = 0;  // address of pq_val_[0]
    uint16_t pq_val_[2] = {0, 0};
    int pq_count_ = 0;
    uint32_t ea_ = 0;
    Reg32 other_sp_{};  // the stack pointer of the mode we are not in
    bool halted_ = false;
    bool stopped_ = false;
    std::array<IrqLine, 8> irq_{};
    uint8_t irq_lines_ = 0;  // bit n: irq_[n] is not Clear (take_irq's quick test)
    std::array<const uint8_t*, kDirectPages> direct_read_{};
    std::array<uint8_t*, kDirectPages> direct_write_{};
    std::array<const uint8_t*, kDirectPages> direct_odd_{};  // pair pages: the low bytes
    IrqLine reset_request_ = IrqLine::Clear;
    IrqLine halt_request_ = IrqLine::Clear;
    int cycles_ = 0;

    ReadWordHandler read_;
    WriteWordHandler write_;
    ReadByteHandler read_byte_;
    WriteByteHandler write_byte_;
    CycleHandler cycle_handler_;
    ResetInstructionHandler reset_instruction_handler_;
    std::function<int(int)> irq_ack_;
};

}  // namespace dsp
