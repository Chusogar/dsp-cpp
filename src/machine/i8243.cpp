#include "machine/i8243.h"

namespace dsp {

void I8243::reset() {
    p2_ = 0x0f;
    p2out_ = 0x0f;
    prog_ = 1;
    opcode_ = 0;
    p_.fill(0);
}

void I8243::change_calls(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void I8243::p2_w(uint8_t value) {
    p2_ = uint8_t(value & 0x0f);
}

void I8243::prog_w(uint8_t value) {
    value = uint8_t(value & 1);
    if (prog_ != 0 && value == 0) {
        opcode_ = p2_;
        if ((opcode_ >> 2) == MCS48_EXPANDER_OP_READ) {
            const int port = opcode_ & 3;
            if (read_) p_[size_t(port)] = read_(port);
            p2out_ = uint8_t(p_[size_t(port)] & 0x0f);
        }
    } else if (prog_ == 0 && value != 0) {
        const int port = opcode_ & 3;
        switch (opcode_ >> 2) {
            case MCS48_EXPANDER_OP_READ:
                break;
            case MCS48_EXPANDER_OP_WRITE:
                p_[size_t(port)] = uint8_t(p2_ & 0x0f);
                if (write_) write_(port, p_[size_t(port)]);
                break;
            case MCS48_EXPANDER_OP_OR:
                p_[size_t(port)] = uint8_t(p_[size_t(port)] | (p2_ & 0x0f));
                if (write_) write_(port, p_[size_t(port)]);
                break;
            case MCS48_EXPANDER_OP_AND:
                p_[size_t(port)] = uint8_t(p_[size_t(port)] & (p2_ & 0x0f));
                if (write_) write_(port, p_[size_t(port)]);
                break;
            default:
                break;
        }
    }
    prog_ = value;
}

}  // namespace dsp
