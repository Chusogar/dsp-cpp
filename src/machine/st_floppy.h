#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// Atari ST 3.5" floppy: raw .ST and compressed .MSA images, plus the
// WD1772 + DMA chip pair that TOS uses to read sectors.
class StFloppy {
public:
    static constexpr int kSectorSize = 512;

    void reset();
    bool load_file(const std::string& path, std::string* error);
    bool loaded() const { return loaded_; }
    int tracks() const { return tracks_; }
    int sides() const { return sides_; }
    int spt() const { return spt_; }

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

    // Destination RAM for DMA. The driver passes a pointer into ST RAM.
    void set_ram(uint8_t* ram, uint32_t size) {
        ram_ = ram;
        ram_size_ = size;
    }

    const uint8_t* sector(int track, int side, int sector) const;
    uint8_t* sector(int track, int side, int sector);

private:
    uint8_t fdc_status() const;
    void fdc_command(uint8_t cmd);
    void do_dma_read();
    void do_dma_write();
    bool decode_geometry(size_t bytes);
    bool load_st(const uint8_t* data, size_t size, std::string* error);
    bool load_msa(const uint8_t* data, size_t size, std::string* error);

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
    bool dma_error_ = false;
};

bool load_st_floppy_file(const std::string& path, std::vector<uint8_t>& image, int* tracks,
                         int* sides, int* spt, std::string* error);

}  // namespace dsp
