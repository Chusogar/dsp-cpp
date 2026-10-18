#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dsp {

constexpr int kMdvSectors = 255;
constexpr int kMdvSectorBytes = 686;
constexpr int kMdvImageBytes = kMdvSectors * kMdvSectorBytes;
constexpr int kMdvPairsHeader = 14;
constexpr int kMdvPairsBlockGap = 35;
constexpr int kMdvPairsBlock = 269;
constexpr int kMdvPairsSectorGap = 60;
constexpr int kMdvPairsPerSector =
    kMdvPairsHeader + kMdvPairsBlockGap + kMdvPairsBlock + kMdvPairsSectorGap;
constexpr int kMdvTapePairs = kMdvSectors * kMdvPairsPerSector;
constexpr int kMdvBitRate = 100000;

// One Sinclair Microdrive: QLAY tape timeline + daisy-chain motor select.
//
// The tape is worked out from the 174930-byte QLAY image as it goes past the
// head (sector headers and blocks, erased gaps between them), not stored as a
// tape: the image can stay where it is (read in place on boards that map
// their files, e.g. from flash), and only the sectors the QL writes to get a
// copy of their tape (~1 KB each).
class QlMicrodrive {
public:
    QlMicrodrive();
    void reset();
    bool load_image(const std::vector<uint8_t>& qlay, std::string* error);
    bool load_file(const std::string& path, std::string* error);
    bool loaded() const { return loaded_; }

    void set_tx_pop(std::function<uint16_t()> cb) { tx_pop_cb_ = std::move(cb); }
    void clk_w(int state);
    void comms_in_w(int state) { comms_in_ = state != 0; }
    void erase_w(int state) { erase_ = state != 0; }
    void read_write_w(int state) { write_ = state != 0; }
    bool selected() const { return comms_out_; }
    bool motor() const { return motor_; }
    int comms_out() const { return comms_out_ ? 1 : 0; }
    int tape_pair() const { return pair_; }

    void tick_bit();
    int data1() const;
    int data2() const;
    int gap() const;

private:
    struct Pair {
        uint8_t left, right, erased;
    };
    Pair pair_at(int pair) const;
    Pair& written_pair(int pair);  // the sector's own copy, made on the first write

    std::function<uint16_t()> tx_pop_cb_;
    const uint8_t* image_ = nullptr;
    std::vector<uint8_t> image_owned_;
    std::vector<std::vector<Pair>> written_;  // per sector, empty until written
    bool loaded_ = false;
    bool clk_ = false;
    bool comms_in_ = false;
    bool comms_out_ = false;
    bool erase_ = false;
    bool write_ = false;
    bool motor_ = false;
    int bit_ = 0;
    int pair_ = 0;
};

// Build / parse cartridge images (QLAY .mdv and ZIP-based .qlpak).
bool load_ql_cartridge(const std::string& path, std::vector<uint8_t>& qlay, std::string* error);
bool make_ql_listing_cartridge(const std::string& medium, const std::string& filename,
                               const std::string& listing, std::vector<uint8_t>& qlay,
                               std::string* error);

}  // namespace dsp
