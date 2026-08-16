#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// 93C46 serial EEPROM, 8-bit organisation (128 x 8), ported from eepromser.pas.
class Eeprom93C46 {
public:
    Eeprom93C46();

    void reset();

    uint8_t do_read() const;
    void cs_write(uint8_t state);
    void clk_write(uint8_t state);
    void di_write(uint8_t state);

    std::array<uint8_t, 0x80>& data() { return data_; }
    const std::array<uint8_t, 0x80>& data() const { return data_; }

private:
    enum class State {
        InReset,
        WaitForStartBit,
        WaitForCommand,
        ReadingData,
        WaitForData,
        WaitForCompletion,
    };
    enum class Command {
        Invalid,
        Read,
        Write,
        Erase,
        Lock,
        Unlock,
        WriteAll,
        EraseAll,
    };

    void handle_event(uint8_t event);
    void execute_command();
    void execute_write_command();
    void parse_command_and_address();
    void write_cell(uint16_t address, uint16_t value);
    uint16_t read_cell(uint16_t address) const;

    static constexpr uint8_t kCsRising = 1 << 0;
    static constexpr uint8_t kCsFalling = 1 << 1;
    static constexpr uint8_t kClkRising = 1 << 2;
    static constexpr uint8_t kClkFalling = 1 << 3;
    static constexpr int kCommandAddressBits = 7;
    static constexpr int kAddressBits = 7;
    static constexpr int kDataBits = 8;

    State state_ = State::InReset;
    Command command_ = Command::Invalid;
    uint8_t cs_state_ = 0;
    uint8_t clk_state_ = 0;
    uint8_t di_state_ = 0;
    uint8_t bits_accum_ = 0;
    uint32_t shift_register_ = 0;
    uint32_t command_address_accum_ = 0;
    uint16_t address_ = 0;
    bool locked_ = true;
    std::array<uint8_t, 0x80> data_{};
};

}  // namespace dsp
