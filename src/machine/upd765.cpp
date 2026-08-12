#include "machine/upd765.h"

#include <cstring>

namespace dsp {

Upd765::Upd765() { reset(); }

void Upd765::reset() {
    motor_ = false;
    curr_drv_ = 0;
    exec_cmd_phase_ = false;
    result_phase_ = false;
    seek_track_ = false;
    status_ = 0x80;
    st0_ = st1_ = st2_ = st3_ = 0;
    cmd_.fill(0);
    result_.fill(0);
    cmd_ptr_ = 0;
    res_ptr_ = 0;
    res_counter_ = 0;
    data_ptr_ = 0;
    data_length_ = 0;
    data_counter_ = 0;
    status_timeout_ = 0;
    read_status_timeout_ = 0;
    for (auto& d : dsk_) {
        d.cont_multi = 3;
        d.max_multi = 3;
        d.track_actual = 0;
        d.cara_actual = 0;
        d.sector_actual = 0;
    }
}

void Upd765::set_motor(uint8_t value) { motor_ = value != 0; }

void Upd765::get_drive() {
    curr_drv_ = cmd_[1] & 1;
    dsk_[curr_drv_].cara_actual = (cmd_[1] >> 2) & 1;
    st0_ = cmd_[1] & 7;
}

void Upd765::get_res7() {
    auto& d = dsk_[curr_drv_];
    auto& tr = d.cur_track();
    const uint8_t si = d.sector_actual;
    result_[0] = st0_;
    result_[1] = st1_;
    result_[2] = st2_;
    if (tr.number_sector > 0 && si < tr.number_sector) {
        result_[3] = tr.sector[si].track;
        result_[4] = tr.sector[si].head;
        result_[5] = tr.sector[si].sector;
        result_[6] = tr.sector[si].sector_size;
    } else {
        result_[3] = d.track_actual;
        result_[4] = d.cara_actual;
        result_[5] = 0;
        result_[6] = 2;
    }
    status_ = 0xd0;
    res_ptr_ = 0;
    res_counter_ = 7;
    st0_ = st1_ = st2_ = 0;
    exec_cmd_phase_ = false;
    result_phase_ = true;
}

bool Upd765::buscar_sector() {
    auto& d = dsk_[curr_drv_];
    auto& tr = d.cur_track();
    if (tr.number_sector == 0) return true;

    int index_count = 0;
    while (index_count != 2) {
        if (d.sector_actual + 1 > tr.number_sector) {
            d.sector_actual = 0;
            ++index_count;
        }
        auto& sec = tr.sector[d.sector_actual];
        if (sec.sector == cmd_[4]) {
            if (sec.track == cmd_[2]) {
                if (sec.head == cmd_[3]) {
                    if (sec.sector_size == cmd_[5]) {
                        if (cmd_[4] == cmd_[6]) st1_ |= 0x80;  // end of cylinder
                        st1_ |= sec.status1 & 0x20;
                        st2_ |= sec.status1 & 0x60;
                        return true;
                    }
                    st1_ |= 0x80;
                    return true;
                }
            } else {
                st1_ |= 0x04;  // no data
                st2_ |= 0x10;  // wrong cylinder
                if (sec.track == 0xff) st2_ |= 0x02;
                return true;
            }
        }
        ++d.sector_actual;
    }
    return false;
}

bool Upd765::saltar_sector() const {
    const auto& d = dsk_[curr_drv_];
    const auto& sec = d.cur_track().sector[d.sector_actual];
    if ((cmd_[0] & 0x20) == 0) return false;
    const uint8_t op = cmd_[0] & 0x1f;
    if (op == 0x06) {
        return (sec.status2 & 0x40) != 0;
    }
    if (op == 0x0c) {
        return (sec.status2 & 0x40) == 0;
    }
    return false;
}

bool Upd765::read_data_stop() {
    auto& d = dsk_[curr_drv_];
    auto& sec = d.cur_track().sector[d.sector_actual];
    if ((cmd_[0] & 0x20) == 0) {
        const uint8_t op = cmd_[0] & 0x1f;
        if (op == 0x06 && (sec.status2 & 0x40)) {
            st2_ |= 0x40;
            return true;
        }
        if (op == 0x0c && (sec.status2 & 0x40) == 0) {
            st2_ |= 0x40;
            return true;
        }
    }
    if (sec.status1 & 0x20) {
        st1_ |= 0x20;
        return true;
    }
    return false;
}

void Upd765::read_sector() {
    for (;;) {
        if (buscar_sector()) {
            if (saltar_sector()) {
                if (cmd_[4] == cmd_[6]) {
                    st1_ &= 0x7f;
                    get_res7();
                    return;
                }
                ++cmd_[4];
                continue;
            }
            break;
        }
        st0_ |= 0x40;
        st1_ |= 0x04;
        get_res7();
        return;
    }

    exec_cmd_phase_ = true;
    auto& d = dsk_[curr_drv_];
    auto& sec = d.cur_track().sector[d.sector_actual];
    data_ptr_ = sec.data_offset;
    data_length_ = sec.data_length;
    if (sec.multi) {
        if (d.cont_multi >= d.max_multi - 1) {
            d.cont_multi = 0;
        } else {
            ++d.cont_multi;
        }
        const uint16_t nominal = uint16_t(128 << (sec.sector_size & 7));
        data_ptr_ = sec.data_offset + uint32_t(d.cont_multi) * nominal;
        data_length_ = nominal;
    }
    data_counter_ = 0;
    status_ = 0xf0;  // RQM | DIO | EXM | BUSY
}

void Upd765::read_track() {
    auto& d = dsk_[curr_drv_];
    auto& tr = d.cur_track();
    if (tr.number_sector == 0 || tr.data.empty()) {
        st0_ = 0xc0;
        st1_ = 0x20;
        st2_ = 0x01;
        get_res7();
        return;
    }
    // Sequential read from start of track data for each sector in order.
    if (d.sector_read_track >= tr.number_sector) d.sector_read_track = 0;
    auto& sec = tr.sector[d.sector_read_track];
    data_ptr_ = sec.data_offset;
    data_length_ = sec.data_length;
    data_counter_ = 0;
    exec_cmd_phase_ = true;
    status_ = 0xf0;
}

void Upd765::seek_track(uint8_t track) {
    dsk_[curr_drv_].track_actual = track;
    if (track >= dsk_[curr_drv_].nbof_tracks && dsk_[curr_drv_].open) {
        // Still accept; AMSDOS may probe.
    }
    st0_ = 0x20 | (cmd_[1] & 7);  // seek end
    seek_track_ = true;
    status_ = 0x80;
}

void Upd765::exec_write_command() {
    const uint8_t op = cmd_[0] & 0x1f;
    switch (op) {
        case 2:  // Read track
            get_drive();
            if (!dsk_[curr_drv_].open) {
                st0_ |= 0x48;
                get_res7();
            } else {
                dsk_[curr_drv_].sector_read_track = 0;
                read_track();
            }
            break;

        case 3:  // Specify
            exec_cmd_phase_ = false;
            result_phase_ = false;
            status_ = (status_ & ~(0x40 | 0x20 | 0x10)) | 0x80;
            break;

        case 4: {  // Sense drive status
            curr_drv_ = cmd_[1] & 1;
            st3_ = (cmd_[1] & 1) | (cmd_[1] & 4);
            if (dsk_[curr_drv_].write_protected) st3_ |= 0x40;
            if (dsk_[curr_drv_].open) st3_ |= 0x20;  // ready
            if (dsk_[curr_drv_].track_actual == 0) st3_ |= 0x10;
            st3_ |= uint8_t(dsk_[curr_drv_].nbof_heads << 3);
            res_counter_ = 1;
            res_ptr_ = 0;
            result_[0] = st3_;
            exec_cmd_phase_ = false;
            result_phase_ = true;
            status_ = (status_ | 0x40) & ~0x20;
            break;
        }

        case 5:  // Write data
        case 9:  // Write deleted
            st0_ = st1_ = st2_ = 0;
            get_drive();
            if (!dsk_[curr_drv_].open) {
                st0_ |= 0x48;
                get_res7();
            } else if (dsk_[curr_drv_].write_protected) {
                st0_ |= 0x40;
                st1_ |= 0x02;
                get_res7();
            } else {
                // Write not fully implemented — report protect.
                st0_ |= 0x40;
                st1_ |= 0x02;
                get_res7();
            }
            break;

        case 6:   // Read data
        case 12:  // Read deleted
            st0_ = st1_ = st2_ = 0;
            get_drive();
            if (!dsk_[curr_drv_].open) {
                st0_ |= 0x48;
                get_res7();
            } else {
                read_sector();
            }
            break;

        case 7:  // Recalibrate
            st0_ = 0x20;
            st1_ = st2_ = 0;
            get_drive();
            dsk_[curr_drv_].track_actual = 0;
            status_ = 0x80;
            seek_track_ = true;
            exec_cmd_phase_ = false;
            result_phase_ = false;
            break;

        case 8: {  // Sense interrupt
            res_ptr_ = 0;
            if (seek_track_) {
                result_[0] = st0_;
                result_[1] = dsk_[curr_drv_].track_actual;
                res_counter_ = 2;
                seek_track_ = false;
                st0_ = 0;
            } else {
                result_[0] = 0x80;  // invalid
                res_counter_ = 1;
            }
            exec_cmd_phase_ = false;
            result_phase_ = true;
            status_ = 0xd0;
            break;
        }

        case 10: {  // Read ID
            st0_ = st1_ = st2_ = 0;
            get_drive();
            if (!dsk_[curr_drv_].open) {
                st0_ |= 0x48;
                get_res7();
                break;
            }
            auto& d = dsk_[curr_drv_];
            auto& tr = d.cur_track();
            if (tr.number_sector == 0) {
                st0_ |= 0x40;
                st1_ |= 0x01;
                get_res7();
                break;
            }
            if (d.sector_actual >= tr.number_sector) d.sector_actual = 0;
            get_res7();
            // Advance to next sector for subsequent Read ID.
            d.sector_actual = uint8_t((d.sector_actual + 1) % tr.number_sector);
            break;
        }

        case 15:  // Seek
            st0_ = st1_ = st2_ = 0;
            get_drive();
            if (!dsk_[curr_drv_].open) {
                st0_ |= 0x48;
            } else {
                seek_track(cmd_[2]);
            }
            exec_cmd_phase_ = false;
            break;

        default:
            res_counter_ = 1;
            res_ptr_ = 0;
            st0_ = 0x80 | st0_;
            if (exec_cmd_phase_) {
                st1_ |= 0x10;
                st0_ = (st0_ & 0x3f) | 0x40;
                status_ = 0x80;
            }
            result_[0] = st0_;
            exec_cmd_phase_ = false;
            result_phase_ = true;
            seek_track_ = true;
            break;
    }
}

uint8_t Upd765::exec_read_command() {
    const uint8_t op = cmd_[0] & 0x1f;
    auto& d = dsk_[curr_drv_];
    auto& tr = d.cur_track();

    if (op == 2) {  // read track
        if (tr.data.empty()) {
            st0_ = 0xc0;
            st1_ = 0x20;
            st2_ = 0x01;
            get_res7();
            return 0;
        }
        uint8_t ret = 0;
        if (data_ptr_ < tr.data.size()) ret = tr.data[data_ptr_];
        ++data_ptr_;
        ++data_counter_;
        if (data_counter_ >= data_length_) {
            if (d.sector_read_track >= cmd_[6] - 1) {
                st1_ |= 0x80;
                get_res7();
            } else {
                ++d.sector_read_track;
                read_track();
            }
        }
        return ret;
    }

    if (op == 6 || op == 12) {
        if (tr.data.empty()) {
            st0_ = 0xc0;
            st1_ = 0x20;
            st2_ = 0x01;
            get_res7();
            return 0;
        }
        uint8_t ret = 0;
        if (data_ptr_ < tr.data.size()) ret = tr.data[data_ptr_];
        ++data_ptr_;
        ++data_counter_;
        if (data_counter_ >= data_length_) {
            if (read_data_stop() || cmd_[4] == cmd_[6]) {
                get_res7();
            } else {
                ++cmd_[4];
                ++d.sector_actual;
                read_sector();
            }
        }
        return ret;
    }
    return 0;
}

uint8_t Upd765::get_result() {
    const uint8_t ret = result_[res_ptr_];
    ++res_ptr_;
    if (res_ptr_ >= res_counter_) {
        status_ = 0x80;
        result_phase_ = false;
        result_.fill(0);
        cmd_.fill(0);
    }
    return ret;
}

void Upd765::write_data(uint8_t value) {
    status_timeout_ = 0;
    if (!exec_cmd_phase_ || !result_phase_) {
        if (cmd_ptr_ == 0) {
            cmd_[0] = value;
            cmd_ptr_ = 1;
            status_ |= 0x10;  // busy
        } else if (cmd_ptr_ < kBytesInCmd[cmd_[0] & 0x1f]) {
            cmd_[cmd_ptr_] = value;
            ++cmd_ptr_;
        }
        if (cmd_ptr_ == kBytesInCmd[cmd_[0] & 0x1f]) {
            cmd_ptr_ = 0;
            status_ |= 0x20;
            exec_write_command();
        }
    } else {
        exec_write_command();
    }
}

uint8_t Upd765::read_data() {
    status_timeout_ = 0;
    if (exec_cmd_phase_) return exec_read_command();
    if (result_phase_) return get_result();
    return 0;
}

uint8_t Upd765::read_status() {
    // Soft timeout used by some protections (Hexagon etc.).
    ++status_timeout_;
    if (status_timeout_ > 0x10) {
        if (seek_track_) {
            ++read_status_timeout_;
            if (read_status_timeout_ > 0x20) {
                status_ = 0xc0;
                if (read_status_timeout_ > 0x40) read_status_timeout_ = 0;
            } else {
                status_ = 0x50;
            }
            seek_track_ = false;
        } else {
            status_ = 0xf0;
            st0_ = (st0_ & 0x3f) | 0x40;
            st1_ |= 0x10;
            seek_track_ = true;
        }
        cmd_ptr_ = 0;
        status_timeout_ = 0;
    }
    return status_;
}

}  // namespace dsp
