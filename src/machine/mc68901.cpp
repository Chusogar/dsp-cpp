#include "machine/mc68901.h"

namespace dsp {
namespace {

const int kPrescale[8] = {0, 4, 10, 16, 50, 64, 100, 200};

}  // namespace

void Mc68901::reset() {
    gpip_ = 0xff;
    aer_ = ddr_ = 0;
    iera_ = ierb_ = 0;
    ipra_ = iprb_ = 0;
    isra_ = isrb_ = 0;
    imra_ = imrb_ = 0;
    vr_ = 0x40;
    tacr_ = tbcr_ = tcdcr_ = 0;
    ta_ = Timer{};
    tb_ = Timer{};
    tc_ = Timer{};
    td_ = Timer{};
    ta_.channel = 13;
    tb_.channel = 8;
    tc_.channel = 5;
    td_.channel = 4;
    irq_ = false;
}

void Mc68901::set_gpip_bit(int bit, int value) {
    const uint8_t mask = uint8_t(1u << bit);
    const uint8_t old = gpip_;
    if (value)
        gpip_ = uint8_t(gpip_ | mask);
    else
        gpip_ = uint8_t(gpip_ & ~mask);
    if (((old ^ gpip_) & mask) == 0) return;
    // GPIP bits 4/5/6/7 are not 1:1 with interrupt channel numbers.
    static const int kGpipChannel[8] = {0, 1, 2, 3, 6, 7, 14, 15};
    const bool rising = (gpip_ & mask) != 0;
    if (((aer_ & mask) != 0) == rising) raise(kGpipChannel[bit]);
}

void Mc68901::raise(int channel) {
    if (channel >= 8) {
        const uint8_t bit = uint8_t(1u << (channel - 8));
        if (iera_ & bit) ipra_ = uint8_t(ipra_ | bit);
    } else {
        const uint8_t bit = uint8_t(1u << channel);
        if (ierb_ & bit) iprb_ = uint8_t(iprb_ | bit);
    }
    update_irq();
}

void Mc68901::update_irq() {
    const bool want = ((ipra_ & iera_ & imra_) != 0) || ((iprb_ & ierb_ & imrb_) != 0);
    if (want == irq_) return;
    irq_ = want;
    if (irq_cb_) irq_cb_(irq_);
}

int Mc68901::irq_ack() {
    for (int ch = 15; ch >= 0; --ch) {
        const uint8_t bit = uint8_t(1u << (ch & 7));
        uint8_t& ipr = ch >= 8 ? ipra_ : iprb_;
        uint8_t& ier = ch >= 8 ? iera_ : ierb_;
        uint8_t& imr = ch >= 8 ? imra_ : imrb_;
        uint8_t& isr = ch >= 8 ? isra_ : isrb_;
        if ((ipr & ier & imr & bit) == 0) continue;
        ipr = uint8_t(ipr & ~bit);
        if (vr_ & 0x08) isr = uint8_t(isr | bit);  // software EOI
        update_irq();
        return int(vr_ & 0xf0) | ch;
    }
    return 0x18;  // spurious
}

void Mc68901::tick_timer(Timer& t, int ticks) {
    const int mode = t.control & 7;
    const int scale = kPrescale[mode];
    if (scale == 0 || t.data == 0) return;
    t.prescale_acc += ticks;
    while (t.prescale_acc >= scale) {
        t.prescale_acc -= scale;
        if (t.count == 0) t.count = t.data;
        t.count--;
        if (t.count == 0) {
            t.count = t.data;
            raise(t.channel);
        }
    }
}

void Mc68901::tick(int ticks) {
    if (ticks <= 0) return;
    tick_timer(ta_, ticks);
    tick_timer(tb_, ticks);
    tick_timer(tc_, ticks);
    tick_timer(td_, ticks);
}

uint8_t Mc68901::read(int offset) {
    switch (offset & 0x1f) {
        case 0x00: return gpip_;
        case 0x01: return aer_;
        case 0x02: return ddr_;
        case 0x03: return iera_;
        case 0x04: return ierb_;
        case 0x05: return ipra_;
        case 0x06: return iprb_;
        case 0x07: return isra_;
        case 0x08: return isrb_;
        case 0x09: return imra_;
        case 0x0a: return imrb_;
        case 0x0b: return vr_;
        case 0x0c: return tacr_;
        case 0x0d: return tbcr_;
        case 0x0e: return tcdcr_;
        case 0x0f: return ta_.count;
        case 0x10: return tb_.count;
        case 0x11: return tc_.count;
        case 0x12: return td_.count;
        default: return 0;
    }
}

void Mc68901::write(int offset, uint8_t value) {
    switch (offset & 0x1f) {
        case 0x00:
            gpip_ = uint8_t((gpip_ & ~ddr_) | (value & ddr_));
            break;
        case 0x01: aer_ = value; break;
        case 0x02: ddr_ = value; break;
        case 0x03: iera_ = value; ipra_ = uint8_t(ipra_ & iera_); update_irq(); break;
        case 0x04: ierb_ = value; iprb_ = uint8_t(iprb_ & ierb_); update_irq(); break;
        case 0x05: ipra_ = uint8_t(ipra_ & value); update_irq(); break;
        case 0x06: iprb_ = uint8_t(iprb_ & value); update_irq(); break;
        case 0x07: isra_ = uint8_t(isra_ & value); update_irq(); break;
        case 0x08: isrb_ = uint8_t(isrb_ & value); update_irq(); break;
        case 0x09: imra_ = value; update_irq(); break;
        case 0x0a: imrb_ = value; update_irq(); break;
        case 0x0b: vr_ = value; break;
        case 0x0c:
            tacr_ = value;
            ta_.control = uint8_t(value & 0x0f);
            if (value & 0x10) ta_.count = ta_.data;
            break;
        case 0x0d:
            tbcr_ = value;
            tb_.control = uint8_t(value & 0x0f);
            if (value & 0x10) tb_.count = tb_.data;
            break;
        case 0x0e:
            tcdcr_ = value;
            tc_.control = uint8_t((value >> 4) & 7);
            td_.control = uint8_t(value & 7);
            break;
        case 0x0f: ta_.data = value; if (ta_.count == 0) ta_.count = value; break;
        case 0x10: tb_.data = value; if (tb_.count == 0) tb_.count = value; break;
        case 0x11: tc_.data = value; if (tc_.count == 0) tc_.count = value; break;
        case 0x12: td_.data = value; if (td_.count == 0) td_.count = value; break;
        default: break;
    }
}

}  // namespace dsp
