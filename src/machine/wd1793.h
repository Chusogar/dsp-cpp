#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "machine/trdos_disk.h"

namespace dsp {

// Western Digital WD1793 / KR1818VG93, enough for TR-DOS on the Beta 128.
class Wd1793 {
public:
    void reset();
    void set_disk(TrdosDisk* disk) { disk_ = disk; }
    void set_side(int side) { side_ = side & 1; }
    void set_drive(int drive) { drive_ = drive & 3; }

    uint8_t status_r();
    uint8_t track_r() const { return track_; }
    uint8_t sector_r() const { return sector_; }
    uint8_t data_r();

    void command_w(uint8_t value);
    void track_w(uint8_t value) { track_ = value; }
    void sector_w(uint8_t value) { sector_ = value; }
    void data_w(uint8_t value);

    bool drq() const { return drq_; }
    bool intrq() const { return intrq_; }

private:
    void finish_type1();
    void start_read_sector();
    void start_write_sector();
    void start_read_address();
    void complete_io(bool rnf);
    const uint8_t* current_sector() const;
    uint8_t* current_sector();

    TrdosDisk* disk_ = nullptr;
    uint8_t status_ = 0;
    uint8_t track_ = 0;
    uint8_t sector_ = 1;
    uint8_t data_ = 0;
    uint8_t command_ = 0;
    int side_ = 0;
    int drive_ = 0;
    bool drq_ = false;
    bool intrq_ = false;
    bool busy_ = false;
    bool type1_ = true;
    bool writing_ = false;
    bool index_pulse_ = false;
    std::vector<uint8_t> buf_;
    size_t buf_pos_ = 0;
};

}  // namespace dsp
