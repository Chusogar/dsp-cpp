#include "machine/ppi8255.h"

namespace dsp {

void Ppi8255::reset() {
    group_a_mode_ = 0;
    group_b_mode_ = 0;
    port_a_dir_ = true;
    port_b_dir_ = true;
    port_ch_dir_ = true;
    port_cl_dir_ = true;
    for (int i = 0; i < 3; ++i) {
        in_mask_[i] = 0;
        out_mask_[i] = 0;
        latch_[i] = 0;
        output_val_[i] = 0;
    }
    set_mode(0x9b, false);
}

void Ppi8255::set_mode(uint8_t data, bool call_handlers) {
    control_ = data;
    group_a_mode_ = (data >> 5) & 3;
    group_b_mode_ = (data >> 2) & 1;
    port_a_dir_ = (data & 0x10) != 0;
    port_b_dir_ = (data & 0x02) != 0;
    port_ch_dir_ = (data & 0x08) != 0;
    port_cl_dir_ = (data & 0x01) != 0;
    if (group_a_mode_ == 3) group_a_mode_ = 2;

    if (group_a_mode_ == 2) {
        in_mask_[0] = 0xff;
        out_mask_[0] = 0xff;
    } else if (port_a_dir_) {
        in_mask_[0] = 0xff;
        out_mask_[0] = 0;
    } else {
        in_mask_[0] = 0;
        out_mask_[0] = 0xff;
    }

    if (port_b_dir_) {
        in_mask_[1] = 0xff;
        out_mask_[1] = 0;
    } else {
        in_mask_[1] = 0;
        out_mask_[1] = 0xff;
    }

    in_mask_[2] = 0;
    out_mask_[2] = 0;
    if (port_ch_dir_) {
        in_mask_[2] |= 0xf0;
    } else {
        out_mask_[2] |= 0xf0;
    }
    if (port_cl_dir_) {
        in_mask_[2] |= 0x0f;
    } else {
        out_mask_[2] |= 0x0f;
    }

    // Mode 1/2 reserve some Port C bits; CPC uses mode 0 exclusively.
    if (group_a_mode_ == 1) {
        in_mask_[2] &= 0xc7;
        out_mask_[2] &= 0xc7;
    } else if (group_a_mode_ == 2) {
        in_mask_[2] &= 0x07;
        out_mask_[2] &= 0x07;
    }
    if (group_b_mode_ == 1) {
        in_mask_[2] &= 0xf8;
        out_mask_[2] &= 0xf8;
    }

    for (int i = 0; i < 3; ++i) {
        latch_[i] = 0;
        output_val_[i] = 0;
    }
    if (call_handlers) {
        write_port(0);
        write_port(1);
        write_port(2);
    }
}

uint8_t Ppi8255::read_port(int port) {
    uint8_t result = 0;
    if (in_mask_[port]) {
        uint8_t input = 0xff;
        if (port == 0 && read_a_) input = read_a_();
        if (port == 1 && read_b_) input = read_b_();
        if (port == 2 && read_c_) input = read_c_();
        result |= input & in_mask_[port];
    }
    result |= output_val_[port] & out_mask_[port];
    return result;
}

void Ppi8255::write_port(int port) {
    output_val_[port] = (output_val_[port] & ~out_mask_[port]) | (latch_[port] & out_mask_[port]);
    const uint8_t value = output_val_[port];
    if (port == 0 && write_a_) write_a_(value);
    if (port == 1 && write_b_) write_b_(value);
    if (port == 2 && write_c_) write_c_(value);
}

uint8_t Ppi8255::read(uint8_t port) {
    port &= 3;
    if (port == 3) return control_;
    return read_port(port);
}

void Ppi8255::write(uint8_t port, uint8_t data) {
    port &= 3;
    if (port == 3) {
        if (data & 0x80) {
            set_mode(data, true);
        } else {
            // Bit set/reset on Port C.
            const int bit = (data >> 1) & 7;
            if (data & 1) {
                latch_[2] = uint8_t(latch_[2] | (1u << bit));
            } else {
                latch_[2] = uint8_t(latch_[2] & ~(1u << bit));
            }
            write_port(2);
        }
        return;
    }
    latch_[port] = data;
    write_port(port);
}

}  // namespace dsp
