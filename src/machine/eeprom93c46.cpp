#include "machine/eeprom93c46.h"

#include <cstddef>

namespace dsp {
namespace {

int calc_address_bits(int cells) {
    int value = cells - 1;
    int bits = 0;
    while (value != 0) {
        value >>= 1;
        bits++;
    }
    return bits;
}

}  // namespace

Eeprom93C46::Eeprom93C46(int data_bits) {
    data_.fill(0xff);
    data_bits_ = (data_bits == 16) ? 16 : 8;
    if (data_bits_ == 16) {
        command_address_bits_ = 6;
        address_bits_ = calc_address_bits(64);
    } else {
        command_address_bits_ = 7;
        address_bits_ = calc_address_bits(128);
    }
}

void Eeprom93C46::reset() {
    state_ = State::InReset;
    locked_ = true;
    bits_accum_ = 0;
    command_address_accum_ = 0;
    command_ = Command::Invalid;
    address_ = 0;
    shift_register_ = 0;
}

uint8_t Eeprom93C46::do_read() const {
    if (state_ == State::ReadingData && (shift_register_ & 0x80000000u) == 0) return 0;
    return 1;
}

void Eeprom93C46::cs_write(uint8_t state) {
    state &= 1;
    if (state == cs_state_) return;
    cs_state_ = state;
    handle_event(cs_state_ != 0 ? kCsRising : kCsFalling);
}

void Eeprom93C46::clk_write(uint8_t state) {
    state &= 1;
    if (state == clk_state_) return;
    clk_state_ = state;
    handle_event(clk_state_ != 0 ? kClkRising : kClkFalling);
}

void Eeprom93C46::di_write(uint8_t state) { di_state_ = uint8_t(state & 1); }

void Eeprom93C46::write_cell(uint16_t address, uint16_t value) {
    if (data_bits_ == 16) {
        const size_t index = size_t(address & 0x3f) * 2;
        data_[index] = uint8_t(value >> 8);
        data_[index + 1] = uint8_t(value);
        return;
    }
    data_[size_t(address & 0x7f)] = uint8_t(value);
}

uint16_t Eeprom93C46::read_cell(uint16_t address) const {
    if (data_bits_ == 16) {
        const size_t index = size_t(address & 0x3f) * 2;
        return uint16_t((uint16_t(data_[index]) << 8) | data_[index + 1]);
    }
    return data_[size_t(address & 0x7f)];
}

void Eeprom93C46::parse_command_and_address() {
    command_ = Command::Invalid;
    address_ = uint16_t(command_address_accum_ & ((1u << command_address_bits_) - 1));
    switch (command_address_accum_ >> command_address_bits_) {
        case 0:
            switch (address_ >> (command_address_bits_ - 2)) {
                case 0: command_ = Command::Lock; break;
                case 1: command_ = Command::WriteAll; break;
                case 2: command_ = Command::EraseAll; break;
                case 3: command_ = Command::Unlock; break;
            }
            address_ = 0;
            break;
        case 1: command_ = Command::Write; break;
        case 2: command_ = Command::Read; break;
        case 3: command_ = Command::Erase; break;
    }
}

void Eeprom93C46::execute_write_command() {
    switch (command_) {
        case Command::Write:
            if (locked_) {
                state_ = State::InReset;
                return;
            }
            write_cell(address_, uint16_t(shift_register_));
            state_ = State::WaitForCompletion;
            break;
        case Command::WriteAll:
            if (locked_) {
                state_ = State::InReset;
                return;
            }
            for (uint16_t cell = 0; cell < (1u << address_bits_); cell++) {
                write_cell(cell, uint16_t(read_cell(cell) & shift_register_));
            }
            state_ = State::WaitForCompletion;
            break;
        default: break;
    }
}

void Eeprom93C46::execute_command() {
    parse_command_and_address();
    bits_accum_ = 0;
    switch (command_) {
        case Command::Read:
            shift_register_ = 0;
            state_ = State::ReadingData;
            break;
        case Command::Write:
        case Command::WriteAll:
            shift_register_ = 0;
            state_ = State::WaitForData;
            break;
        case Command::Erase:
            if (locked_) {
                state_ = State::InReset;
                return;
            }
            write_cell(address_, 0xffff);
            state_ = State::WaitForCompletion;
            break;
        case Command::Lock:
            locked_ = true;
            state_ = State::InReset;
            break;
        case Command::Unlock:
            locked_ = false;
            state_ = State::InReset;
            break;
        case Command::EraseAll:
            if (locked_) {
                state_ = State::InReset;
                return;
            }
            for (uint16_t cell = 0; cell < (1u << address_bits_); cell++) write_cell(cell, 0xffff);
            state_ = State::WaitForCompletion;
            break;
        default: break;
    }
}

void Eeprom93C46::handle_event(uint8_t event) {
    switch (state_) {
        case State::InReset:
            if (event == kCsRising) state_ = State::WaitForStartBit;
            break;
        case State::WaitForStartBit:
            if (event == kClkRising && di_state_ != 0) {
                command_address_accum_ = 0;
                bits_accum_ = 0;
                state_ = State::WaitForCommand;
            } else if (event == kCsFalling) {
                state_ = State::InReset;
            }
            break;
        case State::WaitForCommand:
            if (event == kClkRising) {
                command_address_accum_ = (command_address_accum_ << 1) | di_state_;
                bits_accum_ = uint8_t(bits_accum_ + 1);
                if (bits_accum_ == uint8_t(2 + command_address_bits_)) execute_command();
            } else if (event == kCsFalling) {
                state_ = State::InReset;
            }
            break;
        case State::ReadingData:
            if (event == kClkRising) {
                uint8_t bit_index = bits_accum_;
                bits_accum_ = uint8_t(bits_accum_ + 1);
                if ((bit_index % uint8_t(data_bits_)) == 0 && bit_index == 0) {
                    uint16_t cell = uint16_t(
                        (address_ + bits_accum_ / uint8_t(data_bits_)) & ((1u << address_bits_) - 1));
                    shift_register_ = uint32_t(read_cell(cell)) << (32 - data_bits_);
                } else {
                    shift_register_ = (shift_register_ << 1) | 1;
                }
            } else if (event == kCsFalling) {
                state_ = State::InReset;
            }
            break;
        case State::WaitForData:
            if (event == kClkRising) {
                shift_register_ = (shift_register_ << 1) | di_state_;
                bits_accum_ = uint8_t(bits_accum_ + 1);
                if (bits_accum_ == uint8_t(data_bits_)) execute_write_command();
            } else if (event == kCsFalling) {
                state_ = State::InReset;
            }
            break;
        case State::WaitForCompletion:
            if (event == kCsFalling) state_ = State::InReset;
            break;
    }
}

}  // namespace dsp
