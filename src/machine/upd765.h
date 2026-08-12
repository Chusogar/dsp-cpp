#pragma once

#include <array>
#include <cstdint>

#include "machine/disk_image.h"

namespace dsp {

// NEC uPD765 / Intel 8272 FDC, ported from upd765.pas (dsp-emulator).
// Enough for AMSDOS: Specify, Sense Drive, Recalibrate, Sense Interrupt,
// Seek, Read Data / Deleted, Read ID, Read Track. Writes are rejected
// (write-protect) unless a writable image is present.
class Upd765 {
public:
    Upd765();

    void reset();
    void set_motor(uint8_t value);  // non-zero = on
    void write_data(uint8_t value);
    uint8_t read_data();
    uint8_t read_status();

    DiskImage& drive(int n) { return dsk_[n & 1]; }
    const DiskImage& drive(int n) const { return dsk_[n & 1]; }
    bool motor() const { return motor_; }

private:
    void get_drive();
    void get_res7();
    bool buscar_sector();
    bool saltar_sector() const;
    bool read_data_stop();
    void read_sector();
    void read_track();
    void seek_track(uint8_t track);
    void exec_write_command();
    uint8_t exec_read_command();
    uint8_t get_result();

    std::array<DiskImage, 2> dsk_{};
    bool motor_ = false;
    uint8_t curr_drv_ = 0;

    bool exec_cmd_phase_ = false;
    bool result_phase_ = false;
    bool seek_track_ = false;

    uint8_t status_ = 0x80;
    uint8_t st0_ = 0, st1_ = 0, st2_ = 0, st3_ = 0;

    std::array<uint8_t, 9> cmd_{};
    std::array<uint8_t, 7> result_{};
    uint16_t cmd_ptr_ = 0;
    uint16_t res_ptr_ = 0;
    uint16_t res_counter_ = 0;
    uint32_t data_ptr_ = 0;
    uint32_t data_length_ = 0;
    uint32_t data_counter_ = 0;

    uint8_t status_timeout_ = 0;
    uint8_t read_status_timeout_ = 0;

    static constexpr uint8_t kBytesInCmd[32] = {
        1, 1, 9, 3, 2, 9, 9, 2, 1, 9, 2, 1, 9, 6, 1, 3,
        1, 9, 1, 1, 1, 1, 1, 1, 1, 9, 1, 1, 1, 1, 9, 1,
    };
};

}  // namespace dsp
