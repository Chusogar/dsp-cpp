#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "cpu/m68000.h"
#include "sound/mpeg_audio.h"
#include "sound/scsp.h"

namespace dsp {

// Sega Model 3 sound hardware:
//  * Sound board: 68000 @ 11.29 MHz, two SCSPs (master / slave) each with
//    512 KB of sound RAM, 512 KB program ROM at 0x600000 and an 8 MB banked
//    sample ROM window at 0x800000. The main CPU sends MIDI bytes to the
//    master SCSP's MIDI input; SCSP interrupts drive the 68000.
//  * Digital Sound Board 2 (DSB2): 68000 that receives the same MIDI stream
//    and drives an MPEG-1 layer II decoder playing the music from 16 MB of
//    MPEG ROM (start / end / loop addresses, stereo mode and volume
//    commands written to its MPEG FIFO).
// Output: 44.1 kHz stereo.
class Model3Sound {
public:
    Model3Sound();

    // Raw ROM images as found in the zip (byte order fixed up here).
    void load(const std::vector<uint8_t>& program, const std::vector<uint8_t>& samples,
              const std::vector<uint8_t>& dsb_program, const std::vector<uint8_t>& mpeg);
    void reset();

    void midi_write(uint8_t data);

    // Runs the boards for `samples` 44.1 kHz output samples, appending
    // interleaved stereo to `out`.
    void run(int samples, std::vector<int16_t>& out);

    // Debug.
    M68000& sound_cpu() { return cpu_; }
    M68000& dsb_cpu() { return dsb_cpu_; }
    bool mpeg_playing() const { return mpeg_playing_; }
    uint32_t mpeg_start() const { return mp_start_; }
    Scsp& scsp(int n) { return n ? scsp2_ : scsp1_; }

private:
    // Sound board 68000 bus.
    uint16_t read_word(uint32_t a);
    void write_word(uint32_t a, uint16_t v);
    uint8_t read_byte(uint32_t a);
    void write_byte(uint32_t a, uint8_t v);
    // DSB2 68000 bus.
    uint16_t dsb_read_word(uint32_t a);
    void dsb_write_word(uint32_t a, uint16_t v);
    uint8_t dsb_read_byte(uint32_t a);
    void dsb_write_byte(uint32_t a, uint8_t v);
    void mpeg_fifo_write(uint8_t data);
    void run_dsb(int cycles);
    void decode_mpeg_frame();
    void set_level(int level);

    M68000 cpu_;
    Scsp scsp1_, scsp2_;
    std::vector<uint8_t> ram1_, ram2_;  // 512 KB each, big-endian
    std::vector<uint8_t> program_;      // 512 KB, big-endian
    std::vector<uint8_t> samples_;      // 8 MB, big-endian
    uint32_t sample_bank_ = 0;
    int cycle_debt_ = 0;
    int current_level_ = 0;

    // DSB2
    M68000 dsb_cpu_;
    std::vector<uint8_t> dsb_program_;  // 128 KB, big-endian
    std::vector<uint8_t> dsb_ram_;      // 128 KB
    std::vector<uint8_t> mpeg_rom_;
    std::unique_ptr<mpeg_audio> decoder_;
    std::array<uint8_t, 256> dsb_fifo_{};
    uint8_t dsb_fifo_r_ = 0, dsb_fifo_w_ = 0;
    uint8_t cmd_latch_ = 0;
    int dsb_state_ = 0;
    uint32_t mp_start_ = 0, mp_end_ = 0;
    bool mpeg_playing_ = false;
    int mp_pos_ = 0, mp_limit_ = 0;  // bit positions
    bool loop_ = false;
    uint32_t loop_start_ = 0, loop_end_ = 0;
    uint8_t volume_[2] = {0xff, 0xff};
    int stereo_ = 0;  // 0 stereo, 1 mono left, 2 mono right
    double dsb_cycle_acc_ = 0;
    double dsb_timer_acc_ = 0;

    // Decoded MPEG audio and resampling to 44.1 kHz.
    std::vector<int16_t> pcm_;  // interleaved stereo
    size_t pcm_pos_ = 0;        // in frames
    int pcm_rate_ = 32000;
    double pcm_frac_ = 0;
    int16_t last_l_ = 0, last_r_ = 0;
};

}  // namespace dsp
