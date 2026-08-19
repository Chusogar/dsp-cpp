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

    enum class Type { M68000, M68010 };

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
    // True while fetching instructions / extension words (FD1089 opcode stream).
    bool opcode() const { return opcode_; }

    std::array<Reg32, 8> d{};
    std::array<Reg32, 8> a{};
    Reg32 pc_{};
    Reg32 ppc_{};
    Flags cc;

private:
    uint16_t getword(uint32_t address) { return read_(address & kAddressMask); }
    void putword(uint32_t address, uint16_t value) { write_(address & kAddressMask, value); }
    uint8_t getbyte(uint32_t address);
    void putbyte(uint32_t address, uint8_t value);

    uint16_t fetch_word();
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

    uint32_t clock_;
    Type type_;
    bool opcode_ = true;
    uint32_t ea_ = 0;
    Reg32 other_sp_{};  // the stack pointer of the mode we are not in
    bool halted_ = false;
    bool stopped_ = false;
    std::array<IrqLine, 8> irq_{};
    IrqLine reset_request_ = IrqLine::Clear;
    IrqLine halt_request_ = IrqLine::Clear;
    int cycles_ = 0;

    ReadWordHandler read_;
    WriteWordHandler write_;
    ReadByteHandler read_byte_;
    WriteByteHandler write_byte_;
    CycleHandler cycle_handler_;
};

}  // namespace dsp
