#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// uPD765/NEC765 floppy disk controller wired the Amstrad CPC way (motor and
// data/status registers only, no DMA/terminal count), ported from upd765.pas.
// Reads standard and Extended .dsk images, ported from disk_file_format.pas
// (dsk_format), including its CRC based sector-size patches for a couple of
// well known copy-protected titles. The Oric .dsk/.mfm formats and the
// Lenslock code-wheel protection (which shows a UI dialog on the source
// engine) are not ported.
//
// The ID search used by READ DATA/READ ID/READ DELETED DATA (find_sector, see
// buscar_sector in upd765.pas) is ported exactly: it only advances its
// "sector under the head" position on READ ID or an exact ID match, never on
// SEEK/RECALIBRATE, and gives up on the first non-matching id instead of
// scanning the rest of the rotation. READ TRACK does not use this search at
// all (it walks sectors in physical order), which is how most custom CPC game
// loaders read a disk; AMSDOS's own directory lookup (CAT/RUN"...") does use
// it and may need extra retries depending on where a previous access left the
// search, exactly as in the source engine.
class Nec765Fdc {
public:
    static constexpr int kDriveCount = 2;

    Nec765Fdc();

    // `path` is a .dsk or .edsk image; drive is 0 or 1 (the CPC only wires
    // drive A in practice, but the controller itself supports two).
    bool load_disk(int drive, const std::string& path, std::string* error);
    bool load_disk_from_memory(int drive, const std::vector<uint8_t>& data, std::string* error);
    void eject_disk(int drive);
    bool disk_inserted(int drive) const;

    void reset();

    void write_motor(uint8_t value);
    void write_data(uint8_t value);
    uint8_t read_status();
    uint8_t read_data();

private:
    struct SectorInfo {
        uint8_t track = 0;
        uint8_t head = 0;
        uint8_t sector = 0;
        uint8_t sector_size = 0;
        uint8_t status1 = 0;
        uint8_t status2 = 0;
        uint16_t data_length = 0;
        uint32_t position = 0;  // byte offset within the track's data buffer
        bool multi = false;     // weak/random-data sector (copy protection)
    };

    struct TrackInfo {
        uint8_t track_number = 0;
        uint8_t side_number = 0;
        uint8_t data_rate = 0;
        uint8_t recording_mode = 0;
        uint8_t sector_size = 0;
        uint8_t number_sector = 0;
        uint8_t gap3 = 0;
        uint8_t filler = 0;
        uint32_t track_length = 0;
        std::vector<uint8_t> data;
        std::array<SectorInfo, 32> sectors{};
    };

    struct Disk {
        bool open = false;
        bool write_protected = false;
        uint8_t track_actual = 0;
        uint8_t side_actual = 0;
        uint8_t sector_actual = 0;
        uint8_t sector_read_track = 0;
        uint8_t tracks_count = 0;
        uint8_t heads_count = 0;
        std::array<std::array<TrackInfo, 84>, 2> tracks;
        uint8_t multi_counter = 0;
        uint8_t multi_max = 0;
    };

    bool parse_dsk(Disk& disk, const std::vector<uint8_t>& data, std::string* error);
    void apply_protection_patches(Disk& disk, bool has_multi);

    void get_result7();
    bool find_sector();
    bool should_skip_sector() const;
    void start_read_sector();
    void start_read_track();
    void select_drive();
    bool seek_track(uint8_t track);
    void exec_write_command();
    bool read_data_should_stop();
    uint8_t exec_read_command();
    uint8_t get_result();

    std::array<Disk, kDriveCount> disks_;

    bool floppy_motor_ = false;
    uint8_t current_drive_ = 0;
    bool exec_cmd_phase_ = false;
    bool result_phase_ = false;
    uint8_t status_register_ = 0x80;
    uint8_t st0_ = 0, st1_ = 0, st2_ = 0, st3_ = 0;
    uint8_t status_counter_ = 0;
    uint8_t read_status_counter_ = 0;

    std::array<uint8_t, 9> command_{};
    std::array<uint8_t, 7> result_{};
    uint16_t command_pointer_ = 0;
    uint16_t result_pointer_ = 0;
    uint16_t result_counter_ = 0;
    uint32_t data_pointer_ = 0;
    uint32_t data_length_ = 0;
    uint32_t counter_ = 0;
    bool seek_track_flag_ = false;
};

}  // namespace dsp
