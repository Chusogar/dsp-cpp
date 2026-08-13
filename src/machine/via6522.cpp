#include "machine/via6522.h"

namespace dsp {

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
    in_a_ = in_b_ = 0xff;
    ddr_a_ = ddr_b_ = 0;
    t1ll_ = 0xf3;
    t1lh_ = 0xb5;
    t2ll_ = t2lh_ = 0xff;
    t1_ = uint16_t(t1ll_ | (uint16_t(t1lh_) << 8));
    t2_ = 0xffff;
    t1_active_ = t2_active_ = false;
    t1_pb7_ = 1;
    sr_ = acr_ = pcr_ = ier_ = ifr_ = 0;
    in_cb1_ = false;
    if (irq_cb_) irq_cb_(IrqLine::Clear);
}

void Via6522::set_int(uint8_t bit) {
    ifr_ = uint8_t(ifr_ | bit);
    output_irq();
}

void Via6522::clear_int(uint8_t bit) {
    ifr_ = uint8_t(ifr_ & ~bit);
    output_irq();
}

void Via6522::output_irq() {
    if (ifr_ & ier_ & 0x7f) {
        ifr_ = uint8_t(ifr_ | kIntAny);
        if (irq_cb_) irq_cb_(IrqLine::Hold);
    } else {
        ifr_ = uint8_t(ifr_ & ~kIntAny);
        if (irq_cb_) irq_cb_(IrqLine::Clear);
    }
}

void Via6522::output_pa() {
    const uint8_t v = uint8_t((out_a_ & ddr_a_) | (~ddr_a_));
    if (pa_out_) pa_out_(v);
}

void Via6522::output_pb() {
    uint8_t v = uint8_t((out_b_ & ddr_b_) | (~ddr_b_));
    if (acr_ & 0x80) {  // T1 controls PB7
        if (t1_pb7_) v = uint8_t(v | 0x80);
        else v = uint8_t(v & 0x7f);
    }
    if (pb_out_) pb_out_(v);
}

void Via6522::set_pb_line(int line, bool state) {
    if (state) in_b_ = uint8_t(in_b_ | (1 << line));
    else in_b_ = uint8_t(in_b_ & ~(1 << line));
}

void Via6522::write_cb1(bool state) {
    if (state != in_cb1_) {
        in_cb1_ = state;
        // edge can set CB1 interrupt depending on PCR — simplified
        if ((pcr_ & 0x10) == 0 && !state) set_int(0x10);
        if ((pcr_ & 0x10) != 0 && state) set_int(0x10);
    }
}

void Via6522::tick(int cycles) {
    if (cycles <= 0) return;
    if (t1_active_) {
        for (int i = 0; i < cycles; ++i) {
            if (t1_ == 0) {
                set_int(kIntT1);
                t1_ = uint16_t(t1ll_ | (uint16_t(t1lh_) << 8));
                if (acr_ & 0x40) {  // continuous
                    t1_pb7_ ^= 1;
                    output_pb();
                } else {
                    t1_active_ = false;
                    t1_pb7_ = 1;
                    output_pb();
                }
            } else {
                --t1_;
            }
        }
    }
    if (t2_active_ && !(acr_ & 0x20)) {  // timed mode (not PB6 pulse count)
        for (int i = 0; i < cycles; ++i) {
            if (t2_ == 0) {
                set_int(kIntT2);
                t2_active_ = false;
            } else {
                --t2_;
            }
        }
    }
}

uint8_t Via6522::read(uint8_t reg) {
    switch (reg & 0x0f) {
        case 0x00: {  // ORB / IRB
            uint8_t in = pb_in_ ? pb_in_() : in_b_;
            clear_int(0x18);  // CB1/CB2
            return uint8_t((out_b_ & ddr_b_) | (in & ~ddr_b_));
        }
        case 0x01: {  // ORA / IRA
            uint8_t in = pa_in_ ? pa_in_() : in_a_;
            clear_int(0x03);  // CA1/CA2
            return uint8_t((out_a_ & ddr_a_) | (in & ~ddr_a_));
        }
        case 0x02: return ddr_b_;
        case 0x03: return ddr_a_;
        case 0x04:  // T1C-L
            clear_int(kIntT1);
            return uint8_t(t1_);
        case 0x05: return uint8_t(t1_ >> 8);
        case 0x06: return t1ll_;
        case 0x07: return t1lh_;
        case 0x08:
            clear_int(kIntT2);
            return uint8_t(t2_);
        case 0x09: return uint8_t(t2_ >> 8);
        case 0x0a: return sr_;
        case 0x0b: return acr_;
        case 0x0c: return pcr_;
        case 0x0d: return ifr_;
        case 0x0e: return uint8_t(ier_ | 0x80);
        case 0x0f: {  // ORA no handshake
            uint8_t in = pa_in_ ? pa_in_() : in_a_;
            return uint8_t((out_a_ & ddr_a_) | (in & ~ddr_a_));
        }
        default: return 0xff;
    }
}

void Via6522::write(uint8_t reg, uint8_t value) {
    switch (reg & 0x0f) {
        case 0x00:
            out_b_ = value;
            clear_int(0x18);
            output_pb();
            break;
        case 0x01:
            out_a_ = value;
            clear_int(0x03);
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
            t1ll_ = t1ll_;  // keep
            t1_ = uint16_t(t1ll_ | (uint16_t(t1lh_) << 8));
            clear_int(kIntT1);
            t1_active_ = true;
            t1_pb7_ = 0;
            output_pb();
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
            break;
        case 0x0a:
            sr_ = value;
            break;
        case 0x0b:
            acr_ = value;
            break;
        case 0x0c:
            pcr_ = value;
            break;
        case 0x0d:
            // clear bits written as 1 (except bit7)
            if (value & 0x80) ifr_ = uint8_t(ifr_ | (value & 0x7f));
            else clear_int(value & 0x7f);
            break;
        case 0x0e:
            if (value & 0x80) ier_ = uint8_t(ier_ | (value & 0x7f));
            else ier_ = uint8_t(ier_ & ~(value & 0x7f));
            output_irq();
            break;
        case 0x0f:
            out_a_ = value;
            output_pa();
            break;
        default:
            break;
    }
}

}  // namespace dsp
