#include "machine/mc6845.h"

namespace dsp {

Mc6845::Mc6845() { reset(); }

void Mc6845::reset() {
    regs_.fill(0);
    // Typical CPC defaults (also set by firmware after boot).
    regs_[0] = 63;
    regs_[1] = 40;
    regs_[2] = 46;
    regs_[3] = 0x8e;
    regs_[4] = 38;
    regs_[5] = 0;
    regs_[6] = 25;
    regs_[7] = 30;
    regs_[9] = 7;
    regs_[12] = 0x30;
    regs_[13] = 0x00;

    reg_ = 0;
    character_counter_ = 0;
    hsync_counter_ = 0;
    vsync_counter_ = 0;
    state_row_address_ = 0;
    adj_count_ = 0;
    char_crt_ = 0;
    line_address_ = uint16_t((regs_[12] << 8) | regs_[13]);
    end_of_line_address_ = line_address_;
    state_refresh_address_ = line_address_;
    state_hsync_ = false;
    state_vsync_ = false;
    was_hsync_ = false;
    was_vsync_ = false;
    is_in_adjustment_period_ = false;
    line_is_visible_ = false;
    next_line_is_visible_ = true;
    next_line_no_visible_ = false;
}

void Mc6845::write(uint8_t value) { write_reg(reg_, value); }

void Mc6845::write_reg(uint8_t index, uint8_t value) {
    if (index >= 16) {
        // R16/R17 light pen — ignore writes on most implementations.
        if (index < 32) regs_[index] = value;
        return;
    }
    const uint8_t masked = value & kMasks[index];
    if (regs_[index] == masked) return;
    regs_[index] = masked;

    switch (index) {
        case 5:
            // Changing vertical adjust aborts the current adjust period.
            is_in_adjustment_period_ = false;
            adj_count_ = 0;
            break;
        case 12:
        case 13:
            // Start address is latched at the end of the frame (adjust).
            break;
        default:
            break;
    }
}

uint8_t Mc6845::read() const {
    // Status/register reads: only R12–R17 are readable on HD6845S (CPC).
    if (reg_ >= 12 && reg_ <= 17) return regs_[reg_];
    return 0;
}

void Mc6845::adjust() {
    if (adj_count_ == regs_[5]) {
        is_in_adjustment_period_ = false;
        line_address_ = uint16_t((regs_[12] << 8) | regs_[13]);
        state_refresh_address_ = line_address_;
        adj_count_ = 0;
        next_line_is_visible_ = true;
    } else {
        adj_count_ = uint8_t((adj_count_ + 1) & 0x1f);
    }
}

void Mc6845::end_of_line() {
    if (next_line_is_visible_) {
        line_is_visible_ = true;
        next_line_is_visible_ = false;
    }
    if (next_line_no_visible_) {
        line_is_visible_ = false;
        next_line_no_visible_ = false;
    }

    // VSYNC duration (R3 high nibble); 0 means 16.
    if (state_vsync_) {
        uint8_t len = regs_[3] >> 4;
        if (len == 0) len = 16;
        if (vsync_counter_ == len) {
            state_vsync_ = false;
            if (vsync_cb_) vsync_cb_(false);
        } else {
            ++vsync_counter_;
        }
    }

    // VSYNC start when vertical counter reaches R7.
    if (regs_[4] >= regs_[7]) {
        if (char_crt_ == regs_[7] && !state_vsync_) {
            state_vsync_ = true;
            vsync_counter_ = 0;
            if (vsync_cb_) vsync_cb_(true);
        }
    }

    if (is_in_adjustment_period_) {
        adjust();
    } else {
        if (state_row_address_ == regs_[9]) {
            state_row_address_ = 0;
            line_address_ = end_of_line_address_;
            if (char_crt_ == regs_[4]) {
                is_in_adjustment_period_ = true;
                adj_count_ = 0;
                char_crt_ = 0;
                adjust();
            } else {
                char_crt_ = uint8_t((char_crt_ + 1) & 0x7f);
            }
        } else {
            state_row_address_ = uint8_t((state_row_address_ + 1) & 0x1f);
        }
        if (char_crt_ == regs_[6]) next_line_no_visible_ = true;
    }

    was_vsync_ = state_vsync_;
}

void Mc6845::tick() {
    // Display enable for this character cell.
    const bool de = line_is_visible_ && !state_hsync_ && !state_vsync_ &&
                    character_counter_ < regs_[1];

    if (scan_cb_) {
        scan_cb_(de, state_hsync_, state_vsync_, state_refresh_address_, state_row_address_);
    }

    // HSYNC generation.
    if (state_hsync_) {
        uint8_t len = regs_[3] & 0x0f;
        if (len == 0) len = 16;
        if (hsync_counter_ == len) {
            state_hsync_ = false;
        } else {
            ++hsync_counter_;
        }
    }
    if (regs_[0] >= regs_[2]) {
        if (character_counter_ == regs_[2]) {
            hsync_counter_ = 0;
            state_hsync_ = true;
        }
    }

    // Horizontal address advance.
    if (character_counter_ == regs_[1]) {
        end_of_line_address_ = state_refresh_address_;
    } else {
        state_refresh_address_ = uint16_t((state_refresh_address_ + 1) & 0x3fff);
    }

    if (character_counter_ == regs_[0]) {
        end_of_line();
        character_counter_ = 0;
        state_refresh_address_ = line_address_;
    } else {
        ++character_counter_;
    }

    // Falling edge of HSYNC → notify GA (IRQ counter).
    if (was_hsync_ && !state_hsync_) {
        if (hsync_end_cb_) hsync_end_cb_();
    }
    was_hsync_ = state_hsync_;
}

}  // namespace dsp
