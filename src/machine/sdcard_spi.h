#pragma once

#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace dsp {

// SD card in SPI mode backed by a raw card image (.img/.mmc/.hdf), as the
// ZX Spectrum Next and DivMMC-style interfaces see it. Implements the
// commands those drivers use: CMD0/1/8/9/10/12/13/16/17/18/24/25/55/58/59
// and ACMD41, reporting an SDHC card (block addressing). One byte goes out
// for every byte exchanged; responses come back from the next exchange on,
// so a driver that polls with 0xFF until it sees a non-0xFF byte works.
//
// Writes go to the image file (a real card keeps them) unless the card was
// opened read-only, in which case they live in memory until it is closed.
class SdCardSpi {
public:
    ~SdCardSpi();

    bool open(const std::string& path, bool read_only, std::string* error);
    void close();
    bool inserted() const { return file_.is_open(); }
    const std::string& path() const { return path_; }
    uint64_t size_bytes() const { return size_; }

    void select(bool selected);
    bool selected() const { return selected_; }
    // Clocks one byte in (MOSI) and returns the byte clocked out (MISO).
    uint8_t exchange(uint8_t mosi);

    // Raw sector access (also used by tests and loaders).
    bool read_sector(uint32_t lba, uint8_t* out);
    bool write_sector(uint32_t lba, const uint8_t* data);

    uint32_t sectors_read() const { return sectors_read_; }
    uint32_t sectors_written() const { return sectors_written_; }

private:
    enum class State { Idle, Command, WriteToken, WriteData, MultiRead };

    void execute_command();
    void queue_read_block(uint32_t lba);
    void queue_r1(uint8_t r1) { out_.push_back(r1); }
    void queue_bytes(const uint8_t* p, size_t n) { out_.insert(out_.end(), p, p + n); }
    void queue_data_block(const uint8_t* p, size_t n);
    uint8_t r1_status() const { return idle_ ? 0x01 : 0x00; }

    std::fstream file_;
    std::string path_;
    bool read_only_ = false;
    uint64_t size_ = 0;
    std::map<uint32_t, std::vector<uint8_t>> overlay_;

    bool selected_ = false;
    bool idle_ = true;
    bool app_cmd_ = false;
    State state_ = State::Idle;
    uint8_t cmd_[6] = {};
    int cmd_len_ = 0;
    std::deque<uint8_t> out_;

    uint32_t rw_lba_ = 0;
    bool multi_write_ = false;
    std::vector<uint8_t> write_buf_;
    int write_skip_ = 0;

    uint32_t sectors_read_ = 0;
    uint32_t sectors_written_ = 0;
};

}  // namespace dsp
