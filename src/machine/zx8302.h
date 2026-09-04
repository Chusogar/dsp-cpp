#pragma once

#include <cstdint>
#include <functional>

namespace dsp {

// Sinclair ZX8302 "Tony" — RTC, IPC link, IRQs and microdrive/serial status.
class Zx8302 {
public:
    using LineCallback = std::function<void(int)>;

    enum : uint8_t {
        kIntGap = 0x01,
        kIntInterface = 0x02,
        kIntTransmit = 0x04,
        kIntFrame = 0x08,
        kIntExternal = 0x10,
        kStatusTxFull = 0x02,
        kStatusRxFull = 0x04,
        kStatusMdvGap = 0x08,
    };

    void reset();
    void set_ipl1l_callback(LineCallback cb) { ipl1l_cb_ = std::move(cb); }
    void set_comdata_callback(LineCallback cb) { comdata_cb_ = std::move(cb); }
    void set_baudx4_callback(LineCallback cb) { baudx4_cb_ = std::move(cb); }
    void set_mdseld_callback(LineCallback cb) { mdseld_cb_ = std::move(cb); }
    void set_mdselck_callback(LineCallback cb) { mdselck_cb_ = std::move(cb); }
    void set_mdrdw_callback(LineCallback cb) { mdrdw_cb_ = std::move(cb); }
    void set_erase_callback(LineCallback cb) { erase_cb_ = std::move(cb); }

    uint8_t rtc_r(uint32_t offset) const;
    void rtc_w(uint8_t value);
    void control_w(uint8_t value);
    uint8_t status_r() const;
    void ipc_command_w(uint8_t value);
    uint8_t irq_status_r() const { return irq_; }
    void irq_acknowledge_w(uint8_t value);
    void mdv_control_w(uint8_t value);
    void data_w(uint8_t value);
    uint8_t mdv_track_r(uint32_t offset);
    uint16_t mdv_tx_pop();

    void vsync_w(int state);
    void comctl_w(int state);
    void comdata_w(int state);
    void extint_w(int state);
    void mdv_raw1_w(int state);
    void mdv_raw2_w(int state);
    void mdv_gap_w(int state);
    void tick_rtc();
    void tick_baudx4();

    bool ipc_busy() const { return ipc_busy_; }
    uint8_t irq() const { return irq_; }
    uint8_t status() const { return status_; }

private:
    enum IpcPhase { kStart, kData, kStop };
    enum { kModeMdv = 0x10, kModeMask = 0x18 };
    enum MdvSync { kMdvIdle, kMdvSearch, kMdvDeliver };

    void trigger(uint8_t line);
    void transmit_ipc();

    LineCallback ipl1l_cb_;
    LineCallback comdata_cb_;
    LineCallback baudx4_cb_;
    LineCallback mdseld_cb_;
    LineCallback mdselck_cb_;
    LineCallback mdrdw_cb_;
    LineCallback erase_cb_;

    uint8_t idr_ = 1;
    uint8_t tcr_ = 0;
    uint8_t irq_ = 0;
    uint8_t irq_mask_ = 0;
    uint32_t ctr_ = 0;
    uint8_t status_ = kStatusMdvGap;
    uint8_t mdv_data_[2]{};
    uint8_t mdv_shift_[2]{};
    int mdv_bit_count_ = 0;
    int mdv_sync_ = kMdvIdle;
    uint8_t mdv_tx_[2]{};
    int mdv_tx_count_ = 0;

    int comdata_from_ipc_ = 1;
    int comdata_to_cpu_ = 1;
    int comdata_to_ipc_ = 1;
    int ipc_state_ = kStart;
    bool ipc_busy_ = false;
    bool baudx4_ = false;
    int dtr1_ = 0;
    int cts2_ = 0;
};

}  // namespace dsp
