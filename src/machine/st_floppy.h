#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Atari ST 3.5" floppy: raw .ST, compressed .MSA and Pasti .STX images,
// plus the WD1772 + DMA chip pair that TOS uses to read sectors.
//
// .ST/.MSA images are a flat array of sectors.  .STX images keep every
// track as it was found on the original disk: the ID fields in the order
// and at the bit position where they sit on the track, the FDC status of
// each sector (CRC error, missing data, deleted data mark), fuzzy bits and
// optional raw track images.  For those the WD1772 model follows the head
// position, compares IDs against the track/sector registers, and times
// commands against a 300 rpm rotation, which is what copy protections
// (Bob Winner's 70 overlapping sectors on track 79, for instance) check.
class StFloppy {
public:
    static constexpr int kSectorSize = 512;

    static constexpr int kDrives = 2;  // A: and B:

    void reset();
    // Inserts an image in drive A (0) or B (1).
    bool load_file(int drive, const std::string& path, std::string* error);
    bool load_file(const std::string& path, std::string* error) { return load_file(0, path, error); }
    bool loaded(int drive = 0) const { return disk(drive).loaded; }
    int tracks(int drive = 0) const { return disk(drive).tracks; }
    int sides(int drive = 0) const { return disk(drive).sides; }
    int spt(int drive = 0) const { return disk(drive).spt; }
    bool is_stx(int drive = 0) const { return disk(drive).stx; }
    // STX: number of ID fields on a track (-1 if the track is not present).
    int stx_sector_count(int track, int side, int drive = 0) const;
    int head_position(int drive = 0) const { return disk(drive).head; }

    void set_psg_port_a(uint8_t value) { psg_a_ = value; }

    uint16_t dma_status() const;
    void dma_mode_w(uint16_t value);
    uint16_t dma_mode() const { return dma_mode_; }
    void dma_data_w(uint16_t value);
    uint16_t dma_data_r();
    void dma_addr_w(int which, uint8_t value);
    uint8_t dma_addr_r(int which) const;
    uint32_t dma_address() const { return dma_addr_; }

    bool irq() const { return fdc_irq_; }

    // Advance WD1772 command delay so INTRQ rises after TOS has armed the MFP.
    void tick(int cycles);

    // Destination RAM for DMA. The driver passes a pointer into ST RAM.
    void set_ram(uint8_t* ram, uint32_t size) {
        ram_ = ram;
        ram_size_ = size;
    }

    // Sector of the image in `drive` (A by default), nullptr if absent.
    const uint8_t* sector(int track, int side, int sector, int drive = 0) const;
    uint8_t* sector(int track, int side, int sector, int drive = 0);

private:
    uint8_t fdc_status();
    void fdc_command(uint8_t cmd);
    void finish_command();
    void do_dma_read();
    void do_dma_write();
    void do_read_address();
    int selected_drive() const;
    int selected_side() const;

    // STX track model.
    struct StxSector {
        uint8_t id[4] = {};  // track, side, sector, size code
        uint16_t crc = 0;
        uint8_t status = 0;  // WD1772 status bits found when reading it
        uint32_t bit_pos = 0;
        uint16_t read_time = 0;  // microseconds, 0 = standard
        std::vector<uint8_t> data;
        std::vector<uint8_t> fuzzy;  // 1 bits are stable, 0 bits random
        int size() const { return 128 << (id[3] & 3); }
    };
    struct StxTrack {
        bool present = false;
        uint32_t bytes = 6250;  // track length in bytes (MFM size)
        std::vector<StxSector> sectors;
        std::vector<uint8_t> image;  // raw track for Read Track, if dumped
    };
    // One drive and the disk in it. The WD1772 registers are shared; the
    // head position belongs to each drive.
    struct Disk {
        std::vector<uint8_t> image;  // .ST/.MSA sectors
        int tracks = 80;
        int sides = 2;
        int spt = 9;
        bool loaded = false;
        bool stx = false;
        std::vector<StxTrack> stx_tracks;  // [track * 2 + side]
        int head = 0;  // physical head position (cylinder)
    };
    const Disk& disk(int drive) const { return drives_[(drive == 1) ? 1 : 0]; }
    Disk& disk(int drive) { return drives_[(drive == 1) ? 1 : 0]; }
    // The drive the PSG port selects; A when none is (commands then fail).
    Disk& cur() { return disk(selected_drive()); }
    // A selected drive with a disk in it, or nullptr.
    Disk* media();
    static bool decode_geometry(Disk& d, size_t bytes);
    static bool load_st(Disk& d, const uint8_t* data, size_t size, std::string* error);
    static bool load_msa(Disk& d, const uint8_t* data, size_t size, std::string* error);
    static bool load_stx(Disk& d, const uint8_t* data, size_t size, std::string* error);
    static const uint8_t* disk_sector(const Disk& d, int track, int side, int sector);
    static StxTrack* stx_track(Disk& d, int track, int side);
    uint32_t rotation_byte() const;
    int stx_next_sector(const StxTrack& t, bool match_sector, uint32_t* wait_bytes);
    void stx_read_sector(uint8_t cmd);
    void stx_write_sector(uint8_t cmd);
    void stx_read_address();
    void stx_read_track();
    void stx_finish(uint32_t cycles);
    void dma_push(const uint8_t* data, size_t n);
    uint8_t random_byte();

    uint8_t* ram_ = nullptr;
    uint32_t ram_size_ = 0;

    uint8_t psg_a_ = 0xff;
    uint16_t dma_mode_ = 0;
    uint32_t dma_addr_ = 0;
    uint8_t dma_count_ = 0;
    uint8_t fdc_track_ = 0;
    uint8_t fdc_sector_ = 1;
    uint8_t fdc_data_ = 0;
    uint8_t fdc_status_ = 0;
    bool fdc_irq_ = false;
    bool fdc_busy_ = false;
    bool motor_on_ = false;
    uint32_t motor_idle_ = 0;  // cycles since the last command ended
    int irq_delay_ = 0;
    bool dma_error_ = false;
    uint8_t last_cmd_ = 0;

    Disk drives_[kDrives];
    int step_dir_ = 1;      // last Type I step direction
    uint64_t cycles_ = 0;   // free-running clock for the disk rotation
    uint32_t dma_bytes_ = 0;  // bytes moved in the current 512-byte block
    uint32_t rng_ = 0x12345678;
};

}  // namespace dsp
