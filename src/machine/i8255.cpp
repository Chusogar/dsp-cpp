#include "machine/i8255.h"

#include <utility>

namespace dsp {

void I8255::set_port_handlers(
    PortRead port_a_read,
    PortRead port_b_read,
    PortRead port_c_read,
    PortWrite port_a_write,
    PortWrite port_b_write,
    PortWrite port_c_write)
{
    port_a_read_ = std::move(port_a_read);
    port_b_read_ = std::move(port_b_read);
    port_c_read_ = std::move(port_c_read);

    port_a_write_ = std::move(port_a_write);
    port_b_write_ = std::move(port_b_write);
    port_c_write_ = std::move(port_c_write);
}

uint8_t I8255::port_c_output() const
{
    uint8_t value = port_c_latch_;
    if (port_a_strobed_output()) {
        value = uint8_t(value & ~0x88);
        if (!obf_a_) value |= 0x80;
        if (intr_a_) value |= 0x08;
    }
    return value;
}

void I8255::update_port_c()
{
    if (port_c_write_) port_c_write_(port_c_output());
}

void I8255::ack_a()
{
    if (!port_a_strobed_output() || !obf_a_) return;
    obf_a_ = false;
    intr_a_ = inte_a1_;
    update_port_c();
}

void I8255::reset()
{
    control_ = 0x9b;
    obf_a_ = false;
    intr_a_ = false;
    inte_a1_ = false;

    port_a_latch_ = 0xff;
    port_b_latch_ = 0xff;
    port_c_latch_ = 0xff;

    if (port_a_write_) port_a_write_(port_a_latch_);
    if (port_b_write_) port_b_write_(port_b_latch_);
    if (port_c_write_) port_c_write_(port_c_latch_);
}

uint8_t I8255::read(int port)
{
    switch (port & 3) {
        case 0:
            return port_a_read_
                ? port_a_read_()
                : port_a_latch_;

        case 1:
            return port_b_read_
                ? port_b_read_()
                : port_b_latch_;

        case 2:
            return port_c_read_
                ? port_c_read_()
                : port_c_latch_;

        default:
            return control_;
    }
}

void I8255::write(int port, uint8_t value)
{
    switch (port & 3) {

        case 0:
            port_a_latch_ = value;

            if (port_a_write_) {
                port_a_write_(value);
            }
            if (port_a_strobed_output()) {
                obf_a_ = true;
                intr_a_ = false;
                update_port_c();
            }
            break;

        case 1:
            port_b_latch_ = value;

            if (port_b_write_) {
                port_b_write_(value);
            }
            break;

        case 2:
            port_c_latch_ = value;
            update_port_c();
            break;

        case 3:

            if (value & 0x80) {

                //
                // Modo I/O
                //

                control_ = value;
                if (handshake_) {
                    // A mode set clears the output latches and the
                    // handshake flip-flops (/OBF inactive).
                    port_a_latch_ = port_b_latch_ = port_c_latch_ = 0;
                    obf_a_ = false;
                    intr_a_ = false;
                    inte_a1_ = false;
                    if (port_a_write_ && !(control_ & 0x10)) port_a_write_(0);
                    if (port_b_write_ && !(control_ & 0x02)) port_b_write_(0);
                    update_port_c();
                }

            } else {

                //
                // BSR (Bit Set/Reset)
                //

                const uint8_t bit = (value >> 1) & 0x07;
                const uint8_t mask = uint8_t(1u << bit);

                if (value & 1) {
                    port_c_latch_ |= mask;
                } else {
                    port_c_latch_ &= uint8_t(~mask);
                }
                if (port_a_strobed_output() && bit == 6) {
                    inte_a1_ = (value & 1) != 0;
                    intr_a_ = inte_a1_ && !obf_a_;
                }

                update_port_c();
            }
            break;
    }
}

} // namespace dsp