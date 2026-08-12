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

void I8255::reset()
{
    control_ = 0x9b;

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
            break;

        case 1:
            port_b_latch_ = value;

            if (port_b_write_) {
                port_b_write_(value);
            }
            break;

        case 2:
            port_c_latch_ = value;

            if (port_c_write_) {
                port_c_write_(value);
            }
            break;

        case 3:

            if (value & 0x80) {

                //
                // Modo I/O
                //

                control_ = value;

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

                if (port_c_write_) {
                    port_c_write_(port_c_latch_);
                }
            }
            break;
    }
}

} // namespace dsp