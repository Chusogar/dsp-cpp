#pragma once

#include <cstdint>
#include <functional>

namespace dsp {

class I8255 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;

    void set_port_handlers(
        PortRead port_a_read,
        PortRead port_b_read,
        PortRead port_c_read,
        PortWrite port_a_write,
        PortWrite port_b_write,
        PortWrite port_c_write);

    void reset();

    uint8_t read(int port);
    void write(int port, uint8_t value);

private:
    uint8_t control_ = 0x9b;

    uint8_t port_a_latch_ = 0xff;
    uint8_t port_b_latch_ = 0xff;
    uint8_t port_c_latch_ = 0xff;

    PortRead port_a_read_;
    PortRead port_b_read_;
    PortRead port_c_read_;

    PortWrite port_a_write_;
    PortWrite port_b_write_;
    PortWrite port_c_write_;
};

} // namespace dsp
