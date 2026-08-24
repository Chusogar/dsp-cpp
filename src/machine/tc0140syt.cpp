#include "machine/tc0140syt.h"

namespace dsp {

void Tc0140Syt::reset() {
    slave_data_.fill(0);
    master_data_.fill(0);
    main_mode_ = 0;
    sub_mode_ = 0;
    status_ = 0;
    nmi_enabled_ = false;
    nmi_req_ = false;
}

void Tc0140Syt::interrupt_controller() {
    if (nmi_req_ && nmi_enabled_) {
        nmi_req_ = false;
        if (nmi_handler_) nmi_handler_();
    }
}

void Tc0140Syt::port_w(uint8_t value) { main_mode_ = uint8_t(value & 0x0f); }

void Tc0140Syt::comm_w(uint8_t value) {
    value = uint8_t(value & 0x0f);
    switch (main_mode_) {
        case 0:
            slave_data_[0] = value;
            main_mode_ = 1;
            break;
        case 1:
            slave_data_[1] = value;
            main_mode_ = 2;
            status_ = uint8_t(status_ | kPort01Full);
            nmi_req_ = true;
            interrupt_controller();
            break;
        case 2:
            slave_data_[2] = value;
            main_mode_ = 3;
            break;
        case 3:
            slave_data_[3] = value;
            main_mode_ = 4;
            status_ = uint8_t(status_ | kPort23Full);
            nmi_req_ = true;
            interrupt_controller();
            break;
        case 4:
            // A high-to-low on this port resets the sound CPU.
            if (value != 0) {
                reset();
                if (reset_handler_) reset_handler_();
            }
            break;
        default: break;
    }
}

uint8_t Tc0140Syt::comm_r() {
    switch (main_mode_) {
        case 0:
            main_mode_ = 1;
            return master_data_[0];
        case 1:
            status_ = uint8_t(status_ & ~kPort01FullMaster);
            main_mode_ = 2;
            return master_data_[1];
        case 2:
            main_mode_ = 3;
            return master_data_[2];
        case 3:
            status_ = uint8_t(status_ & ~kPort23FullMaster);
            main_mode_ = 4;
            return master_data_[3];
        case 4: return status_;
        default: return 0;
    }
}

void Tc0140Syt::slave_port_w(uint8_t value) { sub_mode_ = uint8_t(value & 0x0f); }

void Tc0140Syt::slave_comm_w(uint8_t value) {
    value = uint8_t(value & 0x0f);
    switch (sub_mode_) {
        case 0:
            master_data_[0] = value;
            sub_mode_ = 1;
            break;
        case 1:
            master_data_[1] = value;
            sub_mode_ = 2;
            status_ = uint8_t(status_ | kPort01FullMaster);
            break;
        case 2:
            master_data_[2] = value;
            sub_mode_ = 3;
            break;
        case 3:
            master_data_[3] = value;
            sub_mode_ = 4;
            status_ = uint8_t(status_ | kPort23FullMaster);
            break;
        case 4: break;
        case 5: nmi_enabled_ = false; break;
        case 6: nmi_enabled_ = true; break;
        default: break;
    }
    interrupt_controller();
}

uint8_t Tc0140Syt::slave_comm_r() {
    uint8_t result = 0;
    switch (sub_mode_) {
        case 0:
            result = slave_data_[0];
            sub_mode_ = 1;
            break;
        case 1:
            status_ = uint8_t(status_ & ~kPort01Full);
            result = slave_data_[1];
            sub_mode_ = 2;
            break;
        case 2:
            result = slave_data_[2];
            sub_mode_ = 3;
            break;
        case 3:
            status_ = uint8_t(status_ & ~kPort23Full);
            result = slave_data_[3];
            sub_mode_ = 4;
            break;
        case 4: result = status_; break;
        default: break;
    }
    interrupt_controller();
    return result;
}

}  // namespace dsp
