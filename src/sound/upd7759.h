#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// NEC uPD7759 ADPCM speech, ported from upd7759.pas.
class Upd7759 {
public:
    using DrqHandler = std::function<void(uint8_t)>;

    static constexpr uint32_t kClock = 640000;

    explicit Upd7759(float amp = 0.9f, bool slave = true);

    void set_drq_handler(DrqHandler handler) { drq_handler_ = std::move(handler); }
    void set_rom(std::vector<uint8_t> rom) { rom_ = std::move(rom); }
    uint8_t* rom() {
        if (rom_.empty()) rom_.assign(0x20000, 0);
        return rom_.data();
    }

    void reset();
    void start_w(uint8_t data);
    void reset_w(uint8_t data);
    void port_w(uint8_t data);
    uint8_t busy_r() const { return state_ == kIdle ? 1 : 0; }

    // Advances one host audio sample and returns the current 16-bit PCM value.
    int32_t update();

private:
    static constexpr int kIdle = 0;
    static constexpr int kDropDrq = 1;
    static constexpr int kStart = 2;
    static constexpr int kFirstReq = 3;
    static constexpr int kLastSample = 4;
    static constexpr int kDummy1 = 5;
    static constexpr int kAddrMsb = 6;
    static constexpr int kAddrLsb = 7;
    static constexpr int kDummy2 = 8;
    static constexpr int kBlockHeader = 9;
    static constexpr int kNibbleCount = 10;
    static constexpr int kNibbleMsn = 11;
    static constexpr int kNibbleLsn = 12;
    static constexpr int kFracBits = 20;
    static constexpr uint32_t kFracOne = 1u << kFracBits;

    void update_adpcm(int data);
    void advance_state();
    uint8_t rom_byte(uint32_t offset) const;

    float amp_ = 0.9f;
    bool slave_ = true;
    std::vector<uint8_t> rom_;
    DrqHandler drq_handler_;

    uint32_t pos_ = 0;
    uint32_t step_ = 4 * kFracOne;
    uint8_t fifo_in_ = 0;
    uint8_t reset_pin_ = 1;
    uint8_t start_ = 1;
    uint8_t drq_ = 0;
    int state_ = kIdle;
    int clocks_left_ = 0;
    uint16_t nibbles_left_ = 0;
    uint8_t repeat_count_ = 0;
    int post_drq_state_ = kIdle;
    int post_drq_clocks_ = 0;
    uint8_t req_sample_ = 0;
    uint8_t last_sample_ = 0;
    uint8_t block_header_ = 0;
    uint8_t sample_rate_ = 0;
    uint8_t first_valid_header_ = 0;
    uint32_t offset_ = 0;
    uint32_t repeat_offset_ = 0;
    int adpcm_state_ = 0;
    uint8_t adpcm_data_ = 0;
    int sample_ = 0;
    float resample_pos_ = 0;
    float resample_inc_ = 0;
};

}  // namespace dsp
