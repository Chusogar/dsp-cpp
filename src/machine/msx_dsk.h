#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Raw MSX floppy image (FAT12): 512-byte sectors, typically 720 KiB DSDD
// (80 tracks × 2 sides × 9 sectors) or 360 KiB. Also accepts a CPC-style
// "MV - CPC" / "EXTENDED" DSK by flattening it into the same geometry.
class MsxDisk {
public:
    static constexpr int kSectorSize = 512;

    bool load_file(const std::string& path, std::string* error);
    bool load_bytes(const uint8_t* data, size_t size, std::string* error);
    bool present() const { return !image_.empty(); }
    void eject();

    int tracks() const { return tracks_; }
    int heads() const { return heads_; }
    int sectors_per_track() const { return spt_; }

    const uint8_t* sector(int track, int head, int sector /* 1-based */) const;
    uint8_t* sector(int track, int head, int sector);

private:
    bool load_raw(const uint8_t* data, size_t size, std::string* error);
    size_t offset(int track, int head, int sector) const;

    std::vector<uint8_t> image_;
    int tracks_ = 0;
    int heads_ = 0;
    int spt_ = 9;
};

// WD2793-compatible FDC used by the standard MSX disk ROM (type 1 at $7FF8
// and type 2 at $7FB8). Instant DRQ, 512-byte sectors.
class MsxFdc {
public:
    void reset();
    void set_disk(MsxDisk* disk) { disk_ = disk; }

    uint8_t status_r();
    uint8_t track_r() const { return track_; }
    uint8_t sector_r() const { return sector_; }
    uint8_t data_r();

    void command_w(uint8_t value);
    void track_w(uint8_t value) { track_ = value; }
    void sector_w(uint8_t value) { sector_ = value; }
    void data_w(uint8_t value);

    void set_side(int side) { side_ = side & 1; }
    void set_drive(int drive) { drive_ = drive & 3; }
    void set_motor(bool on) { motor_ = on; }

    bool drq() const { return drq_; }
    bool intrq() const { return intrq_; }

    uint8_t read_reg(int index);
    void write_reg(int index, uint8_t value);

private:
    void finish_type1();
    void complete_io(bool rnf);
    void start_read_sector();
    void start_write_sector();
    void start_read_address();
    const uint8_t* current_sector() const;
    uint8_t* current_sector();

    MsxDisk* disk_ = nullptr;
    uint8_t status_ = 0;
    uint8_t track_ = 0;
    uint8_t sector_ = 1;
    uint8_t data_ = 0;
    int side_ = 0;
    int drive_ = 0;
    bool motor_ = false;
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
