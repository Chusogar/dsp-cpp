#include "machine/mos6526.h"

namespace dsp {

Mos6526::Mos6526(uint32_t clock) : clock_(clock) {}

void Mos6526::set_port_a(PortRead in, PortWrite out) {
    pa_in_ = std::move(in);
    pa_out_ = std::move(out);
}
void Mos6526::set_port_b(PortRead in, PortWrite out) {
    pb_in_ = std::move(in);
    pb_out_ = std::move(out);
}

void Mos6526::reset() {
    pra_ = prb_ = ddra_ = ddrb_ = 0;
    ina_ = inb_ = 0xFF;
    ta_ = tb_ = ta_latch_ = tb_latch_ = 0xFFFF;
    tod_t_ = tod_s_ = tod_m_ = tod_h_ = 0;
    sdr_ = icr_ = imr_ = cra_ = crb_ = 0;
    ta_under_ = tb_under_ = false;
    flag_ = false;
    joystick1 = joystick2 = 0xFF;
}

void Mos6526::update_irq() {
    if (icr_ & imr_ & 0x1F) {
        icr_ = uint8_t(icr_ | 0x80);
        if (irq_) irq_(IrqLine::Assert);
    } else {
        icr_ = uint8_t(icr_ & 0x7F);
        if (irq_) irq_(IrqLine::Clear);
    }
}

void Mos6526::set_flag(bool state) {
    if (flag_ && !state) {
        // falling edge
        icr_ = uint8_t(icr_ | 0x10);
        update_irq();
    }
    flag_ = state;
}

void Mos6526::clock_ta() {
    if (!(cra_ & 1)) return;
    if (ta_ == 0) {
        ta_ = ta_latch_;
        ta_under_ = true;
        icr_ = uint8_t(icr_ | 0x01);
        if (cra_ & 0x08) cra_ = uint8_t(cra_ & ~1);  // one-shot
        update_irq();
        // Timer A underflow can clock Timer B
        if ((crb_ & 0x41) == 0x41) clock_tb();
    } else {
        ta_--;
    }
}

void Mos6526::clock_tb() {
    if (!(crb_ & 1)) return;
    // Count PHI2 unless CRA bit forces count underflows of TA (handled above)
    if ((crb_ & 0x40) == 0) {
        if (tb_ == 0) {
            tb_ = tb_latch_;
            tb_under_ = true;
            icr_ = uint8_t(icr_ | 0x02);
            if (crb_ & 0x08) crb_ = uint8_t(crb_ & ~1);
            update_irq();
        } else {
            tb_--;
        }
    } else {
        // Count TA underflows — tb decremented from clock_ta path only when bit set
        if (tb_ == 0) {
            tb_ = tb_latch_;
            tb_under_ = true;
            icr_ = uint8_t(icr_ | 0x02);
            if (crb_ & 0x08) crb_ = uint8_t(crb_ & ~1);
            update_irq();
        } else {
            tb_--;
        }
    }
}

void Mos6526::tick(int cycles) {
    for (int i = 0; i < cycles; i++) {
        if (cra_ & 1) {
            if (ta_ == 0) {
                ta_ = ta_latch_;
                icr_ = uint8_t(icr_ | 0x01);
                if (cra_ & 0x08) cra_ = uint8_t(cra_ & ~1);
                if ((crb_ & 0x41) == 0x41) {
                    // TB counts TA underflows
                    if (tb_ == 0) {
                        tb_ = tb_latch_;
                        icr_ = uint8_t(icr_ | 0x02);
                        if (crb_ & 0x08) crb_ = uint8_t(crb_ & ~1);
                    } else {
                        tb_--;
                    }
                }
                update_irq();
            } else {
                ta_--;
            }
        }
        if ((crb_ & 1) && (crb_ & 0x40) == 0) {
            if (tb_ == 0) {
                tb_ = tb_latch_;
                icr_ = uint8_t(icr_ | 0x02);
                if (crb_ & 0x08) crb_ = uint8_t(crb_ & ~1);
                update_irq();
            } else {
                tb_--;
            }
        }
    }
}

uint8_t Mos6526::read(uint8_t reg) {
    switch (reg & 0x0F) {
        case 0: {
            uint8_t v = pa();
            if (pa_in_) v = uint8_t(v & pa_in_());
            // Joystick 2 on CIA1 PA
            v = uint8_t(v & joystick2);
            return v;
        }
        case 1: {
            uint8_t v = pb();
            if (pb_in_) v = uint8_t(v & pb_in_());
            v = uint8_t(v & joystick1);
            return v;
        }
        case 2:
            return ddra_;
        case 3:
            return ddrb_;
        case 4:
            return uint8_t(ta_ & 0xFF);
        case 5:
            return uint8_t(ta_ >> 8);
        case 6:
            return uint8_t(tb_ & 0xFF);
        case 7:
            return uint8_t(tb_ >> 8);
        case 8:
            return tod_t_;
        case 9:
            return tod_s_;
        case 0xA:
            return tod_m_;
        case 0xB:
            return tod_h_;
        case 0xC:
            return sdr_;
        case 0xD: {
            const uint8_t v = icr_;
            icr_ = 0;
            if (irq_) irq_(IrqLine::Clear);
            return v;
        }
        case 0xE:
            return cra_;
        case 0xF:
            return crb_;
    }
    return 0xFF;
}

void Mos6526::write(uint8_t reg, uint8_t value) {
    switch (reg & 0x0F) {
        case 0:
            pra_ = value;
            if (pa_out_) pa_out_(pa());
            break;
        case 1:
            prb_ = value;
            if (pb_out_) pb_out_(pb());
            break;
        case 2:
            ddra_ = value;
            if (pa_out_) pa_out_(pa());
            break;
        case 3:
            ddrb_ = value;
            if (pb_out_) pb_out_(pb());
            break;
        case 4:
            ta_latch_ = uint16_t((ta_latch_ & 0xFF00) | value);
            break;
        case 5:
            ta_latch_ = uint16_t((ta_latch_ & 0x00FF) | (value << 8));
            if (!(cra_ & 1)) ta_ = ta_latch_;
            break;
        case 6:
            tb_latch_ = uint16_t((tb_latch_ & 0xFF00) | value);
            break;
        case 7:
            tb_latch_ = uint16_t((tb_latch_ & 0x00FF) | (value << 8));
            if (!(crb_ & 1)) tb_ = tb_latch_;
            break;
        case 8:
            tod_t_ = value & 0x0F;
            break;
        case 9:
            tod_s_ = value;
            break;
        case 0xA:
            tod_m_ = value;
            break;
        case 0xB:
            tod_h_ = value;
            break;
        case 0xC:
            sdr_ = value;
            break;
        case 0xD:
            // bit7: 1=set mask bits, 0=clear mask bits
            if (value & 0x80)
                imr_ = uint8_t(imr_ | (value & 0x1F));
            else
                imr_ = uint8_t(imr_ & ~(value & 0x1F));
            update_irq();
            break;
        case 0xE:
            cra_ = value;
            if (value & 0x10) {
                ta_ = ta_latch_;
                cra_ = uint8_t(cra_ & ~0x10);
            }
            break;
        case 0xF:
            crb_ = value;
            if (value & 0x10) {
                tb_ = tb_latch_;
                crb_ = uint8_t(crb_ & ~0x10);
            }
            break;
    }
}

}  // namespace dsp
