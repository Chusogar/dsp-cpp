#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

// TZX / CDT tape player (tap_tzx.pas). Timing base = Spectrum 3.5 MHz T-states.
// CPC callers should scale with ~0.875.
class TapeTzx {
public:
    enum class BlockType : uint8_t {
        Standard = 0x10,
        Turbo = 0x11,
        PureTone = 0x12,
        PulseSeq = 0x13,
        PureData = 0x14,
        DirectRecording = 0x15,
        Generalized = 0x19,
        Pause = 0x20,
        GroupStart = 0x21,
        GroupEnd = 0x22,
        Jump = 0x23,
        LoopStart = 0x24,
        LoopEnd = 0x25,
        Select = 0x28,
        Stop48k = 0x2a,
        SetLevel = 0x2b,
        Text = 0x30,
        Message = 0x31,
        Archive = 0x32,
        Hardware = 0x33,
        Custom = 0x35,
        Msx = 0x4b,
        Glue = 0x5a,
        Unknown = 0xff,
    };

    // Symbol definition for generalized data ($19).
    struct Symbol {
        uint8_t flags = 0;  // bits 0-1: polarity (0=xor, 1=force low, 2=force high, 3=hold)
        std::vector<uint16_t> pulses;
    };

    struct Block {
        BlockType type = BlockType::Unknown;
        uint16_t pilot = 2168;
        uint16_t sync1 = 667;
        uint16_t sync2 = 735;
        uint16_t zero = 855;
        uint16_t one = 1710;
        uint16_t pilot_pulses = 8063;
        uint8_t used_bits = 8;
        uint32_t pause_ms = 1000;
        int16_t jump = 0;
        uint16_t loop_count = 0;
        uint8_t signal_level = 0;     // $2B
        uint8_t initial_level = 0;    // 0 or 0x40 for PULS/PAUS
        uint32_t sample_tstates = 0;  // $15: T-states per bit sample
        uint32_t totp = 0;            // $19 pilot symbol count
        uint8_t npp = 0;              // max pulses per pilot symbol
        uint16_t asp = 0;             // alphabet size pilot (0→256)
        uint32_t totd = 0;            // data symbol count
        uint8_t npd = 0;              // max pulses per data symbol
        uint16_t asd = 0;             // alphabet size data (0→256)
        std::vector<uint32_t> pulses;  // $13 / PULS (may exceed 16-bit)
        std::vector<Symbol> symbols;   // $19 data alphabet
        std::vector<Symbol> pilot_symbols;
        std::vector<uint16_t> pilot_stream;  // sequence of pilot symbol indices + repeats
        std::vector<uint8_t> data;
        std::string text;
    };

    bool load_file(const std::string& path, std::string* error = nullptr);
    bool load_memory(const uint8_t* data, size_t size, std::string* error = nullptr);
    bool load_csw(const uint8_t* data, size_t size, std::string* error = nullptr);
    bool load_pzx(const uint8_t* data, size_t size, std::string* error = nullptr);
    void clear();

    void play(bool restart = true);
    void pause();
    void stop();
    bool is_playing() const { return playing_ && !paused_; }
    bool is_paused() const { return paused_; }
    bool is_loaded() const { return loaded_; }
    bool motor_enabled() const { return is_playing(); }

    int advance(int tstates);
    uint8_t level() const { return level_; }

    size_t block_count() const { return blocks_.size(); }
    size_t current_block() const { return index_; }

private:
    void next_block();
    void start_block();
    bool parse_tzx(const uint8_t* data, size_t size, std::string* error);
    bool parse_csw(const uint8_t* data, size_t size, std::string* error);
    bool parse_pzx(const uint8_t* data, size_t size, std::string* error);
    static uint8_t bits_for_alphabet(uint16_t asd);
    uint32_t read_symbol_index(const Block& b, uint32_t symbol_i) const;
    static std::vector<uint8_t> csw_rle_decode(const uint8_t* in, size_t in_len,
                                              uint8_t initial_polarity);
    void start_gen_symbol(const Symbol& sym);
    void advance_gen_data();

    static uint16_t alphabet_size(uint8_t raw) { return raw == 0 ? 256 : raw; }

    std::vector<Block> blocks_;
    bool loaded_ = false;
    bool playing_ = false;
	bool paused_ = false;

    size_t index_ = 0;
    uint8_t level_ = 0;
    int estado_ = 0;
    int estados_left_ = 0;
    uint8_t bit_mask_ = 0x80;
    uint8_t last_bit_mask_ = 1;
    size_t data_pos_ = 0;
    uint32_t pulse_count_ = 0;
    size_t pulse_index_ = 0;
    size_t loop_start_ = 0;
    uint16_t loop_remain_ = 0;

    // Generalized / symbol playback
    size_t sym_pulse_i_ = 0;
    size_t pilot_stream_i_ = 0;
    uint16_t pilot_repeat_left_ = 0;
    uint8_t current_symbol_ = 0;
};

}  // namespace dsp
