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

    void reset();
    bool load_file(const std::string& path, std::string* error);
    bool loaded() const { return loaded_; }
    int tracks() const { return tracks_; }
    int sides() const { return sides_; }
    int spt() const { return spt_; }
    bool is_stx() const { return stx_; }
    // STX: number of ID fields on a track (-1 if the track is not present).
    int stx_sector_count(int track, int side) const;
    int head_position() const { return head_; }

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

    const uint8_t* sector(int track, int side, int sector) const;
    uint8_t* sector(int track, int side, int sector);

private:
    uint8_t fdc_status();
    void fdc_command(uint8_t cmd);
    void finish_command();
    void do_dma_read();
    void do_dma_write();
    void do_read_address();
    int selected_drive() const;
    int selected_side() const;
    bool decode_geometry(size_t bytes);
    bool load_st(const uint8_t* data, size_t size, std::string* error);
    bool load_msa(const uint8_t* data, size_t size, std::string* error);
    bool load_stx(const uint8_t* data, size_t size, std::string* error);

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
    StxTrack* stx_track(int track, int side);
    uint32_t rotation_byte() const;
    int stx_next_sector(const StxTrack& t, bool match_sector, uint32_t* wait_bytes);
    void stx_read_sector(uint8_t cmd);
    void stx_write_sector(uint8_t cmd);
    void stx_read_address();
    void stx_read_track();
    void stx_finish(uint32_t cycles);
    void dma_push(const uint8_t* data, size_t n);
    uint8_t random_byte();

    std::vector<uint8_t> image_;
    int tracks_ = 80;
    int sides_ = 2;
    int spt_ = 9;
    bool loaded_ = false;

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

    bool stx_ = false;
    std::vector<StxTrack> stx_tracks_;  // [track * 2 + side]
    int head_ = 0;          // physical head position (cylinder)
    int step_dir_ = 1;      // last Type I step direction
    uint64_t cycles_ = 0;   // free-running clock for the disk rotation
    uint32_t dma_bytes_ = 0;  // bytes moved in the current 512-byte block
    uint32_t rng_ = 0x12345678;
};

}  // namespace dsp
