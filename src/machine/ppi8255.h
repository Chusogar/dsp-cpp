#pragma once

#include <cstdint>
#include <functional>

namespace dsp {

// Intel 8255 PPI (mode 0 sufficient for Amstrad CPC / many arcade boards).
class Ppi8255 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;

    Ppi8255() { reset(); }

    void set_port_handlers(PortRead a_r, PortRead b_r, PortRead c_r,
                           PortWrite a_w, PortWrite b_w, PortWrite c_w) {
        read_a_ = std::move(a_r);
        read_b_ = std::move(b_r);
        read_c_ = std::move(c_r);
        write_a_ = std::move(a_w);
        write_b_ = std::move(b_w);
        write_c_ = std::move(c_w);
    }

    void reset();
    uint8_t read(uint8_t port);
    void write(uint8_t port, uint8_t data);

    uint8_t latch(int port) const { return latch_[port & 3]; }

private:
    void set_mode(uint8_t data, bool call_handlers);
    uint8_t read_port(int port);
    void write_port(int port);

    uint8_t control_ = 0x9b;
    uint8_t group_a_mode_ = 0;
    uint8_t group_b_mode_ = 0;
    bool port_a_dir_ = true;   // true = input
    bool port_b_dir_ = true;
    bool port_ch_dir_ = true;
    bool port_cl_dir_ = true;

    uint8_t in_mask_[3] = {};
    uint8_t out_mask_[3] = {};
    uint8_t latch_[3] = {};
    uint8_t output_val_[3] = {};

    PortRead read_a_, read_b_, read_c_;
    PortWrite write_a_, write_b_, write_c_;
};

}  // namespace dsp
