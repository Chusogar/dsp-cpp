#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Sony SPC700, the sound CPU of the Super Famicom.
//
// An 8-bit core in the 65xx family but with its own instruction set: A, X and
// Y plus the 16-bit pair YA, a stack fixed to page 1, and a direct page that
// the P flag moves between page 0 and page 1. It owns 64 KB of private RAM;
// the top 64 bytes are covered by the IPL boot ROM until software switches it
// out, and four mailbox ports let it talk to the main CPU.
//
// Written from the published instruction set. Cycle counts come from the
// standard opcode timing table.
class Spc700 {
public:
    using ReadHandler = std::function<uint8_t(uint16_t)>;
    using WriteHandler = std::function<void(uint16_t, uint8_t)>;

    void set_memory_handlers(ReadHandler r, WriteHandler w) {
        read_ = std::move(r);
        write_ = std::move(w);
    }

    void reset();
    // Executes one instruction and returns the cycles it took.
    int step();

    uint16_t pc() const { return pc_; }

    uint8_t a = 0, x = 0, y = 0, sp = 0xef;

private:
    // Program status word.
    struct Psw {
        bool n = false, v = false, p = false, b = false;
        bool h = false, i = false, z = false, c = false;
    };

    uint8_t rd(uint16_t a) { return read_ ? read_(a) : 0; }
    void wr(uint16_t a, uint8_t v) { if (write_) write_(a, v); }
    uint8_t fetch() { return rd(pc_++); }
    uint16_t fetch16() { const uint8_t l = fetch(); return uint16_t(l | (fetch() << 8)); }
    uint16_t dp(uint8_t off) const { return uint16_t((psw_.p ? 0x100 : 0) | off); }

    void push(uint8_t v) { wr(uint16_t(0x100 | sp), v); sp--; }
    uint8_t pop() { sp++; return rd(uint16_t(0x100 | sp)); }

    uint8_t psw_byte() const;
    void set_psw(uint8_t v);

    uint8_t setnz(uint8_t v) { psw_.n = (v & 0x80) != 0; psw_.z = (v == 0); return v; }
    uint16_t setnz16(uint16_t v) { psw_.n = (v & 0x8000) != 0; psw_.z = (v == 0); return v; }

    uint8_t op_adc(uint8_t a, uint8_t b);
    uint8_t op_sbc(uint8_t a, uint8_t b);
    void op_cmp(uint8_t a, uint8_t b);
    uint8_t op_asl(uint8_t v);
    uint8_t op_lsr(uint8_t v);
    uint8_t op_rol(uint8_t v);
    uint8_t op_ror(uint8_t v);
    void branch(bool take, int& cycles);

    ReadHandler read_;
    WriteHandler write_;
    uint16_t pc_ = 0xffc0;
    Psw psw_;
    bool halted_ = false;   // SLEEP / STOP
};

}  // namespace dsp
