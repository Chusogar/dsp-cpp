// PlayStation digital controller via JOY_TX/RX/STAT/CTRL/BAUD.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#include "machine/psx_joypad.h"

namespace dsp {

namespace {
constexpr uint16_t kControllerType = 0x5A41;  // digital
}

void PsxJoypad::reset() {
    *this = PsxJoypad{};
}

void PsxJoypad::reload_timer() {
    baud_timer_ = int(baud_ * baud_factor_) & ~1;
}

bool PsxJoypad::tick() {
    if (ack_counter_ > 0) {
        ack_counter_ -= 100;
        if (ack_counter_ == 0) {
            ack_level_ = false;
            irq_request_ = true;
        }
    }
    return irq_request_;
}

void PsxJoypad::reset_controller_idle() {
    ctrl_mode_ = CtrlMode::Idle;
    transfer_fifo_.clear();
    ctrl_ack_flag_ = false;
}

void PsxJoypad::generate_response() {
    transfer_fifo_.clear();
    transfer_fifo_.push_back(uint8_t(kControllerType & 0xFF));
    transfer_fifo_.push_back(uint8_t(kControllerType >> 8));
    transfer_fifo_.push_back(uint8_t(buttons_ & 0xFF));
    transfer_fifo_.push_back(uint8_t(buttons_ >> 8));
}

uint8_t PsxJoypad::process_controller(uint8_t b) {
    switch (ctrl_mode_) {
        case CtrlMode::Idle:
            if (b == 0x01) {
                ctrl_mode_ = CtrlMode::Connected;
                ctrl_ack_flag_ = true;
                return 0xFF;
            }
            transfer_fifo_.clear();
            ctrl_ack_flag_ = false;
            return 0xFF;

        case CtrlMode::Connected:
            if (b == 0x42) {
                ctrl_mode_ = CtrlMode::Transferring;
                generate_response();
                ctrl_ack_flag_ = true;
                const uint8_t data = transfer_fifo_.front();
                transfer_fifo_.pop_front();
                return data;
            }
            ctrl_mode_ = CtrlMode::Idle;
            transfer_fifo_.clear();
            ctrl_ack_flag_ = false;
            return 0xFF;

        case CtrlMode::Transferring: {
            const uint8_t data = transfer_fifo_.front();
            transfer_fifo_.pop_front();
            ctrl_ack_flag_ = !transfer_fifo_.empty();
            if (!ctrl_ack_flag_) ctrl_mode_ = CtrlMode::Idle;
            return data;
        }
    }
    return 0xFF;
}

void PsxJoypad::write(uint32_t addr, uint32_t value) {
    switch (addr & 0xFF) {
        case 0x40:
            tx_data_ = uint8_t(value);
            rx_data_ = 0xFF;
            fifo_full_ = true;
            tx_ready1_ = true;
            tx_ready2_ = false;
            if (joy_output_) {
                tx_ready2_ = true;
                if (slot_ == 1) {
                    rx_data_ = 0xFF;
                    ack_level_ = false;
                    return;
                }
                if (device_ == Device::None) {
                    if (value == 0x01) device_ = Device::Controller;
                    else if (value == 0x81) device_ = Device::MemoryCard;
                }
                if (device_ == Device::Controller) {
                    rx_data_ = process_controller(tx_data_);
                    ack_level_ = ctrl_ack_flag_;
                    if (ack_level_) ack_counter_ = 500;
                } else if (device_ == Device::MemoryCard) {
                    // Minimal memory-card stub: no ACK, FF response.
                    rx_data_ = 0xFF;
                    ack_level_ = false;
                } else {
                    ack_level_ = false;
                }
                if (!ack_level_) device_ = Device::None;
            } else {
                device_ = Device::None;
                reset_controller_idle();
                ack_level_ = false;
            }
            break;
        case 0x48:
            set_mode(value);
            break;
        case 0x4A:
            set_ctrl(value);
            break;
        case 0x4E:
            baud_ = uint16_t(value);
            reload_timer();
            break;
        default:
            break;
    }
}

void PsxJoypad::set_ctrl(uint32_t value) {
    tx_enable_ = (value & 1) != 0;
    joy_output_ = ((value >> 1) & 1) != 0;
    rx_enable_ = ((value >> 2) & 1) != 0;
    ctrl_unk3_ = ((value >> 3) & 1) != 0;
    ctrl_ack_ = ((value >> 4) & 1) != 0;
    ctrl_unk5_ = ((value >> 5) & 1) != 0;
    ctrl_reset_ = ((value >> 6) & 1) != 0;
    rx_irq_mode_ = (value >> 8) & 3;
    tx_irq_enable_ = ((value >> 10) & 1) != 0;
    rx_irq_enable_ = ((value >> 11) & 1) != 0;
    ack_irq_enable_ = ((value >> 12) & 1) != 0;
    slot_ = (value >> 13) & 1;

    if (ctrl_ack_) {
        rx_parity_error_ = false;
        irq_request_ = false;
        ctrl_ack_ = false;
    }
    if (ctrl_reset_) {
        device_ = Device::None;
        reset_controller_idle();
        fifo_full_ = false;
        set_mode(0);
        set_ctrl(0);
        baud_ = 0;
        rx_data_ = 0xFF;
        tx_data_ = 0xFF;
        tx_ready1_ = true;
        tx_ready2_ = true;
        ctrl_reset_ = false;
    }
    if (!joy_output_) {
        device_ = Device::None;
        reset_controller_idle();
    }
}

void PsxJoypad::set_mode(uint32_t value) {
    baud_factor_ = value & 3;
    char_length_ = (value >> 2) & 3;
    parity_enable_ = ((value >> 4) & 1) != 0;
    parity_odd_ = ((value >> 5) & 1) != 0;
    clk_polarity_ = ((value >> 8) & 1) != 0;
}

uint32_t PsxJoypad::load(uint32_t addr) {
    switch (addr & 0xFF) {
        case 0x40:
            fifo_full_ = false;
            return rx_data_;
        case 0x44:
            return get_stat();
        case 0x48:
            return get_mode();
        case 0x4A:
            return get_ctrl();
        case 0x4E:
            return baud_;
        default:
            return 0xFFFFFFFFu;
    }
}

uint32_t PsxJoypad::get_ctrl() const {
    uint32_t v = 0;
    v |= tx_enable_ ? 1u : 0u;
    v |= (joy_output_ ? 1u : 0u) << 1;
    v |= (rx_enable_ ? 1u : 0u) << 2;
    v |= (ctrl_unk3_ ? 1u : 0u) << 3;
    v |= (ctrl_unk5_ ? 1u : 0u) << 5;
    v |= rx_irq_mode_ << 8;
    v |= (tx_irq_enable_ ? 1u : 0u) << 10;
    v |= (rx_irq_enable_ ? 1u : 0u) << 11;
    v |= (ack_irq_enable_ ? 1u : 0u) << 12;
    v |= slot_ << 13;
    return v;
}

uint32_t PsxJoypad::get_mode() const {
    uint32_t v = 0;
    v |= baud_factor_;
    v |= char_length_ << 2;
    v |= (parity_enable_ ? 1u : 0u) << 4;
    v |= (parity_odd_ ? 1u : 0u) << 5;
    v |= (clk_polarity_ ? 1u : 0u) << 8;
    return v;
}

uint32_t PsxJoypad::get_stat() {
    uint32_t v = 0;
    v |= tx_ready1_ ? 1u : 0u;
    v |= (fifo_full_ ? 1u : 0u) << 1;
    v |= (tx_ready2_ ? 1u : 0u) << 2;
    v |= (rx_parity_error_ ? 1u : 0u) << 3;
    v |= (ack_level_ ? 1u : 0u) << 7;
    v |= (irq_request_ ? 1u : 0u) << 9;
    v |= uint32_t(baud_timer_) << 11;
    ack_level_ = false;
    return v;
}

}  // namespace dsp
