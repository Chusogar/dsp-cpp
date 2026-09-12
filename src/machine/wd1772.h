#pragma once

#include <cstdint>
#include <vector>

#include "machine/sam_disk.h"

namespace dsp {

// VLSI VL-1772-02 (WD1772-compatible) floppy disk controller, as used by
// the SAM Coupe's internal and external disk interfaces. Implements the
// command subset real SAM software actually relies on: RESTORE, SEEK,
// STEP/STEP-IN/STEP-OUT, READ SECTOR (single/multiple), WRITE SECTOR,
// READ ADDRESS and FORCE INTERRUPT. Track/sector data comes from a
// SamDisk (SDF or MGT image); with none attached, commands complete with
// a "record not found" error, matching an empty drive.
class Wd1772 {
public:
    void reset();
    void set_disk(SamDisk* disk) { disk_ = disk; }
    void set_side(int side) { side_ = side & 1; }

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
    size_t debug_buf_pos() const { return buf_pos_; }
    size_t debug_buf_size() const { return buf_.size(); }
    // Advances the simulated disk-rotation index pulse once per video
    // frame; loaders that just poll for the index line to change state at
    // all (rather than needing exact ~5Hz real-disk timing) see it toggle
    // reliably every frame.
    void tick_frame() { index_state_ = !index_state_; }

private:
    void finish(uint8_t extra_status = 0);
    void start_read_sector();
    void start_write_sector();
    void start_read_address();
    const SamSector* find_sector(bool by_id) const;

    SamDisk* disk_ = nullptr;
    int side_ = 0;
    int cur_cyl_ = 0;  // physical head position (may differ from track_ register meaning)

    uint8_t status_ = 0;
    uint8_t track_ = 0;
    uint8_t sector_ = 1;
    uint8_t data_ = 0;
    uint8_t command_ = 0;

    bool drq_ = false;
    bool intrq_ = false;
    bool busy_ = false;
    bool type1_ = true;
    bool multiple_ = false;
    bool writing_ = false;

    std::vector<uint8_t> buf_;
    size_t buf_pos_ = 0;
    bool index_state_ = false;
    // SAM's ROM/software relies on the FDC giving up if the CPU polls
    // STATUS repeatedly without ever reading DATA while DRQ is set --
    // confirmed against SimCoupe (the reference SAM emulator), whose
    // comments note specific commercial titles use this exact 16-poll
    // timeout as a synchronisation mechanism. Without it, software that
    // deliberately polls status a bounded number of times before giving
    // up on a byte (rather than reading it immediately) hangs forever
    // instead of seeing the LOST_DATA error it expects.
    int status_reads_with_data_ = 0;
    static constexpr int kMaxStatusPollsWithData = 16;
};

}  // namespace dsp
