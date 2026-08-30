#include <cstdio>
#include "machine/eeprom_er5911.h"

namespace dsp {

void EepromEr5911::reset() {
    cs_ = 0;
    clk_ = 0;
    di_ = 0;
    state_ = kInReset;
    command_ = kInvalid;
    locked_ = false;  // start unlocked for self-test
    bits_accum_ = 0;
    command_address_accum_ = 0;
    shift_register_ = 0;
    address_ = 0;
}

void EepromEr5911::load(const uint8_t* data, int size) {
    mem_.fill(0xff);
    if (data && size > 0) {
        const int n = size < kSize ? size : kSize;
        std::memcpy(mem_.data(), data, size_t(n));
    }
}

void EepromEr5911::di_write(int state) { di_ = state ? 1 : 0; }

void EepromEr5911::cs_write(int state) {
    const int ns = state ? 1 : 0;
    if (ns == cs_) return;
    const int prev = cs_;
    cs_ = ns;
    if (!prev && cs_) handle_event(kCsRising);
    else if (prev && !cs_) handle_event(kCsFalling);
}

void EepromEr5911::clk_write(int state) {
    const int ns = state ? 1 : 0;
    if (ns == clk_) return;
    const int prev = clk_;
    clk_ = ns;
    if (!prev && clk_) handle_event(kClkRising);
    else if (prev && !clk_) handle_event(kClkFalling);
}

int EepromEr5911::do_read() const {
    if (state_ == kReadingData && (shift_register_ & 0x80000000u) == 0)
        return 0;
    return 1;
}

void EepromEr5911::parse_er5911() {
    command_ = kInvalid;
    address_ = int(command_address_accum_ & ((1u << kCommandAddressBits) - 1));
    switch (command_address_accum_ >> kCommandAddressBits) {
        case 0: {
            switch (address_ >> (kCommandAddressBits - 2)) {
                case 0: command_ = kLock; break;
                case 1: command_ = kInvalid; break;
                case 2: command_ = kEraseAll; break;
                case 3: command_ = kUnlock; break;
            }
            address_ = 0;
            break;
        }
        case 1: command_ = kWrite; break;
        case 2: command_ = kRead; break;
        case 3: command_ = kWrite; break;
    }
    address_ &= (1 << kAddressBits) - 1;
}

void EepromEr5911::execute_command() {
    parse_er5911();
    bits_accum_ = 0;
    switch (command_) {
        case kRead: {
            // Preload first byte; first CLK will shift. Dummy 0 before first CLK via
            // initial MSB handling: load with extra top 0 by using 33-bit conceptual gap.
            // Match Pascal: shift starts 0, first CLK loads. Keep Pascal behaviour.
            shift_register_ = 0;
            state_ = kReadingData;
            break;
        }
        case kWrite:
        case kWriteAll:
            shift_register_ = 0;
            state_ = kWaitForData;
            break;
        case kLock:
            locked_ = true;
            state_ = kInReset;
            break;
        case kUnlock:
            locked_ = false;
            state_ = kInReset;
            break;
        case kEraseAll:
            if (locked_) {
                state_ = kInReset;
                break;
            }
            mem_.fill(0xff);
            state_ = kWaitCompletion;
            break;
        default:
            state_ = kInReset;
            break;
    }
}

void EepromEr5911::execute_write_command() {
    if (locked_) {
        state_ = kInReset;
        return;
    }
    const uint8_t value = uint8_t(shift_register_ & 0xff);
    if (command_ == kWrite) {
        mem_[size_t(address_ % kSize)] = value;
    } else if (command_ == kWriteAll) {
        for (int i = 0; i < kSize; i++)
            mem_[size_t(i)] = uint8_t(mem_[size_t(i)] & value);
    }
    state_ = kWaitCompletion;
}

void EepromEr5911::handle_event(Event ev) {
    switch (state_) {
        case kInReset:
            if (ev == kCsRising) state_ = kWaitStartBit;
            break;

        case kWaitStartBit:
            if (ev == kClkRising && di_ == 1) {
                command_address_accum_ = 0;
                bits_accum_ = 0;
                state_ = kWaitCommand;
            } else if (ev == kCsFalling) {
                state_ = kInReset;
            }
            break;

        case kWaitCommand:
            if (ev == kClkRising) {
                command_address_accum_ = (command_address_accum_ << 1) | uint32_t(di_);
                bits_accum_++;
                if (bits_accum_ == (2 + kCommandAddressBits)) execute_command();
            } else if (ev == kCsFalling) {
                state_ = kInReset;
            }
            break;

        case kReadingData:
            if (ev == kClkRising) {
                const int bit_index = bits_accum_;
                bits_accum_++;
                // Pascal: on first clock (bit_index==0) load word into top of shift reg
                if ((bit_index % kDataBits) == 0) {
                    const int addr = (address_ + bit_index / kDataBits) & ((1 << kAddressBits) - 1);
                    const uint8_t byte = mem_[size_t(addr)];
                    shift_register_ = uint32_t(byte) << (32 - kDataBits);
                } else {
                    shift_register_ = (shift_register_ << 1) | 1u;
                }
            } else if (ev == kCsFalling) {
                state_ = kInReset;
            }
            break;

        case kWaitForData:
            if (ev == kClkRising) {
                shift_register_ = (shift_register_ << 1) | uint32_t(di_);
                bits_accum_++;
                if (bits_accum_ == kDataBits) execute_write_command();
            } else if (ev == kCsFalling) {
                state_ = kInReset;
            }
            break;

        case kWaitCompletion:
            if (ev == kCsFalling) state_ = kInReset;
            break;
    }
}

}  // namespace dsp
