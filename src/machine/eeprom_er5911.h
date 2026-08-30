#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace dsp {

// ER5911 8-bit serial EEPROM (128 bytes) — state machine from dsp-emulator eepromser.pas
class EepromEr5911 {
public:
    static constexpr int kSize = 0x80;
    static constexpr int kDataBits = 8;
    static constexpr int kAddressBits = 7;
    static constexpr int kCommandAddressBits = 9;

    EepromEr5911() { reset(); }

    void reset();
    void load(const uint8_t* data, int size);

    void di_write(int state);
    void cs_write(int state);
    void clk_write(int state);
    int do_read() const;
    int ready_read() const { return 1; }

    uint8_t* data() { return mem_.data(); }

private:
    enum State {
        kInReset = 0,
        kWaitStartBit,
        kWaitCommand,
        kReadingData,
        kWaitForData,
        kWaitCompletion,
    };
    enum Command {
        kInvalid = 0,
        kRead,
        kWrite,
        kWriteAll,
        kEraseAll,
        kLock,
        kUnlock,
    };
    enum Event {
        kCsRising,
        kCsFalling,
        kClkRising,
        kClkFalling,
    };

    void handle_event(Event ev);
    void execute_command();
    void execute_write_command();
    void parse_er5911();

    std::array<uint8_t, kSize> mem_{};
    int cs_ = 0, clk_ = 0, di_ = 0;
    State state_ = kInReset;
    Command command_ = kInvalid;
    bool locked_ = true;
    int bits_accum_ = 0;
    uint32_t command_address_accum_ = 0;
    uint32_t shift_register_ = 0;
    int address_ = 0;
};

}  // namespace dsp
