#include "machine/pia6821.h"
namespace dsp {
void Pia6821::reset() {
    ctl_a_ = ctl_b_ = ddr_a_ = ddr_b_ = out_a_ = out_b_ = in_a_ = in_b_ = 0;
    in_ca1_ = true; in_cb1_ = false; out_ca2_ = out_cb2_ = false;
    in_a_ = 0xff; in_b_ = 0;
    irq_a1_ = irq_a2_ = irq_b1_ = irq_b2_ = irq_a_state_ = irq_b_state_ = false;
}
void Pia6821::set_in_out(InHandler a, InHandler b, OutHandler oa, OutHandler ob) {
    in_a_handler_ = std::move(a); in_b_handler_ = std::move(b);
    out_a_handler_ = std::move(oa); out_b_handler_ = std::move(ob);
}
void Pia6821::set_irq(IrqHandler a, IrqHandler b) { irqa_handler_ = std::move(a); irqb_handler_ = std::move(b); }
void Pia6821::set_cb2(CbHandler cb2) { cb2_handler_ = std::move(cb2); }
void Pia6821::update_interrupts() {
    const bool na = (irq_a1_ && (ctl_a_ & 1)) || (irq_a2_ && (ctl_a_ & 8) && c2_input(ctl_a_));
    const bool nb = (irq_b1_ && (ctl_b_ & 1)) || (irq_b2_ && (ctl_b_ & 8) && c2_input(ctl_b_));
    if (na != irq_a_state_) { irq_a_state_ = na; if (irqa_handler_) irqa_handler_(na); }
    if (nb != irq_b_state_) { irq_b_state_ = nb; if (irqb_handler_) irqb_handler_(nb); }
}
void Pia6821::set_out_ca2(bool d) { out_ca2_ = d; }
void Pia6821::set_out_cb2(bool d) {
    if (d == out_cb2_) return;
    out_cb2_ = d;
    if (cb2_handler_) cb2_handler_(d);
}
uint8_t Pia6821::get_in_a_value() const {
    uint8_t p = in_a_handler_ ? in_a_handler_() : in_a_;
    return uint8_t((out_a_ & ddr_a_) | (p & ~ddr_a_));
}
uint8_t Pia6821::get_in_b_value() const {
    uint8_t p = in_b_handler_ ? in_b_handler_() : in_b_;
    return uint8_t((out_b_ & ddr_b_) | (p & ~ddr_b_));
}
void Pia6821::send_to_out_a() { if (out_a_handler_) out_a_handler_(uint8_t(out_a_ & ddr_a_)); }
void Pia6821::send_to_out_b() { if (out_b_handler_) out_b_handler_(out_b_); }
uint8_t Pia6821::read(uint8_t offset) {
    switch (offset & 3) {
        case 0:
            if (!output_selected(ctl_a_)) return ddr_a_;
            { auto v = get_in_a_value(); irq_a1_ = irq_a2_ = false; update_interrupts();
              if (c2_strobe_mode(ctl_a_) && strobe_c1_reset(ctl_a_)) set_out_ca2(true); return v; }
        case 1: return uint8_t((ctl_a_ & ~0xc0) | (irq_a1_ ? 0x80 : 0) | (irq_a2_ ? 0x40 : 0));
        case 2:
            if (!output_selected(ctl_b_)) return ddr_b_;
            { auto v = get_in_b_value();
              if (irq_b1_ && c2_strobe_mode(ctl_b_) && strobe_c1_reset(ctl_b_)) set_out_cb2(true);
              irq_b1_ = irq_b2_ = false; update_interrupts(); return v; }
        case 3: return uint8_t((ctl_b_ & ~0xc0) | (irq_b1_ ? 0x80 : 0) | (irq_b2_ ? 0x40 : 0));
    }
    return 0;
}
void Pia6821::write(uint8_t offset, uint8_t value) {
    switch (offset & 3) {
        case 0:
            if (!output_selected(ctl_a_)) { ddr_a_ = value; send_to_out_a(); }
            else { out_a_ = value; send_to_out_a(); if (c2_strobe_mode(ctl_a_)) set_out_ca2(false); }
            break;
        case 1: {
            bool was = c2_output(ctl_a_);
            ctl_a_ = uint8_t(value & 0x3f); update_interrupts();
            if (c2_output(ctl_a_)) {
                if ((ctl_a_ & 0x38) == 0x30) set_out_ca2(false);
                else if ((ctl_a_ & 0x38) == 0x38) set_out_ca2(true);
            } else if (was) set_out_ca2(false);
            break;
        }
        case 2:
            if (!output_selected(ctl_b_)) { ddr_b_ = value; send_to_out_b(); }
            else { out_b_ = value; send_to_out_b(); if (c2_strobe_mode(ctl_b_)) set_out_cb2(false); }
            break;
        case 3: {
            bool was = c2_output(ctl_b_);
            ctl_b_ = uint8_t(value & 0x3f); update_interrupts();
            if (c2_output(ctl_b_)) {
                if ((ctl_b_ & 0x38) == 0x30) set_out_cb2(false);
                else if ((ctl_b_ & 0x38) == 0x38) set_out_cb2(true);
            } else if (was) set_out_cb2(false);
            break;
        }
    }
}
void Pia6821::ca1_w(bool state) {
    if (in_ca1_ != state) {
        if ((state && c1_low_to_high(ctl_a_)) || (!state && c1_high_to_low(ctl_a_))) {
            irq_a1_ = true; update_interrupts();
            if (c2_strobe_mode(ctl_a_) && !strobe_c1_reset(ctl_a_)) set_out_ca2(true);
        }
        in_ca1_ = state;
    }
}
void Pia6821::cb1_w(bool state) {
    if (in_cb1_ != state) {
        if ((state && c1_low_to_high(ctl_b_)) || (!state && c1_high_to_low(ctl_b_))) {
            irq_b1_ = true; update_interrupts();
        }
        in_cb1_ = state;
    }
}
void Pia6821::portb_w(uint8_t value) { in_b_ = value; }
}  // namespace dsp
