#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "cpu/irq_line.h"
#include "cpu/m6805.h"

namespace dsp {

// Taito MC68705 host interface, ported from
// dsp-emulator src/arcade/misc/taito_68705.pas
//
// mcu_type:
//   Standard (0)  — Return of Invaders, Bubble Bobble, etc.
//   TigerHeli (1) — inverted status bits + port A write side-effect
//   Arkanoid (2)  — status on port C, custom port B read callback
class Taito68705 {
public:
    enum class Type : uint8_t { Standard = 0, TigerHeli = 1, Arkanoid = 2 };

    using MiscCallback = std::function<void(int pos, uint8_t value)>;
    using ArkanoidRead = std::function<uint8_t()>;

    explicit Taito68705(uint32_t clock, Type type = Type::Standard);

    void reset();
    void run(int cycles);
    void set_reset(bool held);  // true = held in reset

    // Host (main CPU) side
    uint8_t read();           // clears mcu_sent
    void write(uint8_t value);  // sets main_sent, asserts MCU IRQ

    bool main_sent() const { return main_sent_; }
    bool mcu_sent() const { return mcu_sent_; }

    // MCU ROM buffer (0x800 bytes). Load firmware here before reset().
    uint8_t* rom_data() { return mem_.data(); }
    const uint8_t* rom_data() const { return mem_.data(); }
    static constexpr size_t kRomSize = 0x800;

    void set_misc_callback(MiscCallback cb) { misc_call_ = std::move(cb); }
    void set_arkanoid_read(ArkanoidRead cb) { arkanoid_call_ = std::move(cb); }

private:
    uint8_t cpu_read(uint16_t address);
    void cpu_write(uint16_t address, uint8_t value);
    uint8_t cpu_read_arkanoid(uint16_t address);
    void cpu_write_arkanoid(uint16_t address, uint8_t value);

    uint8_t read_port_c_status() const;

    M6805 cpu_;
    Type type_;
    bool reset_held_ = false;

    std::array<uint8_t, 0x800> mem_{};

    uint8_t port_a_in_ = 0, port_a_out_ = 0;
    uint8_t port_b_in_ = 0, port_b_out_ = 0;
    uint8_t port_c_in_ = 0, port_c_out_ = 0;
    uint8_t ddr_a_ = 0, ddr_b_ = 0, ddr_c_ = 0;
    uint8_t from_main_ = 0, from_mcu_ = 0;
    bool main_sent_ = false;
    bool mcu_sent_ = false;

    MiscCallback misc_call_;
    ArkanoidRead arkanoid_call_;
};

}  // namespace dsp
