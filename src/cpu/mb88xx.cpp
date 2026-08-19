#include "cpu/mb88xx.h"

#include <algorithm>

namespace dsp {

Mb88::Mb88(Type type, uint32_t clock) : type_(type), clock_(clock) {
    switch (type) {
        case Type::Mb8841:
        case Type::Mb8842:
            program_width_ = 11;
            data_width_ = 7;
            break;
        case Type::Mb8843:
        case Type::Mb8844:
        default:
            program_width_ = 10;
            data_width_ = 6;
            break;
    }
    program_mask_ = uint16_t((1u << program_width_) - 1);
    data_mask_ = uint8_t((1u << data_width_) - 1);
    program_.assign(size_t(program_mask_) + 1, 0);
    ram_.assign(size_t(data_mask_) + 1, 0);
    read_k_ = [] { return uint8_t(0); };
    write_o_ = [](uint8_t) {};
    write_p_ = [](uint8_t) {};
    for (int i = 0; i < 4; i++) {
        read_r_[i] = [] { return uint8_t(0); };
        write_r_[i] = [](uint8_t) {};
    }
    read_si_ = [] { return 0; };
}

void Mb88::set_program_rom(const uint8_t* data, size_t size) {
    const size_t n = std::min(size, program_.size());
    if (data != nullptr && n > 0) std::copy(data, data + n, program_.begin());
}

void Mb88::set_r_read(int port, ReadNibble handler) {
    if (port >= 0 && port < 4) read_r_[port] = std::move(handler);
}

void Mb88::set_r_write(int port, WriteNibble handler) {
    if (port >= 0 && port < 4) write_r_[port] = std::move(handler);
}

void Mb88::reset() {
    pc_ = 0;
    pa_ = 0;
    sp_[0] = sp_[1] = sp_[2] = sp_[3] = 0;
    si_ = 0;
    a_ = 0;
    x_ = 0;
    y_ = 0;
    st_ = 1;
    zf_ = 0;
    cf_ = 0;
    vf_ = 0;
    sf_ = 0;
    nf_ = 0;
    pio_ = 0;
    th_ = 0;
    tl_ = 0;
    tp_ = 0;
    sb_ = 0;
    sb_count_ = 0;
    pending_interrupt_ = 0;
    o_output_ = 0;
    tc_ = false;
    serial_cycle_acc_ = 0;
}

void Mb88::set_irq(IrqLine state) {
    const bool asserted = state != IrqLine::Clear;
    if ((pio_ & kIntExternal) && !nf_ && asserted) pending_interrupt_ |= kIntExternal;
    nf_ = asserted ? 1 : 0;
    if (state == IrqLine::Pulse) nf_ = 0;
}

void Mb88::set_tc(bool state) {
    if (tc_ && !state && (pio_ & 0x40)) increment_timer();
    tc_ = state;
}

void Mb88::set_reset_line(bool asserted) {
    if (reset_asserted_ && !asserted) reset();
    reset_asserted_ = asserted;
}

int Mb88::run(int cycles) {
    if (reset_asserted_ || cycles <= 0) return 0;
    icount_ = cycles;
    const int start = icount_;
    while (icount_ > 0) execute_one();
    return start - icount_;
}

uint8_t Mb88::fetch() { return program_[size_t(pc()) & program_mask_]; }

uint8_t Mb88::read_data(uint8_t ea) const { return ram_[size_t(ea & data_mask_)] & 0x0f; }

void Mb88::write_data(uint8_t ea, uint8_t value) { ram_[size_t(ea & data_mask_)] = uint8_t(value & 0x0f); }

void Mb88::inc_pc() {
    pc_++;
    if (pc_ >= 0x40) {
        pc_ = 0;
        pa_++;
    }
}

void Mb88::write_pla(uint8_t index) {
    uint8_t mask = 0xff;
    if (pla_bits_ == 8) {
        const uint8_t shift = (index & 0x10) ? 4 : 0;
        mask = uint8_t(0x0f << shift);
        o_output_ = uint8_t((o_output_ & uint8_t(~mask)) | ((index << shift) & mask));
    } else {
        o_output_ = uint8_t(index);
    }
    write_o_(o_output_);
}

void Mb88::update_pio_enable(uint8_t newpio) {
    if ((pio_ ^ newpio) & 0x30) serial_cycle_acc_ = 0;
    pio_ = newpio;
}

void Mb88::increment_timer() {
    tl_ = uint8_t((tl_ + 1) & 0x0f);
    if (tl_ == 0) {
        th_ = uint8_t((th_ + 1) & 0x0f);
        if (th_ == 0) {
            vf_ = 1;
            pending_interrupt_ |= kIntTimer;
        }
    }
}

void Mb88::serial_tick() {
    if ((pio_ & 0x30) != 0x20) return;
    sb_count_++;
    if (sb_count_ >= kSerialDisableThresh) return;
    if (!sf_) {
        sb_ = uint8_t((sb_ >> 1) | (read_si_() ? 8 : 0));
        if (sb_count_ >= 4) {
            sf_ = 1;
            pending_interrupt_ |= kIntSerial;
        }
    }
}

void Mb88::update_pio(int cycles) {
    if (pio_ & 0x80) {
        tp_ = uint8_t(tp_ + cycles);
        while (tp_ >= kTimerPrescale) {
            tp_ = uint8_t(tp_ - kTimerPrescale);
            increment_timer();
        }
    }
    if ((pio_ & 0x30) == 0x20) {
        serial_cycle_acc_ += cycles;
        while (serial_cycle_acc_ >= 1) {
            serial_cycle_acc_--;
            serial_tick();
        }
    }
    if (pending_interrupt_ & pio_) {
        const uint16_t cur = pc();
        sp_[si_] = cur;
        sp_[si_] |= uint16_t(cf_ << 15);
        sp_[si_] |= uint16_t(zf_ << 14);
        sp_[si_] |= uint16_t(st_ << 13);
        si_ = uint8_t((si_ + 1) & 3);
        if (pending_interrupt_ & pio_ & kIntExternal)
            pc_ = 0x02;
        else if (pending_interrupt_ & pio_ & kIntTimer)
            pc_ = 0x04;
        else
            pc_ = 0x06;
        pa_ = 0;
        st_ = 1;
        pending_interrupt_ = 0;
        icount_ -= 3;
    }
}

void Mb88::execute_one() {
    uint8_t opcode = fetch();
    inc_pc();
    uint8_t oc = 1;
    uint8_t arg = 0;
    const uint8_t ea = uint8_t((x_ << 4) + y_);

    auto update_st_c = [&](uint8_t v) { st_ = (v & 0x10) ? 0 : 1; };
    auto update_st_z = [&](uint8_t v) { st_ = (v == 0) ? 0 : 1; };
    auto update_cf = [&](uint8_t v) { cf_ = ((v & 0x10) == 0) ? 0 : 1; };
    auto update_zf = [&](uint8_t v) { zf_ = (v != 0) ? 0 : 1; };

    switch (opcode) {
        case 0x00:
            st_ = 1;
            break;
        case 0x01:
            write_pla(uint8_t((cf_ << 4) | a_));
            st_ = 1;
            break;
        case 0x02:
            write_p_(a_);
            st_ = 1;
            break;
        case 0x03:
            write_r_[y_ & 3](a_);
            st_ = 1;
            break;
        case 0x04:
            y_ = a_;
            st_ = 1;
            break;
        case 0x05:
            th_ = a_;
            st_ = 1;
            break;
        case 0x06:
            tl_ = a_;
            st_ = 1;
            break;
        case 0x07:
            sb_ = a_;
            st_ = 1;
            break;
        case 0x08:
            y_++;
            update_st_c(y_);
            y_ &= 0x0f;
            update_zf(y_);
            break;
        case 0x09:
            arg = read_data(ea);
            arg++;
            update_st_c(arg);
            arg &= 0x0f;
            update_zf(arg);
            write_data(ea, arg);
            break;
        case 0x0a:
            write_data(ea, a_);
            y_++;
            update_st_c(y_);
            y_ &= 0x0f;
            update_zf(y_);
            break;
        case 0x0b:
            arg = read_data(ea);
            write_data(ea, a_);
            a_ = arg;
            update_zf(a_);
            st_ = 1;
            break;
        case 0x0c:
            a_ = uint8_t((a_ << 1) | cf_);
            update_st_c(a_);
            cf_ = uint8_t(st_ ^ 1);
            a_ &= 0x0f;
            update_zf(a_);
            break;
        case 0x0d:
            a_ = read_data(ea);
            update_zf(a_);
            st_ = 1;
            break;
        case 0x0e:
            arg = uint8_t(read_data(ea) + a_ + cf_);
            update_st_c(arg);
            cf_ = uint8_t(st_ ^ 1);
            a_ = uint8_t(arg & 0x0f);
            update_zf(a_);
            break;
        case 0x0f:
            a_ = uint8_t(a_ & read_data(ea));
            update_zf(a_);
            st_ = uint8_t(zf_ ^ 1);
            break;
        case 0x10:
            if (cf_ || a_ > 9) a_ = uint8_t(a_ + 6);
            update_st_c(a_);
            cf_ = uint8_t(st_ ^ 1);
            a_ &= 0x0f;
            break;
        case 0x11:
            if (cf_ || a_ > 9) a_ = uint8_t(a_ + 10);
            update_st_c(a_);
            cf_ = uint8_t(st_ ^ 1);
            a_ &= 0x0f;
            break;
        case 0x12:
            a_ = uint8_t(read_k_() & 0x0f);
            update_zf(a_);
            st_ = 1;
            break;
        case 0x13:
            a_ = uint8_t(read_r_[y_ & 3]() & 0x0f);
            update_zf(a_);
            st_ = 1;
            break;
        case 0x14:
            a_ = y_;
            update_zf(a_);
            st_ = 1;
            break;
        case 0x15:
            a_ = th_;
            update_zf(a_);
            st_ = 1;
            break;
        case 0x16:
            a_ = tl_;
            update_zf(a_);
            st_ = 1;
            break;
        case 0x17:
            a_ = sb_;
            update_zf(a_);
            st_ = 1;
            break;
        case 0x18:
            y_--;
            update_st_c(y_);
            y_ &= 0x0f;
            break;
        case 0x19:
            arg = read_data(ea);
            arg--;
            update_st_c(arg);
            arg &= 0x0f;
            update_zf(arg);
            write_data(ea, arg);
            break;
        case 0x1a:
            write_data(ea, a_);
            y_--;
            update_st_c(y_);
            y_ &= 0x0f;
            update_zf(y_);
            break;
        case 0x1b:
            arg = x_;
            x_ = a_;
            a_ = arg;
            update_zf(a_);
            st_ = 1;
            break;
        case 0x1c:
            a_ = uint8_t(a_ | (cf_ << 4));
            update_st_c(uint8_t(a_ << 4));
            cf_ = uint8_t(st_ ^ 1);
            a_ = uint8_t((a_ >> 1) & 0x0f);
            update_zf(a_);
            break;
        case 0x1d:
            write_data(ea, a_);
            st_ = 1;
            break;
        case 0x1e:
            arg = uint8_t(read_data(ea) - a_ - cf_);
            update_st_c(arg);
            cf_ = uint8_t(st_ ^ 1);
            a_ = uint8_t(arg & 0x0f);
            update_zf(a_);
            break;
        case 0x1f:
            a_ = uint8_t(a_ | read_data(ea));
            update_zf(a_);
            st_ = uint8_t(zf_ ^ 1);
            break;
        case 0x20:
            arg = read_r_[y_ / 4]();
            write_r_[y_ / 4](uint8_t(arg | (1 << (y_ % 4))));
            st_ = 1;
            break;
        case 0x21:
            cf_ = 1;
            st_ = 1;
            break;
        case 0x22:
            arg = read_r_[y_ / 4]();
            write_r_[y_ / 4](uint8_t(arg & ~(1 << (y_ % 4))));
            st_ = 1;
            break;
        case 0x23:
            cf_ = 0;
            st_ = 1;
            break;
        case 0x24:
            arg = read_r_[y_ / 4]();
            st_ = (arg & (1 << (y_ % 4))) ? 0 : 1;
            break;
        case 0x25:
            st_ = uint8_t(nf_ ^ 1);
            break;
        case 0x26:
            st_ = uint8_t(vf_ ^ 1);
            vf_ = 0;
            break;
        case 0x27:
            st_ = uint8_t(sf_ ^ 1);
            if (sf_) sb_count_ = 0;
            sf_ = 0;
            break;
        case 0x28:
            st_ = uint8_t(cf_ ^ 1);
            break;
        case 0x29:
            st_ = uint8_t(zf_ ^ 1);
            break;
        case 0x2a:
            write_data(ea, sb_);
            update_zf(sb_);
            st_ = 1;
            break;
        case 0x2b:
            sb_ = read_data(ea);
            update_zf(sb_);
            st_ = 1;
            break;
        case 0x2c:
            si_ = uint8_t((si_ - 1) & 3);
            pc_ = uint8_t(sp_[si_] & 0x3f);
            pa_ = uint8_t((sp_[si_] >> 6) & 0x1f);
            st_ = 1;
            break;
        case 0x2d:
            a_ = uint8_t(((~a_) + 1) & 0x0f);
            update_st_z(a_);
            break;
        case 0x2e:
            arg = uint8_t(read_data(ea) - a_);
            update_cf(arg);
            arg &= 0x0f;
            update_st_z(arg);
            zf_ = uint8_t(st_ ^ 1);
            break;
        case 0x2f:
            a_ = uint8_t(a_ ^ read_data(ea));
            update_st_z(a_);
            zf_ = uint8_t(st_ ^ 1);
            break;
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
            arg = read_data(ea);
            write_data(ea, uint8_t(arg | (1 << (opcode & 3))));
            st_ = 1;
            break;
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
            arg = read_data(ea);
            write_data(ea, uint8_t(arg & ~(1 << (opcode & 3))));
            st_ = 1;
            break;
        case 0x38:
        case 0x39:
        case 0x3a:
        case 0x3b:
            arg = read_data(ea);
            st_ = (arg & (1 << (opcode & 3))) ? 0 : 1;
            break;
        case 0x3c:
            si_ = uint8_t((si_ - 1) & 3);
            pc_ = uint8_t(sp_[si_] & 0x3f);
            pa_ = uint8_t((sp_[si_] >> 6) & 0x1f);
            st_ = uint8_t((sp_[si_] >> 13) & 1);
            zf_ = uint8_t((sp_[si_] >> 14) & 1);
            cf_ = uint8_t((sp_[si_] >> 15) & 1);
            break;
        case 0x3d:
            pa_ = uint8_t(fetch() & 0x1f);
            pc_ = uint8_t(a_ * 4);
            oc = 2;
            st_ = 1;
            break;
        case 0x3e:
            update_pio_enable(uint8_t(pio_ | fetch()));
            inc_pc();
            oc = 2;
            st_ = 1;
            break;
        case 0x3f:
            update_pio_enable(uint8_t(pio_ & ~fetch()));
            inc_pc();
            oc = 2;
            st_ = 1;
            break;
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
            arg = read_r_[0]();
            write_r_[0](uint8_t(arg | (1 << (opcode & 3))));
            st_ = 1;
            break;
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x47:
            arg = read_r_[0]();
            write_r_[0](uint8_t(arg & ~(1 << (opcode & 3))));
            st_ = 1;
            break;
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
            arg = read_r_[2]();
            st_ = (arg & (1 << (opcode & 3))) ? 0 : 1;
            break;
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
            st_ = (a_ & (1 << (opcode & 3))) ? 0 : 1;
            break;
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
            arg = read_data(uint8_t(opcode & 3));
            write_data(uint8_t(opcode & 3), a_);
            a_ = arg;
            update_zf(a_);
            st_ = 1;
            break;
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x57:
            arg = read_data(uint8_t((opcode & 3) + 4));
            write_data(uint8_t((opcode & 3) + 4), y_);
            y_ = arg;
            update_zf(y_);
            st_ = 1;
            break;
        case 0x58:
        case 0x59:
        case 0x5a:
        case 0x5b:
        case 0x5c:
        case 0x5d:
        case 0x5e:
        case 0x5f:
            x_ = uint8_t(opcode & 7);
            update_zf(x_);
            st_ = 1;
            break;
        case 0x60:
        case 0x61:
        case 0x62:
        case 0x63:
        case 0x64:
        case 0x65:
        case 0x66:
        case 0x67:
            arg = fetch();
            inc_pc();
            oc = 2;
            if (st_) {
                sp_[si_] = pc();
                si_ = uint8_t((si_ + 1) & 3);
                pc_ = uint8_t(arg & 0x3f);
                pa_ = uint8_t(((opcode & 7) << 2) | (arg >> 6));
            }
            st_ = 1;
            break;
        case 0x68:
        case 0x69:
        case 0x6a:
        case 0x6b:
        case 0x6c:
        case 0x6d:
        case 0x6e:
        case 0x6f:
            arg = fetch();
            inc_pc();
            oc = 2;
            if (st_) {
                pc_ = uint8_t(arg & 0x3f);
                pa_ = uint8_t(((opcode & 7) << 2) | (arg >> 6));
            }
            st_ = 1;
            break;
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
        case 0x78:
        case 0x79:
        case 0x7a:
        case 0x7b:
        case 0x7c:
        case 0x7d:
        case 0x7e:
        case 0x7f:
            arg = uint8_t((opcode & 0x0f) + a_);
            update_st_c(arg);
            cf_ = uint8_t(st_ ^ 1);
            a_ = uint8_t(arg & 0x0f);
            update_zf(a_);
            break;
        case 0x80:
        case 0x81:
        case 0x82:
        case 0x83:
        case 0x84:
        case 0x85:
        case 0x86:
        case 0x87:
        case 0x88:
        case 0x89:
        case 0x8a:
        case 0x8b:
        case 0x8c:
        case 0x8d:
        case 0x8e:
        case 0x8f:
            y_ = uint8_t(opcode & 0x0f);
            update_zf(y_);
            st_ = 1;
            break;
        case 0x90:
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
        case 0x96:
        case 0x97:
        case 0x98:
        case 0x99:
        case 0x9a:
        case 0x9b:
        case 0x9c:
        case 0x9d:
        case 0x9e:
        case 0x9f:
            a_ = uint8_t(opcode & 0x0f);
            update_zf(a_);
            st_ = 1;
            break;
        case 0xa0:
        case 0xa1:
        case 0xa2:
        case 0xa3:
        case 0xa4:
        case 0xa5:
        case 0xa6:
        case 0xa7:
        case 0xa8:
        case 0xa9:
        case 0xaa:
        case 0xab:
        case 0xac:
        case 0xad:
        case 0xae:
        case 0xaf:
            arg = uint8_t((opcode & 0x0f) - y_);
            update_cf(arg);
            arg &= 0x0f;
            update_st_z(arg);
            zf_ = uint8_t(st_ ^ 1);
            break;
        case 0xb0:
        case 0xb1:
        case 0xb2:
        case 0xb3:
        case 0xb4:
        case 0xb5:
        case 0xb6:
        case 0xb7:
        case 0xb8:
        case 0xb9:
        case 0xba:
        case 0xbb:
        case 0xbc:
        case 0xbd:
        case 0xbe:
        case 0xbf:
            arg = uint8_t((opcode & 0x0f) - a_);
            update_cf(arg);
            arg &= 0x0f;
            update_st_z(arg);
            zf_ = uint8_t(st_ ^ 1);
            break;
        default:
            if (st_) pc_ = uint8_t(opcode & 0x3f);
            st_ = 1;
            break;
    }

    icount_ -= oc;
    update_pio(oc);
}

}  // namespace dsp
