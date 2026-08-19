#include "machine/via6522.h"

namespace dsp {
namespace {

bool pa_latch(uint8_t acr) { return (acr & 0x01) != 0; }
bool pb_latch(uint8_t acr) { return (acr & 0x02) != 0; }
bool t1_continuous(uint8_t acr) { return (acr & 0x40) != 0; }
bool t1_set_pb7(uint8_t acr) { return (acr & 0x80) != 0; }
bool t2_count_pb6(uint8_t acr) { return (acr & 0x20) != 0; }

uint8_t sr_mode(uint8_t acr) { return uint8_t((acr >> 2) & 7); }

bool ca1_pos(uint8_t pcr) { return (pcr & 0x01) != 0; }
bool cb1_pos(uint8_t pcr) { return (pcr & 0x10) != 0; }

bool ca2_fix_out(uint8_t pcr) { return (pcr & 0x0C) == 0x0C; }
bool cb2_fix_out(uint8_t pcr) { return (pcr & 0xC0) == 0xC0; }
bool ca2_pulse(uint8_t pcr) { return (pcr & 0x0E) == 0x0A; }
bool cb2_pulse(uint8_t pcr) { return (pcr & 0xE0) == 0xA0; }
bool ca2_handshake(uint8_t pcr) { return (pcr & 0x0C) == 0x08; }
bool cb2_handshake(uint8_t pcr) { return (pcr & 0xC0) == 0x80; }
bool ca2_ind_irq(uint8_t pcr) { return (pcr & 0x0A) == 0x02; }
bool cb2_ind_irq(uint8_t pcr) { return (pcr & 0xA0) == 0x20; }

uint8_t ca2_level(uint8_t pcr) { return uint8_t((pcr >> 1) & 1); }
uint8_t cb2_level(uint8_t pcr) { return uint8_t((pcr >> 5) & 1); }

}  // namespace

Via6522::Via6522(uint32_t clock) : clock_(clock) { reset(); }

void Via6522::set_port_a(PortRead in, PortWrite out) {
    pa_in_ = std::move(in);
    pa_out_ = std::move(out);
}
void Via6522::set_port_b(PortRead in, PortWrite out) {
    pb_in_ = std::move(in);
    pb_out_ = std::move(out);
}

void Via6522::reset() {
    out_a_ = out_b_ = 0;
    in_a_ = in_b_ = latch_a_ = latch_b_ = 0xFF;
    ddr_a_ = ddr_b_ = 0;
    t1ll_ = 0xF3;
    t1lh_ = 0xB5;
    t2ll_ = t2lh_ = 0xFF;
    t1_ = uint16_t(t1ll_ | (uint16_t(t1lh_) << 8));
    t2_ = 0xFFFF;
    t1_active_ = t2_active_ = false;
    t1_pb7_ = 1;
    t2_pb6_prev_ = true;
    sr_ = 0;
    shift_count_ = 0;
    shift_phase_ = 0;
    shift_out_bit_ = true;
    acr_ = pcr_ = ier_ = ifr_ = 0;
    in_ca1_ = in_ca2_ = in_cb1_ = in_cb2_ = false;
    out_ca2_ = out_cb1_ = out_cb2_ = true;
}

void Via6522::set_int(uint8_t bits) {
    ifr_ = uint8_t(ifr_ | (bits & 0x7F));
    output_irq();
}

void Via6522::clear_int(uint8_t bits) {
    ifr_ = uint8_t(ifr_ & ~(bits & 0x7F));
    output_irq();
}

void Via6522::output_irq() {
    if (ifr_ & ier_ & 0x7F) {
        ifr_ = uint8_t(ifr_ | kIntAny);
        if (irq_cb_) irq_cb_(IrqLine::Assert);
    } else {
        ifr_ = uint8_t(ifr_ & ~kIntAny);
        if (irq_cb_) irq_cb_(IrqLine::Clear);
    }
}

uint8_t Via6522::out_a() const {
    return uint8_t((out_a_ & ddr_a_) | (in_a_ & ~ddr_a_));
}

uint8_t Via6522::out_b() const {
    uint8_t v = uint8_t((out_b_ & ddr_b_) | (in_b_ & ~ddr_b_));
    if (t1_set_pb7(acr_)) {
        // PB7 forced by T1
        v = uint8_t((v & 0x7F) | (t1_pb7_ << 7));
    }
    return v;
}

void Via6522::output_pa() {
    if (pa_out_) pa_out_(out_a());
}

void Via6522::output_pb() {
    if (pb_out_) pb_out_(out_b());
}

void Via6522::output_ca2(bool level) {
    if (out_ca2_ == level) return;
    out_ca2_ = level;
    if (ca2_handler_) ca2_handler_(level);
}

void Via6522::output_cb2(bool level) {
    if (out_cb2_ == level) return;
    out_cb2_ = level;
    if (cb2_handler_) cb2_handler_(level);
}

uint8_t Via6522::input_pa() {
    const uint8_t raw = pa_in_ ? pa_in_() : in_a_;
    in_a_ = raw;
    if (pa_latch(acr_)) return latch_a_;
    return uint8_t((out_a_ & ddr_a_) | (raw & ~ddr_a_));
}

uint8_t Via6522::input_pb() {
    const uint8_t raw = pb_in_ ? pb_in_() : in_b_;
    in_b_ = raw;
    uint8_t v = pb_latch(acr_) ? latch_b_ : raw;
    v = uint8_t((out_b_ & ddr_b_) | (v & ~ddr_b_));
    if (t1_set_pb7(acr_)) v = uint8_t((v & 0x7F) | (t1_pb7_ << 7));
    return v;
}

void Via6522::write_pa(uint8_t value) {
    in_a_ = value;
    if (pa_latch(acr_) == false) latch_a_ = value;
}

void Via6522::write_pb(uint8_t value) {
    in_b_ = value;
    if (pb_latch(acr_) == false) latch_b_ = value;
}

void Via6522::set_pb_line(int line, bool state) {
    if (line < 0 || line > 7) return;
    if (state)
        in_b_ = uint8_t(in_b_ | (1 << line));
    else
        in_b_ = uint8_t(in_b_ & ~(1 << line));
}

void Via6522::write_ca1(bool state) {
    if (state == in_ca1_) return;
    const bool rise = state && !in_ca1_;
    const bool fall = !state && in_ca1_;
    in_ca1_ = state;
    const bool trigger = ca1_pos(pcr_) ? rise : fall;
    if (trigger) {
        if (pa_latch(acr_)) latch_a_ = pa_in_ ? pa_in_() : in_a_;
        set_int(kIntCA1);
        // Handshake: CA2 goes high after CA1 active edge when auto-HS
        if (ca2_handshake(pcr_)) output_ca2(true);
    }
}

void Via6522::write_ca2(bool state) {
    if (state == in_ca2_) return;
    const bool rise = state && !in_ca2_;
    const bool fall = !state && in_ca2_;
    in_ca2_ = state;
    // Independent interrupt on CA2 when input mode
    if (!ca2_fix_out(pcr_) && !ca2_pulse(pcr_) && !ca2_handshake(pcr_)) {
        const bool pos = (pcr_ & 0x04) != 0;
        if ((pos && rise) || (!pos && fall)) set_int(kIntCA2);
    } else if (ca2_ind_irq(pcr_)) {
        if (fall) set_int(kIntCA2);
    }
}

void Via6522::write_cb1(bool state) {
    if (state == in_cb1_) return;
    const bool rise = state && !in_cb1_;
    const bool fall = !state && in_cb1_;
    in_cb1_ = state;
    const bool trigger = cb1_pos(pcr_) ? rise : fall;
    if (trigger) {
        if (pb_latch(acr_)) latch_b_ = pb_in_ ? pb_in_() : in_b_;
        set_int(kIntCB1);
        if (cb2_handshake(pcr_)) output_cb2(true);
        // External shift clock on CB1
        const uint8_t mode = sr_mode(acr_);
        if (mode == 1 || mode == 2 || mode == 3 || mode == 5 || mode == 6 ||
            mode == 7) {
            // modes 1,2,3,5,6,7 may use CB1 as shift clock depending on variant;
            // classic: modes 0x06/0x07 (ext) clock on CB1
            if (mode == 6 || mode == 7) shift_clock();
        }
    }
}

void Via6522::write_cb2(bool state) {
    if (state == in_cb2_) return;
    const bool rise = state && !in_cb2_;
    const bool fall = !state && in_cb2_;
    in_cb2_ = state;
    if (!cb2_fix_out(pcr_) && !cb2_pulse(pcr_) && !cb2_handshake(pcr_)) {
        const bool pos = (pcr_ & 0x40) != 0;
        if ((pos && rise) || (!pos && fall)) set_int(kIntCB2);
    } else if (cb2_ind_irq(pcr_)) {
        if (fall) set_int(kIntCB2);
    }
}

void Via6522::tick_t1() {
    if (!t1_active_) return;
    if (t1_ == 0) {
        set_int(kIntT1);
        if (t1_continuous(acr_)) {
            t1_ = uint16_t(t1ll_ | (uint16_t(t1lh_) << 8));
            t1_pb7_ = uint8_t(t1_pb7_ ^ 1);
            if (t1_set_pb7(acr_)) output_pb();
        } else {
            t1_active_ = false;
            t1_pb7_ = 1;
            if (t1_set_pb7(acr_)) output_pb();
        }
    } else {
        t1_--;
    }
}

void Via6522::tick_t2() {
    if (!t2_active_) return;
    if (t2_count_pb6(acr_)) {
        // Count PB6 falling edges — handled in write_pb / set_pb_line path
        return;
    }
    if (t2_ == 0) {
        set_int(kIntT2);
        t2_active_ = false;
    } else {
        t2_--;
    }
}

void Via6522::shift_clock() {
    if (shift_count_ == 0) return;
    const uint8_t mode = sr_mode(acr_);
    const bool shift_out = (mode >= 4);  // 4..7 output

    if (shift_out) {
        // Shift out MSB first onto CB2
        shift_out_bit_ = (sr_ & 0x80) != 0;
        sr_ = uint8_t((sr_ << 1) | (shift_out_bit_ ? 1 : 0));  // rotate
        // Free-running T2 out (mode 4) does not rotate in data; reload pattern
        if (mode == 4) {
            // keep rotating the same byte
        } else if (mode == 5 || mode == 6 || mode == 7) {
            // shift out, CB1 generated internally for mode 5/7 under PHI2/T2
            sr_ = uint8_t(sr_ << 1);  // logical shift
        }
        output_cb2(shift_out_bit_);
    } else {
        // Shift in from CB2
        sr_ = uint8_t((sr_ << 1) | (in_cb2_ ? 1 : 0));
    }

    if (shift_count_ != 0xFF) {  // 0xFF = free-run
        if (--shift_count_ == 0) set_int(kIntSR);
    }
}

void Via6522::tick(int cycles) {
    for (int i = 0; i < cycles; i++) {
        tick_t1();
        tick_t2();

        // Shift register PHI2 / T2 timed modes
        const uint8_t mode = sr_mode(acr_);
        if (shift_count_ != 0) {
            if (mode == 2 || mode == 6) {
                // PHI2 control: clock every 2 PHI2
                if (++shift_phase_ >= 2) {
                    shift_phase_ = 0;
                    shift_clock();
                }
            } else if (mode == 1 || mode == 4 || mode == 5) {
                // T2 as rate generator: clock when T2 underflows mid-count
                // Approximate: every (t2ll+2) cycles
                const int period = int(t2ll_) + 2;
                if (++shift_phase_ >= period) {
                    shift_phase_ = 0;
                    shift_clock();
                }
            }
        }
    }
}

uint8_t Via6522::read(uint8_t reg) {
    switch (reg & 0x0F) {
        case 0x00: {  // ORB / IRB
            const uint8_t v = input_pb();
            clear_int(kIntCB1);
            // Independent CB2 clears only if not handshake/fixed
            if (!cb2_handshake(pcr_) && !cb2_pulse(pcr_) && !cb2_fix_out(pcr_))
                clear_int(kIntCB2);
            return v;
        }
        case 0x01: {  // ORA / IRA with handshake
            const uint8_t v = input_pa();
            clear_int(kIntCA1);
            if (!ca2_handshake(pcr_) && !ca2_pulse(pcr_) && !ca2_fix_out(pcr_))
                clear_int(kIntCA2);
            if (ca2_handshake(pcr_) || ca2_pulse(pcr_)) {
                output_ca2(false);
                if (ca2_pulse(pcr_)) {
                    // pulse low for ~1 cycle — restore next tick edge
                    output_ca2(true);
                }
            }
            return v;
        }
        case 0x02:
            return ddr_b_;
        case 0x03:
            return ddr_a_;
        case 0x04:  // T1CL
            clear_int(kIntT1);
            return uint8_t(t1_ & 0xFF);
        case 0x05:
            return uint8_t(t1_ >> 8);
        case 0x06:
            return t1ll_;
        case 0x07:
            return t1lh_;
        case 0x08:  // T2CL
            clear_int(kIntT2);
            return uint8_t(t2_ & 0xFF);
        case 0x09:
            return uint8_t(t2_ >> 8);
        case 0x0A:  // SR
            clear_int(kIntSR);
            // Start shift-in if mode requires
            if (sr_mode(acr_) >= 1 && sr_mode(acr_) <= 3 && shift_count_ == 0)
                shift_count_ = 8;
            return sr_;
        case 0x0B:
            return acr_;
        case 0x0C:
            return pcr_;
        case 0x0D:
            return ifr_;
        case 0x0E:
            return uint8_t(ier_ | 0x80);
        case 0x0F: {  // ORA no handshake
            return input_pa();
        }
        default:
            return 0xFF;
    }
}

void Via6522::write(uint8_t reg, uint8_t value) {
    switch (reg & 0x0F) {
        case 0x00:  // ORB
            out_b_ = value;
            clear_int(kIntCB1);
            if (!cb2_handshake(pcr_) && !cb2_pulse(pcr_) && !cb2_fix_out(pcr_))
                clear_int(kIntCB2);
            if (cb2_handshake(pcr_) || cb2_pulse(pcr_)) {
                output_cb2(false);
                if (cb2_pulse(pcr_)) output_cb2(true);
            }
            output_pb();
            // PB6 edge counting for T2
            if (t2_count_pb6(acr_) && t2_active_) {
                const bool pb6 = (out_b() & 0x40) != 0;
                if (t2_pb6_prev_ && !pb6) {
                    if (t2_ == 0) {
                        set_int(kIntT2);
                        t2_active_ = false;
                    } else {
                        t2_--;
                    }
                }
                t2_pb6_prev_ = pb6;
            }
            break;
        case 0x01:  // ORA handshake
            out_a_ = value;
            clear_int(kIntCA1);
            if (!ca2_handshake(pcr_) && !ca2_pulse(pcr_) && !ca2_fix_out(pcr_))
                clear_int(kIntCA2);
            if (ca2_handshake(pcr_) || ca2_pulse(pcr_)) {
                output_ca2(false);
                if (ca2_pulse(pcr_)) output_ca2(true);
            }
            output_pa();
            break;
        case 0x02:
            ddr_b_ = value;
            output_pb();
            break;
        case 0x03:
            ddr_a_ = value;
            output_pa();
            break;
        case 0x04:
            t1ll_ = value;
            break;
        case 0x05:
            t1lh_ = value;
            t1_ = uint16_t(t1ll_ | (uint16_t(t1lh_) << 8));
            clear_int(kIntT1);
            t1_active_ = true;
            t1_pb7_ = 0;
            if (t1_set_pb7(acr_)) output_pb();
            break;
        case 0x06:
            t1ll_ = value;
            break;
        case 0x07:
            t1lh_ = value;
            clear_int(kIntT1);
            break;
        case 0x08:
            t2ll_ = value;
            break;
        case 0x09:
            t2lh_ = value;
            t2_ = uint16_t(t2ll_ | (uint16_t(t2lh_) << 8));
            clear_int(kIntT2);
            t2_active_ = true;
            t2_pb6_prev_ = (out_b() & 0x40) != 0;
            break;
        case 0x0A:  // SR write
            sr_ = value;
            clear_int(kIntSR);
            {
                const uint8_t mode = sr_mode(acr_);
                if (mode == 4)
                    shift_count_ = 0xFF;  // free-run
                else if (mode >= 5)
                    shift_count_ = 8;  // shift out 8 bits
                else if (mode >= 1)
                    shift_count_ = 8;  // shift in
                else
                    shift_count_ = 0;
                shift_phase_ = 0;
            }
            break;
        case 0x0B: {  // ACR
            const uint16_t t1_now = t1_;
            acr_ = value;
            output_pb();
            if (t1_continuous(acr_)) {
                t1_ = t1_now;
                t1_active_ = true;
            }
            // Shift modes that force CB2 high as idle
            if (sr_mode(acr_) == 0 || sr_mode(acr_) == 7 || sr_mode(acr_) == 3)
                output_cb2(true);
            break;
        }
        case 0x0C:  // PCR
            pcr_ = value;
            if (ca2_fix_out(value)) output_ca2(ca2_level(value) != 0);
            if (cb2_fix_out(value)) output_cb2(cb2_level(value) != 0);
            break;
        case 0x0D:  // IFR — write 1 clears
            clear_int(value & 0x7F);
            break;
        case 0x0E:  // IER
            if (value & 0x80)
                ier_ = uint8_t(ier_ | (value & 0x7F));
            else
                ier_ = uint8_t(ier_ & ~(value & 0x7F));
            output_irq();
            break;
        case 0x0F:  // ORA no handshake
            out_a_ = value;
            output_pa();
            break;
        default:
            break;
    }
}

}  // namespace dsp
