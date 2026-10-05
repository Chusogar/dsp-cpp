#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Sega 315-5296 I/O chip: eight bidirectional ports plus CNT outputs.
class Sega3155296 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;
    using CntWrite = std::function<void(int, bool)>;

    void set_port_read(int port, PortRead handler);
    void set_port_write(int port, PortWrite handler);
    void set_cnt_write(CntWrite handler) { cnt_write_ = std::move(handler); }

    void reset();
    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t data);

    uint8_t direction() const { return dir_; }
    uint8_t cnt() const { return cnt_; }

private:
    std::array<PortRead, 8> in_{};
    std::array<PortWrite, 8> out_{};
    CntWrite cnt_write_;
    std::array<uint8_t, 8> latch_{};
    uint8_t cnt_ = 0;
    uint8_t dir_ = 0;
};

}  // namespace dsp
