#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "sound/ay8910.h"
#include "sound/fmopn.h"

namespace dsp {

// Yamaha YM2610 (OPNB): four FM channels, an AY-3-8910 compatible SSG,
// six ADPCM-A channels and one ADPCM-B (delta-T) channel. Used by the
// NeoGeo MVS / AES. Built on OpnCore plus the existing AY8910.
class YM2610 {
public:
    using IrqHandler = std::function<void(bool)>;

    static constexpr int kSampleRate = 44100;

    explicit YM2610(uint32_t clock, float amplitude = 1.0f);

    YM2610(const YM2610&) = delete;
    YM2610& operator=(const YM2610&) = delete;

    void set_irq_handler(IrqHandler handler) { opn_.set_irq_handler(std::move(handler)); }

    void set_adpcm_a_rom(std::vector<uint8_t> rom) { adpcm_a_rom_ = std::move(rom); }
    void set_adpcm_b_rom(std::vector<uint8_t> rom) { adpcm_b_rom_ = std::move(rom); }

    void reset();

    // Four-port interface: 0/1 = part 0 address/data, 2/3 = part 1 address/data.
    void write(int port, uint8_t value);
    uint8_t read(int port);

    uint8_t status() const { return uint8_t(opn_.status() | adpcm_b_status_); }

    // Next mixed sample at kSampleRate (mono, signed 16-bit range).
    int32_t update();

private:
    struct AdpcmA {
        bool playing = false;
        uint32_t start = 0;
        uint32_t end = 0;
        uint32_t pos = 0;
        int signal = 0;
        int step = 0;
        int nibble = 0;
        uint8_t pan_level = 0xdf;  // bits 7-6 pan, 4-0 level
        uint8_t start_hi = 0, start_lo = 0, end_hi = 0, end_lo = 0;
    };

    void write_part0(uint8_t address, uint8_t value);
    void write_part1(uint8_t address, uint8_t value);
    void adpcm_a_key(uint8_t value);
    void clock_adpcm_a();
    void clock_adpcm_b();
    int decode_nibble(int& signal, int& step, uint8_t nibble);

    OpnCore opn_;
    AY8910 ay_;
    uint8_t address_[2] = {};
    float amplitude_;
    uint32_t clock_;

    std::vector<uint8_t> adpcm_a_rom_;
    std::vector<uint8_t> adpcm_b_rom_;
    std::array<AdpcmA, 6> adpcm_a_{};
    uint8_t adpcm_a_tl_ = 0x3f;
    uint8_t adpcm_a_flags_ = 0;
    double adpcm_a_acc_ = 0.0;
    double adpcm_a_step_ = 0.0;
    int32_t adpcm_a_mix_ = 0;

    bool adpcm_b_playing_ = false;
    bool adpcm_b_repeat_ = false;
    uint32_t adpcm_b_start_ = 0;
    uint32_t adpcm_b_end_ = 0;
    uint32_t adpcm_b_pos_ = 0;
    uint16_t adpcm_b_delta_ = 0;
    uint8_t adpcm_b_volume_ = 0;
    uint8_t adpcm_b_pan_ = 0xc0;
    uint8_t adpcm_b_start_hi_ = 0, adpcm_b_start_lo_ = 0;
    uint8_t adpcm_b_end_hi_ = 0, adpcm_b_end_lo_ = 0;
    uint8_t adpcm_b_delta_hi_ = 0, adpcm_b_delta_lo_ = 0;
    int adpcm_b_signal_ = 0;
    int adpcm_b_step_ = 0;
    int adpcm_b_nibble_ = 0;
    uint8_t adpcm_b_status_ = 0;
    double adpcm_b_acc_ = 0.0;
    int32_t adpcm_b_mix_ = 0;
};

}  // namespace dsp
