// PlayStation SPU (ADPCM voices + mix).
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace dsp {

class PsxSpu {
public:
    static constexpr int kRamSize = 512 * 1024;
    static constexpr int kSampleRate = 44100;
    static constexpr int kVoiceCount = 24;
    static constexpr int kCyclesPerSample = 0x300;  // 33868800 / 44100

    void reset();

    uint16_t read16(uint32_t addr) const;
    void write16(uint32_t addr, uint16_t value);

    uint32_t load32(uint32_t addr) const;
    void write32(uint32_t addr, uint32_t value);

    void dma_write(const uint32_t* data, int words);
    void dma_read(uint32_t* out, int words);

    // CD-DA / XA stereo samples (interleaved L,R). Always queued; mixed when enabled.
    void push_cd_samples(const int16_t* samples, int count);

    // Advance by CPU cycles; returns true if SPU IRQ should fire.
    bool tick(int cycles);

    // Append mono samples generated since last drain (pad with silence to sample_count).
    void drain_samples(std::vector<int16_t>& out, int sample_count);

    uint8_t* ram() { return ram_.data(); }
    const uint8_t* ram() const { return ram_.data(); }

private:
    enum class Phase : uint8_t { Attack, Decay, Sustain, Release, Off };

    struct Volume {
        uint16_t register_ = 0;
        bool is_sweep_mode() const { return ((register_ >> 15) & 1) != 0; }
        int16_t fixed_volume() const { return int16_t(register_ << 1); }
    };

    struct Adsr {
        uint16_t lo = 0;
        uint16_t hi = 0;
        bool attack_exp() const { return ((lo >> 15) & 1) != 0; }
        int attack_shift() const { return (lo >> 10) & 0x1F; }
        int attack_step() const { return (lo >> 8) & 0x3; }
        int decay_shift() const { return (lo >> 4) & 0xF; }
        int sustain_level() const { return lo & 0xF; }
        bool sustain_exp() const { return ((hi >> 15) & 1) != 0; }
        bool sustain_decrease() const { return ((hi >> 14) & 1) != 0; }
        int sustain_shift() const { return (hi >> 8) & 0x1F; }
        int sustain_step() const { return (hi >> 6) & 0x3; }
        bool release_exp() const { return ((hi >> 5) & 1) != 0; }
        int release_shift() const { return hi & 0x1F; }
    };

    struct Voice {
        Volume volume_left;
        Volume volume_right;
        uint16_t pitch = 0;
        uint16_t start_address = 0;
        uint16_t current_address = 0;
        Adsr adsr;
        uint16_t adsr_volume = 0;
        uint16_t adpcm_repeat_address = 0;
        uint32_t counter = 0;
        Phase adsr_phase = Phase::Off;
        int16_t old = 0;
        int16_t older = 0;
        int16_t latest = 0;
        bool has_samples = false;
        bool read_ram_irq = false;
        int adsr_counter = 0;
        uint8_t spu_adpcm[16]{};
        int16_t decoded_samples[31]{};

        uint32_t sample_index() const { return (counter >> 12) & 0x1F; }
        void set_sample_index(uint32_t v) {
            counter = (counter & 0xFFFu) | (v << 12);
        }
        uint32_t interpolation_index() const { return (counter >> 3) & 0xFF; }

        void key_on();
        void key_off();
        void decode_samples(uint8_t* ram, uint16_t ram_irq_address);
        int16_t process_volume(const Volume& vol) const;
        void tick_adsr();
    };

    int16_t sample_voice(int v);
    void tick_noise_generator();
    bool handle_capture_buffer(int address, int16_t sample);
    void write_ram16(uint32_t addr, int16_t value);
    int16_t load_ram16(uint32_t addr) const;
    void write_reverb(uint32_t addr, int16_t value);
    int16_t load_reverb(uint32_t addr) const;
    std::pair<int16_t, int16_t> process_reverb(int l_input, int r_input);
    static int16_t saturate(int sample);

    bool generate_sample();  // one stereo pair → mono queue

    std::array<uint8_t, kRamSize> ram_{};
    std::array<Voice, kVoiceCount> voices_{};

    int16_t main_volume_left_ = 0;
    int16_t main_volume_right_ = 0;
    int16_t reverb_out_left_ = 0;
    int16_t reverb_out_right_ = 0;

    uint32_t key_on_ = 0;
    uint32_t key_off_ = 0;
    uint32_t pitch_mod_ = 0;
    uint32_t noise_mode_ = 0;
    uint32_t reverb_mode_ = 0;
    uint32_t endx_ = 0;

    uint16_t unknown_a0_ = 0;
    uint32_t reverb_start_ = 0;
    uint32_t reverb_internal_ = 0;
    uint16_t ram_irq_address_ = 0;
    uint16_t transfer_addr_reg_ = 0;
    uint32_t transfer_addr_ = 0;
    uint16_t transfer_fifo_ = 0;
    uint16_t transfer_control_ = 0;
    uint16_t control_ = 0;
    uint16_t status_ = 0;

    uint16_t cd_volume_left_ = 0;
    uint16_t cd_volume_right_ = 0;
    uint16_t extern_volume_left_ = 0;
    uint16_t extern_volume_right_ = 0;
    uint16_t current_volume_left_ = 0;
    uint16_t current_volume_right_ = 0;
    uint32_t unknown_bc_ = 0;

    // Reverb config
    uint32_t d_apf1_ = 0, d_apf2_ = 0;
    int16_t v_iir_ = 0, v_comb1_ = 0, v_comb2_ = 0, v_comb3_ = 0, v_comb4_ = 0;
    int16_t v_wall_ = 0, v_apf1_ = 0, v_apf2_ = 0;
    uint32_t m_lsame_ = 0, m_rsame_ = 0, m_lcomb1_ = 0, m_rcomb1_ = 0;
    uint32_t m_lcomb2_ = 0, m_rcomb2_ = 0, d_lsame_ = 0, d_rsame_ = 0;
    uint32_t m_ldiff_ = 0, m_rdiff_ = 0, m_lcomb3_ = 0, m_rcomb3_ = 0;
    uint32_t m_lcomb4_ = 0, m_rcomb4_ = 0, d_ldiff_ = 0, d_rdiff_ = 0;
    uint32_t m_lapf1_ = 0, m_rapf1_ = 0, m_lapf2_ = 0, m_rapf2_ = 0;
    int16_t v_lin_ = 0, v_rin_ = 0;

    int capture_buffer_pos_ = 0;
    int sample_counter_ = 0;
    int reverb_counter_ = 0;
    int noise_timer_ = 0;
    int noise_level_ = 0;

    std::deque<int16_t> cd_queue_;
    std::deque<int16_t> output_;  // mono

    bool spu_enabled() const { return ((control_ >> 15) & 1) != 0; }
    bool spu_unmuted() const { return ((control_ >> 14) & 1) != 0; }
    bool irq9_enabled() const { return ((control_ >> 6) & 1) != 0; }
    bool reverb_master() const { return ((control_ >> 7) & 1) != 0; }
    bool cd_audio_enabled() const { return (control_ & 1) != 0; }
    bool cd_audio_reverb() const { return ((control_ >> 2) & 1) != 0; }
    int noise_freq_shift() const { return (control_ >> 10) & 0xF; }
    int noise_freq_step() const { return (control_ >> 8) & 0x3; }
};

}  // namespace dsp
