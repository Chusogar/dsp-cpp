// PlayStation digital controller via JOY_TX/RX/STAT/CTRL/BAUD.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <cstdint>
#include <deque>

namespace dsp {

// Digital pad button bits (active low when pressed). Matching ProjectPSX
// GamepadInputsEnum / hardware bit layout.
enum PsxPadButton : uint16_t {
    kPsxPadSelect = 0x0001,
    kPsxPadL3 = 0x0002,
    kPsxPadR3 = 0x0004,
    kPsxPadStart = 0x0008,
    kPsxPadUp = 0x0010,
    kPsxPadRight = 0x0020,
    kPsxPadDown = 0x0040,
    kPsxPadLeft = 0x0080,
    kPsxPadL2 = 0x0100,
    kPsxPadR2 = 0x0200,
    kPsxPadL1 = 0x0400,
    kPsxPadR1 = 0x0800,
    kPsxPadTriangle = 0x1000,
    kPsxPadCircle = 0x2000,
    kPsxPadCross = 0x4000,
    kPsxPadSquare = 0x8000,
};

class PsxJoypad {
public:
    void reset();

    // buttons_: bits clear = pressed (active low).
    void set_buttons(uint16_t buttons_active_low) { buttons_ = buttons_active_low; }
    uint16_t buttons() const { return buttons_; }

    uint32_t load(uint32_t addr);
    void write(uint32_t addr, uint32_t value);

    // Returns true when controller IRQ should be raised.
    bool tick();

private:
    enum class Device { None, Controller, MemoryCard };
    enum class CtrlMode { Idle, Connected, Transferring };

    uint8_t process_controller(uint8_t tx);
    void generate_response();
    void reset_controller_idle();
    void set_ctrl(uint32_t value);
    void set_mode(uint32_t value);
    uint32_t get_ctrl() const;
    uint32_t get_mode() const;
    uint32_t get_stat();
    void reload_timer();

    uint8_t tx_data_ = 0xFF;
    uint8_t rx_data_ = 0xFF;
    bool fifo_full_ = false;

    bool tx_ready1_ = true;
    bool tx_ready2_ = true;
    bool rx_parity_error_ = false;
    bool ack_level_ = false;
    bool irq_request_ = false;
    int baud_timer_ = 0;

    uint32_t baud_factor_ = 0;
    uint32_t char_length_ = 0;
    bool parity_enable_ = false;
    bool parity_odd_ = false;
    bool clk_polarity_ = false;

    bool tx_enable_ = false;
    bool joy_output_ = false;
    bool rx_enable_ = false;
    bool ctrl_unk3_ = false;
    bool ctrl_ack_ = false;
    bool ctrl_unk5_ = false;
    bool ctrl_reset_ = false;
    uint32_t rx_irq_mode_ = 0;
    bool tx_irq_enable_ = false;
    bool rx_irq_enable_ = false;
    bool ack_irq_enable_ = false;
    uint32_t slot_ = 0;

    uint16_t baud_ = 0;
    Device device_ = Device::None;
    CtrlMode ctrl_mode_ = CtrlMode::Idle;
    bool ctrl_ack_flag_ = false;
    uint16_t buttons_ = 0xFFFF;
    std::deque<uint8_t> transfer_fifo_;
    int ack_counter_ = 0;
};

}  // namespace dsp
