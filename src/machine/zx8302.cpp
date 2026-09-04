#include "machine/zx8302.h"

#include <ctime>

namespace dsp {
namespace {

// QDOS seconds are unix time plus the 1961-01-01 → 1970-01-01 offset.
constexpr uint32_t kRtcUnixAdjust = 283996800u;

}  // namespace

void Zx8302::reset() {
    idr_ = 1;
    tcr_ = 0;
    irq_ = 0;
    irq_mask_ = 0;
    ctr_ = uint32_t(std::time(nullptr)) + kRtcUnixAdjust;
    status_ = kStatusMdvGap;
    mdv_data_[0] = 0;
    mdv_data_[1] = 0;
    comdata_from_ipc_ = 1;
    comdata_to_cpu_ = 1;
    comdata_to_ipc_ = 1;
    ipc_state_ = kStart;
    ipc_busy_ = false;
    baudx4_ = false;
    dtr1_ = 0;
    cts2_ = 0;
}

void Zx8302::trigger(uint8_t line) {
    irq_ = uint8_t(irq_ | line);
    if (ipl1l_cb_) ipl1l_cb_(1);
}

void Zx8302::transmit_ipc() {
    switch (ipc_state_) {
        case kStart:
            if (comdata_cb_) comdata_cb_(idr_ & 1);
            ipc_busy_ = true;
            ipc_state_ = kData;
            break;
        case kData:
            comdata_to_ipc_ = (idr_ >> 1) & 1;
            if (comdata_cb_) comdata_cb_(comdata_to_ipc_);
            ipc_state_ = kStop;
            break;
        case kStop:
            if (comdata_cb_) comdata_cb_((idr_ >> 2) & 1);
            ipc_busy_ = false;
            break;
    }
}

uint8_t Zx8302::rtc_r(uint32_t offset) const {
    const int shift = 24 - int(offset & 3) * 8;
    return uint8_t(ctr_ >> shift);
}

void Zx8302::rtc_w(uint8_t) {}

void Zx8302::control_w(uint8_t value) { tcr_ = value; }

uint8_t Zx8302::status_r() const {
    uint8_t data = status_;
    data = uint8_t(data | (uint8_t(dtr1_) << 4));
    data = uint8_t(data | (uint8_t(cts2_) << 5));
    if (ipc_busy_) data = uint8_t(data | 0x40);
    if (comdata_to_cpu_) data = uint8_t(data | 0x80);
    return data;
}

void Zx8302::ipc_command_w(uint8_t value) {
    idr_ = value;
    ipc_state_ = kStart;
    transmit_ipc();
}

void Zx8302::irq_acknowledge_w(uint8_t value) {
    irq_mask_ = uint8_t(value & 0xe0);
    irq_ = uint8_t(irq_ & ~uint8_t(value & 0x1f));
    if (irq_ == 0 && ipl1l_cb_) ipl1l_cb_(0);
    if ((irq_mask_ & 0x20) != 0 && (status_ & kStatusMdvGap) != 0) {
        trigger(kIntGap);
    }
}

void Zx8302::mdv_control_w(uint8_t) {
    status_ = uint8_t(status_ & ~kStatusRxFull);
}

void Zx8302::data_w(uint8_t) { status_ = uint8_t(status_ | kStatusTxFull); }

uint8_t Zx8302::mdv_track_r(uint32_t offset) {
    const int track = int(offset & 1);
    const uint8_t data = mdv_data_[track];
    if (track == 1) status_ = uint8_t(status_ & ~kStatusRxFull);
    return data;
}

void Zx8302::vsync_w(int state) {
    if (state) trigger(kIntFrame);
}

void Zx8302::comctl_w(int state) {
    if (state) {
        transmit_ipc();
        comdata_to_cpu_ = comdata_from_ipc_;
    }
}

void Zx8302::comdata_w(int state) { comdata_from_ipc_ = state; }

void Zx8302::extint_w(int state) {
    if (state) trigger(kIntExternal);
}

void Zx8302::tick_rtc() { ctr_++; }

void Zx8302::tick_baudx4() {
    baudx4_ = !baudx4_;
    if (baudx4_cb_) baudx4_cb_(baudx4_ ? 1 : 0);
}

}  // namespace dsp
