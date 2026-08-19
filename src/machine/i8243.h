#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace dsp {

constexpr int MCS48_EXPANDER_OP_READ = 0;
constexpr int MCS48_EXPANDER_OP_WRITE = 1;
constexpr int MCS48_EXPANDER_OP_OR = 2;
constexpr int MCS48_EXPANDER_OP_AND = 3;

class I8243 {
public:
    using ReadHandler = std::function<uint8_t(int)>;
    using WriteHandler = std::function<void(int, uint8_t)>;

    I8243() { reset(); }

    void reset();
    void change_calls(ReadHandler read, WriteHandler write);
    void set_read_handler(ReadHandler handler) { read_ = std::move(handler); }
    void set_write_handler(WriteHandler handler) { write_ = std::move(handler); }

    uint8_t p2_r() const { return p2out_; }
    void p2_w(uint8_t value);
    void prog_w(uint8_t value);

private:
    uint8_t p2_ = 0x0f;
    uint8_t p2out_ = 0x0f;
    uint8_t prog_ = 1;
    uint8_t opcode_ = 0;
    std::array<uint8_t, 4> p_{};
    ReadHandler read_;
    WriteHandler write_;
};

}  // namespace dsp
