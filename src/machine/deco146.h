#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Data East 146 / 104 protection chips, ported from dsp-emulator deco_146.pas
// and deco_104.pas (MAME deco146.cpp / deco104.cpp).
//
// A bank of 0x400 read ports, each one returning a scrambled (bit reordered,
// optionally XORed and NANDed) copy of a word the CPU wrote to the chip, or of
// one of three input ports. Writes to dedicated ports load the XOR and NAND
// registers or the sound latch.
class Deco146 {
public:
    enum class Type { Deco146, Deco104 };
    static constexpr int kInterfaceScrambleReverse = 1;
    static constexpr int kUseMagicAddressXor = 2;
    static constexpr int kInterfaceScrambleInterleave = 4;

    struct Entry {
        int write_offset;  // -1, -2, -3: input ports A, B, C
        uint8_t mapping[16];
        bool use_xor, use_nand;
    };

    using PortRead = std::function<uint16_t()>;
    using SoundLatchWrite = std::function<void(uint8_t)>;

    explicit Deco146(Type type, int props = 0);

    void set_port_handlers(PortRead a, PortRead b, PortRead c) {
        port_a_ = std::move(a);
        port_b_ = std::move(b);
        port_c_ = std::move(c);
    }
    void set_soundlatch_handler(SoundLatchWrite handler) { soundlatch_ = std::move(handler); }

    void reset();
    uint16_t read_data(uint16_t address);
    // `mem_mask` selects the bytes written (0xff00 / 0x00ff for byte accesses).
    void write_data(uint16_t address, uint16_t data, uint16_t mem_mask = 0xffff);

private:
    uint16_t scramble(uint16_t address) const;
    uint16_t read_protport(uint16_t address);
    void write_protport(uint16_t address, uint16_t data, uint16_t mem_mask);

    const Entry* table_;
    uint8_t bankswitch_swap_read_address_, xor_port_, mask_port_, soundlatch_port_;
    uint16_t magic_read_address_xor_;
    bool magic_read_address_xor_enabled_;
    std::array<uint8_t, 10> addrswap_{};
    std::array<uint8_t, 6> region_selects_{};
    uint8_t current_rambank_ = 0;
    bool latchflag_ = false;
    uint16_t nand_ = 0, xor_ = 0, latchaddr_ = 0xffff, latchdata_ = 0;
    std::array<uint16_t, 0x80> rambank0_{}, rambank1_{};
    PortRead port_a_, port_b_, port_c_;
    SoundLatchWrite soundlatch_;
};

}  // namespace dsp
