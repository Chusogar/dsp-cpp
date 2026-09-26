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

    // Port A strobed output handshake (group A modes 1 and 2), after MAME
    // i8255_device.  Opt-in: when enabled, a write to port A in mode 1/2
    // output drives /OBF (PC7) low and INTR (PC3) low, and ack_a() (the /ACK
    // strobe on PC6) sets /OBF high again.  The port C write handler then
    // sees the composed pin levels.  Sega's Hang-On and System 16A boards
    // route /OBF to the sound Z80's NMI.
    void set_handshake(bool enabled) { handshake_ = enabled; }
    void ack_a();
    bool obf_a() const { return obf_a_; }

private:
    int group_a_mode() const { return (control_ & 0x40) ? 2 : ((control_ >> 5) & 1); }
    bool port_a_strobed_output() const {
        return handshake_ && (group_a_mode() == 2 || (group_a_mode() == 1 && !(control_ & 0x10)));
    }
    uint8_t port_c_output() const;
    void update_port_c();

    bool handshake_ = false;
    bool obf_a_ = false;   // output buffer full (pin /OBF low)
    bool intr_a_ = false;
    bool inte_a1_ = false; // output interrupt enable (PC6 in mode 2, PC6 in mode 1 out)

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
