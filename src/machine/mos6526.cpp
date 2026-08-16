#include "machine/mos6526.h"

namespace dsp {
namespace {

uint8_t bcd_increment(uint8_t value) {
    value = uint8_t(value + 1);
    if ((value & 0x0f) >= 0x0a) value = uint8_t(value + 0x10 - 0x0a);
    return value;
}

}  // namespace

Mos6526::Mos6526(uint32_t clock) { (void)clock; }

void Mos6526::set_calls(PortRead pa_read, PortRead pb_read, PortWrite pa_write, PortWrite pb_write,
                        IrqCallback irq) {
    pa_read_ = std::move(pa_read);
    pb_read_ = std::move(pb_read);
    pa_write_ = std::move(pa_write);
    pb_write_ = std::move(pb_write);
    irq_call_ = std::move(irq);
}

void Mos6526::reset() {
    tod_stopped_ = true;
    cra_ = 0;
    crb_ = 0;
    alarm_ = 0;
    tod_ = 0x01000000;
    ta_pb6_ = 0;
    tb_pb7_ = 0;
    ta_out_ = 0;
    tb_out_ = 0;
    pra_ = 0;
    prb_ = 0;
    ddra_ = 0xff;
    ddrb_ = 0;
    pa_ = 0xff;
    pb_ = 0xff;
    pa_in_ = 0;
    pb_in_ = 0;
    imr_ = 0;
    icr_ = 0;
    ir0_ = 0;
    ir1_ = 0;
    irq_ = false;
    bits_ = 0;
    ta_latch_ = 0xffff;
    tb_latch_ = 0xffff;
    load_a0_ = load_a1_ = load_a2_ = 0;
    load_b0_ = load_b1_ = load_b2_ = 0;
    ta_ = 0;
    tb_ = 0;
    pc_ = 1;
    count_a0_ = count_a1_ = count_a2_ = count_a3_ = 0;
    oneshot_a0_ = 0;
    count_b0_ = count_b1_ = count_b2_ = count_b3_ = 0;
    oneshot_b0_ = 0;
    icr_read_ = false;
    cnt_ = 1;
    flag_ = 1;
    tod_count_ = 0;
    joystick1 = 0xff;
    joystick2 = 0xff;
}

void Mos6526::sync(int cycles) {
    for (int i = 0; i < cycles; i++) {
        if (pc_ == 0) pc_ = 1;
        clock_ta();
        clock_tb();
        update_pb();
        update_interrupt();
        clock_pipeline();
    }
}

void Mos6526::clock_ta() {
    if (count_a3_ != 0) ta_ = uint16_t(ta_ - 1);
    ta_out_ = int((count_a2_ != 0) && (ta_ == 0));
    if (ta_out_ != 0) {
        ta_pb6_ = uint8_t(ta_pb6_ ^ 1);
        if (((cra_ & 8) != 0) || (oneshot_a0_ != 0)) {
            cra_ = uint8_t(cra_ & 0xfe);
            count_a0_ = 0;
            count_a1_ = 0;
            count_a2_ = 0;
        }
        load_a1_ = 1;
    }
    if (load_a1_ != 0) {
        count_a2_ = 0;
        ta_ = ta_latch_;
    }
}

void Mos6526::clock_tb() {
    if (count_b3_ != 0) tb_ = uint16_t(tb_ - 1);
    tb_out_ = int((count_b2_ != 0) && (tb_ == 0));
    if (tb_out_ != 0) {
        tb_pb7_ = uint8_t(tb_pb7_ ^ 1);
        if (((crb_ & 8) != 0) || (oneshot_b0_ != 0)) {
            crb_ = uint8_t(crb_ & 0xfe);
            count_b0_ = 0;
            count_b1_ = 0;
            count_b2_ = 0;
        }
        load_b1_ = 1;
    }
    if (load_b1_ != 0) {
        count_b2_ = 0;
        tb_ = tb_latch_;
    }
}

void Mos6526::update_interrupt() {
    if (!irq_ && (ir1_ != 0)) {
        if (irq_call_) irq_call_(IrqLine::Assert);
        irq_ = true;
    }
    if (ta_out_ != 0) icr_ = uint8_t(icr_ | kIcrTa);
    if ((tb_out_ != 0) && !icr_read_) icr_ = uint8_t(icr_ | kIcrTb);
    icr_read_ = false;
}

void Mos6526::clock_pipeline() {
    count_a3_ = count_a2_;
    if ((cra_ & 0x20) == 0) count_a2_ = 1;  // CRA_INMODE_PHI2
    count_a2_ = count_a2_ & (cra_ & 1);
    count_a1_ = count_a0_;
    count_a0_ = 0;
    load_a2_ = load_a1_;
    load_a1_ = load_a0_;
    load_a0_ = (cra_ & 0x10) >> 4;
    cra_ = uint8_t(cra_ & ~0x10);
    oneshot_a0_ = (cra_ & 8) >> 3;

    count_b3_ = count_b2_;
    switch ((crb_ & 0x60) >> 5) {
        case 0:  // PHI2
            count_b2_ = 1;
            break;
        case 2:  // TA
            count_b2_ = ta_out_;
            break;
        case 3:  // CNT+TA
            count_b2_ = int((ta_out_ != 0) && (cnt_ != 0));
            break;
        default:
            break;
    }
    count_b2_ = count_b2_ & (crb_ & 1);
    count_b1_ = count_b0_;
    count_b0_ = 0;
    load_b2_ = load_b1_;
    load_b1_ = load_b0_;
    load_b0_ = (crb_ & 0x10) >> 4;
    crb_ = uint8_t(crb_ & ~0x10);
    oneshot_b0_ = (crb_ & 8) >> 3;

    if (ir0_ != 0) ir1_ = 1;
    ir0_ = ((icr_ & imr_) != 0) ? 1 : 0;
}

void Mos6526::update_pa() {
    const uint8_t pa = uint8_t(pra_ | (pa_in_ & uint8_t(~ddra_)));
    if (pa_ != pa) {
        pa_ = pa;
        if (pa_write_) pa_write_(pa);
    }
}

void Mos6526::update_pb() {
    uint8_t pb = uint8_t(prb_ | (pb_in_ & uint8_t(~ddrb_)));
    if (cra_ & 2) {
        const uint8_t pb6 = (cra_ & 4) ? ta_pb6_ : uint8_t(ta_out_);
        pb = uint8_t((pb & ~0x40) | ((pb6 & 1) << 6));
    }
    if (crb_ & 2) {
        const uint8_t pb7 = (crb_ & 4) ? tb_pb7_ : uint8_t(tb_out_);
        pb = uint8_t((pb & ~0x80) | ((pb7 & 1) << 7));
    }
    if (pb_ != pb) {
        if (pb_write_) pb_write_(pb);
        pb_ = pb;
    }
}

void Mos6526::write_tod(int offset, uint8_t data) {
    const int shift = 8 * offset;
    const uint32_t mask = ~(0xffu << shift);
    if (crb_ & 0x80) {
        alarm_ = (alarm_ & mask) | (uint32_t(data) << shift);
    } else {
        tod_ = (tod_ & mask) | (uint32_t(data) << shift);
    }
}

void Mos6526::set_cra(uint8_t data) {
    if (((cra_ & 1) == 0) && (data & 1)) ta_pb6_ = 1;
    if (((cra_ & 0x40) == 0) && (data & 0x40)) bits_ = 0;
    if ((cra_ & 0x40) && ((data & 0x40) == 0)) bits_ = 0;
    cra_ = data;
    update_pb();
}

void Mos6526::flag_w(uint8_t value) {
    if (flag_ != value) {
        icr_ = uint8_t(icr_ | kIcrFlag);
        flag_ = value;
    }
}

void Mos6526::clock_tod() {
    uint8_t subsecond = uint8_t(tod_ >> 0);
    uint8_t second = uint8_t(tod_ >> 8);
    uint8_t minute = uint8_t(tod_ >> 16);
    uint8_t hour = uint8_t(tod_ >> 24);
    tod_count_ += 1;
    const int limit = (cra_ & 0x80) ? 5 : 6;
    if (tod_count_ == limit) {
        tod_count_ = 0;
        subsecond = bcd_increment(subsecond);
        if (subsecond >= 0x10) {
            subsecond = 0;
            second = bcd_increment(second);
            if (second >= 0x60) {
                second = 0;
                minute = bcd_increment(minute);
                if (minute >= 0x60) {
                    minute = 0;
                    uint8_t pm = uint8_t(hour & 0x80);
                    hour = uint8_t(hour & 0x1f);
                    if (hour == 11) pm = uint8_t(pm ^ 0x80);
                    if (hour == 12) hour = 0;
                    hour = bcd_increment(hour);
                    hour = uint8_t(hour | pm);
                }
            }
        }
    }
    // Pascal assembled this as `hour or 24` (a TOD hour-shift bug). Use the
    // intended `hour shl 24` so the clock actually advances past the hour byte.
    tod_ = (uint32_t(subsecond) << 0) | (uint32_t(second) << 8) | (uint32_t(minute) << 16) |
           (uint32_t(hour) << 24);
}

void Mos6526::set_crb(uint8_t data) {
    if (((crb_ & 1) == 0) && (data & 1)) tb_pb7_ = 1;
    crb_ = data;
    update_pb();
}

uint8_t Mos6526::read(uint8_t address) {
    uint8_t res = 0;
    switch (address & 0x0f) {
        case 0x00: {
            const uint8_t tempb = pa_read_ ? pa_read_() : uint8_t(0xff);
            if (ddra_ != 0xff) {
                res = uint8_t((tempb & uint8_t(~ddra_)) | (pra_ & ddra_));
            } else {
                res = uint8_t(tempb & pra_);
            }
            pa_in_ = res;
            break;
        }
        case 0x01: {
            const uint8_t tempb = pb_read_ ? pb_read_() : uint8_t(0xff);
            if (ddrb_ != 0xff) {
                res = uint8_t((tempb & uint8_t(~ddrb_)) | (prb_ & ddrb_));
            } else {
                res = uint8_t(tempb & prb_);
            }
            pb_in_ = res;
            if (cra_ & 2) {
                const uint8_t pb6 = (cra_ & 4) ? ta_pb6_ : uint8_t(ta_out_);
                res = uint8_t((res & ~0x40) | ((pb6 & 1) << 6));
            }
            if (crb_ & 2) {
                const uint8_t pb7 = (crb_ & 4) ? tb_pb7_ : uint8_t(tb_out_);
                res = uint8_t((res & ~0x80) | ((pb7 & 1) << 7));
            }
            pc_ = 0;
            break;
        }
        case 0x02:
            res = ddra_;
            break;
        case 0x03:
            res = ddrb_;
            break;
        case 0x04:
            res = uint8_t(ta_ & 0xff);
            break;
        case 0x05:
            res = uint8_t(ta_ >> 8);
            break;
        case 0x06:
            res = uint8_t(tb_ & 0xff);
            break;
        case 0x07:
            res = uint8_t(tb_ >> 8);
            break;
        case 0x0d:
            res = uint8_t((ir1_ << 7) | icr_);
            icr_read_ = true;
            ir0_ = 0;
            ir1_ = 0;
            icr_ = 0;
            irq_ = false;
            if (irq_call_) irq_call_(IrqLine::Clear);
            break;
        case 0x0e:
            res = cra_;
            break;
        case 0x0f:
            res = crb_;
            break;
        default:
            break;
    }
    return res;
}

void Mos6526::write(uint8_t address, uint8_t value) {
    switch (address & 0x0f) {
        case 0x00:
            pra_ = value;
            update_pa();
            break;
        case 0x01:
            prb_ = value;
            update_pb();
            pc_ = 0;
            break;
        case 0x02:
            ddra_ = value;
            update_pa();
            break;
        case 0x03:
            ddrb_ = value;
            update_pb();
            break;
        case 0x04:
            ta_latch_ = uint16_t((ta_latch_ & 0xff00) | value);
            if (load_a2_ != 0) ta_ = uint16_t((ta_ & 0xff00) | value);
            break;
        case 0x05:
            ta_latch_ = uint16_t((uint16_t(value) << 8) | (ta_latch_ & 0xff));
            if ((cra_ & 1) == 0) load_a0_ = 1;
            if (cra_ & 8) {
                ta_ = ta_latch_;
                set_cra(uint8_t(cra_ | 1));
            }
            if (load_a2_ != 0) ta_ = uint16_t((uint16_t(value) << 8) | (ta_ & 0xff));
            break;
        case 0x06:
            tb_latch_ = uint16_t((tb_latch_ & 0xff00) | value);
            if (load_b2_ != 0) tb_ = uint16_t((tb_ & 0xff00) | value);
            break;
        case 0x07:
            tb_latch_ = uint16_t((uint16_t(value) << 8) | (tb_latch_ & 0xff));
            if ((crb_ & 1) == 0) load_b0_ = 1;
            if (crb_ & 8) {
                tb_ = tb_latch_;
                set_crb(uint8_t(crb_ | 1));
            }
            if (load_b2_ != 0) tb_ = uint16_t((uint16_t(value) << 8) | (tb_ & 0xff));
            break;
        case 0x08:
            write_tod(0, value);
            tod_stopped_ = false;
            break;
        case 0x09:
            write_tod(1, value);
            break;
        case 0x0a:
            write_tod(2, value);
            break;
        case 0x0b:
            write_tod(3, value);
            break;
        case 0x0d:
            if (value & 0x80) {
                imr_ = uint8_t(imr_ | (value & 0x1f));
            } else {
                imr_ = uint8_t(imr_ & ~(value & 0x1f));
            }
            if (!irq_ && ((icr_ & imr_) != 0)) ir0_ = 1;
            break;
        case 0x0e:
            set_cra(value);
            break;
        case 0x0f:
            set_crb(value);
            break;
        default:
            break;
    }
}

}  // namespace dsp
