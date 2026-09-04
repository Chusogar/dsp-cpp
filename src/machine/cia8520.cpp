#include "machine/cia8520.h"

namespace dsp {

void Cia8520::set_port_a(PortRead in, PortWrite out) {
    pa_in_ = std::move(in);
    pa_out_ = std::move(out);
}

void Cia8520::set_port_b(PortRead in, PortWrite out) {
    pb_in_ = std::move(in);
    pb_out_ = std::move(out);
}

void Cia8520::reset() {
    pra_ = prb_ = ddra_ = ddrb_ = 0;
    ta_ = tb_ = ta_latch_ = tb_latch_ = 0xFFFF;
    tod_ = 0;
    sdr_ = icr_ = imr_ = cra_ = crb_ = 0;
    if (irq_) irq_(false);
}

uint8_t Cia8520::port_read(uint8_t latch, uint8_t ddr, const PortRead& in) const {
    const uint8_t ext = in ? in() : uint8_t(0xFF);
    return uint8_t((latch & ddr) | (ext & uint8_t(~ddr)));
}

void Cia8520::update_irq() {
    const bool asserted = (icr_ & imr_ & 0x1F) != 0;
    if (asserted)
        icr_ = uint8_t(icr_ | 0x80);
    else
        icr_ = uint8_t(icr_ & 0x7F);
    if (irq_) irq_(asserted);
}

void Cia8520::pulse_flag() {
    icr_ = uint8_t(icr_ | 0x10);
    update_irq();
}

void Cia8520::tod_tick() {
    tod_ = (tod_ + 1) & 0xFFFFFF;
}

void Cia8520::tick(int eclocks) {
    for (int i = 0; i < eclocks; i++) {
        if (cra_ & 1) {
            if (ta_ == 0) {
                ta_ = ta_latch_;
                icr_ = uint8_t(icr_ | 0x01);
                if (cra_ & 0x08) cra_ = uint8_t(cra_ & ~1);
                if ((crb_ & 0x41) == 0x41) {
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

uint8_t Cia8520::read(uint8_t reg) {
    switch (reg & 0x0F) {
        case 0:
            return port_read(pra_, ddra_, pa_in_);
        case 1:
            return port_read(prb_, ddrb_, pb_in_);
        case 2:
            return ddra_;
        case 3:
            return ddrb_;
        case 4:
            return uint8_t(ta_);
        case 5:
            return uint8_t(ta_ >> 8);
        case 6:
            return uint8_t(tb_);
        case 7:
            return uint8_t(tb_ >> 8);
        case 8:
            return uint8_t(tod_);
        case 9:
            return uint8_t(tod_ >> 8);
        case 0xA:
            return uint8_t(tod_ >> 16);
        case 0xB:
            return 0;
        case 0xC:
            return sdr_;
        case 0xD: {
            const uint8_t v = icr_;
            icr_ = 0;
            if (irq_) irq_(false);
            return v;
        }
        case 0xE:
            return cra_;
        case 0xF:
            return crb_;
    }
    return 0xFF;
}

void Cia8520::write(uint8_t reg, uint8_t value) {
    switch (reg & 0x0F) {
        case 0:
            pra_ = value;
            if (pa_out_) pa_out_(uint8_t(pra_ | uint8_t(~ddra_)));
            break;
        case 1:
            prb_ = value;
            if (pb_out_) pb_out_(uint8_t(prb_ | uint8_t(~ddrb_)));
            break;
        case 2:
            ddra_ = value;
            if (pa_out_) pa_out_(uint8_t(pra_ | uint8_t(~ddra_)));
            break;
        case 3:
            ddrb_ = value;
            if (pb_out_) pb_out_(uint8_t(prb_ | uint8_t(~ddrb_)));
            break;
        case 4:
            ta_latch_ = uint16_t((ta_latch_ & 0xFF00) | value);
            break;
        case 5:
            ta_latch_ = uint16_t((ta_latch_ & 0x00FF) | (uint16_t(value) << 8));
            if (!(cra_ & 1)) ta_ = ta_latch_;
            break;
        case 6:
            tb_latch_ = uint16_t((tb_latch_ & 0xFF00) | value);
            break;
        case 7:
            tb_latch_ = uint16_t((tb_latch_ & 0x00FF) | (uint16_t(value) << 8));
            if (!(crb_ & 1)) tb_ = tb_latch_;
            break;
        case 8:
            tod_ = (tod_ & 0xFFFF00) | value;
            break;
        case 9:
            tod_ = (tod_ & 0xFF00FF) | (uint32_t(value) << 8);
            break;
        case 0xA:
            tod_ = (tod_ & 0x00FFFF) | (uint32_t(value) << 16);
            break;
        case 0xC:
            sdr_ = value;
            icr_ = uint8_t(icr_ | 0x08);
            update_irq();
            break;
        case 0xD:
            if (value & 0x80)
                imr_ = uint8_t(imr_ | (value & 0x1F));
            else
                imr_ = uint8_t(imr_ & ~(value & 0x1F));
            update_irq();
            break;
        case 0xE:
            cra_ = uint8_t(value & ~0x10);
            if (value & 0x10) ta_ = ta_latch_;
            break;
        case 0xF:
            crb_ = uint8_t(value & ~0x10);
            if (value & 0x10) tb_ = tb_latch_;
            break;
        default:
            break;
    }
}

}  // namespace dsp
