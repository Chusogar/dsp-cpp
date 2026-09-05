#pragma once

#include <cstdint>
#include <string>

#include "machine/mac_dsk.h"

namespace dsp {

// Apple IWM plus the Sony 800K 3.5" drive it talks to on a Mac Plus.
class Iwm {
public:
    void reset();
    void tick(int cycles);
    void set_hdsel(bool side);

    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t data);

    bool load_file(const std::string& path, std::string* error);
    bool loaded() const { return disk_.loaded(); }
    int track() const { return track_; }
    int side() const { return hdsel_ ? 1 : 0; }
    bool motor_on() const { return drive_motor_; }
    uint8_t mode() const { return mode_; }
    MacDsk& disk() { return disk_; }
    const MacDsk& disk() const { return disk_; }

private:
    uint8_t access(uint8_t offset, uint8_t data, bool is_write);
    uint8_t sense() const;
    void strobe_command();
    uint8_t next_nibble();
    void select_track();

    MacDsk disk_;
    uint8_t phases_ = 0;
    uint8_t control_ = 0;
    uint8_t mode_ = 0;
    uint8_t status_ = 0;
    uint8_t data_ = 0;
    uint8_t handshake_ = 0xbf;
    bool hdsel_ = false;
    bool last_strobe_ = false;
    bool dir_out_ = true;  // true: step toward higher tracks
    bool drive_motor_ = false;
    bool stepping_ = false;
    int track_ = 0;
    int nibble_pos_ = 0;
    int64_t cycles_ = 0;
    int64_t tach_period_ = 10000;
};

}  // namespace dsp
