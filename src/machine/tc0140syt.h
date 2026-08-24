#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace dsp {

// Taito TC0140SYT main↔sound mailbox, ported from taito_sound.pas.
// Four nibble ports in each direction plus a status register; the sound Z80
// is notified through an NMI when the master fills a port pair.
class Tc0140Syt {
public:
    using NmiHandler = std::function<void()>;
    using ResetHandler = std::function<void()>;

    static constexpr uint8_t kPort01Full = 0x01;
    static constexpr uint8_t kPort23Full = 0x02;
    static constexpr uint8_t kPort01FullMaster = 0x04;
    static constexpr uint8_t kPort23FullMaster = 0x08;

    void set_nmi_handler(NmiHandler handler) { nmi_handler_ = std::move(handler); }
    // Called when the 68000 writes a non-zero nibble to mode 4 (sound CPU reset).
    void set_reset_handler(ResetHandler handler) { reset_handler_ = std::move(handler); }

    void reset();

    void port_w(uint8_t value);
    void comm_w(uint8_t value);
    uint8_t comm_r();

    void slave_port_w(uint8_t value);
    void slave_comm_w(uint8_t value);
    uint8_t slave_comm_r();

    uint8_t status() const { return status_; }

private:
    void interrupt_controller();

    std::array<uint8_t, 4> slave_data_{};
    std::array<uint8_t, 4> master_data_{};
    uint8_t main_mode_ = 0;
    uint8_t sub_mode_ = 0;
    uint8_t status_ = 0;
    bool nmi_enabled_ = false;
    bool nmi_req_ = false;

    NmiHandler nmi_handler_;
    ResetHandler reset_handler_;
};

}  // namespace dsp
