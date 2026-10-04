#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace dsp {

// Sega 315-5881 stream decryption / decompression chip, as fitted to the
// security boards of several Model 3 games. Ported from MAME's
// 315-5881_crypt.cpp (BSD-3-Clause, Andreas Naive, Olivier Galibert,
// David Haywood). The chip reads 16-bit words through `read_`, decrypts them
// with the per-game key and the sub-key written by the CPU and, for
// compressed substreams, also decompresses them.
class Sega3155881 {
public:
    explicit Sega3155881(uint32_t key = 0);

    void set_key(uint32_t k) { key = k; }
    void set_read(std::function<uint16_t(uint32_t)> read) { read_ = std::move(read); }

    void reset();
    void set_addr_low(uint16_t data);
    void set_addr_high(uint16_t data);
    void set_subkey(uint16_t data);
    uint16_t do_decrypt(uint8_t*& base);

private:
    enum {
        BUFFER_SIZE = 2, LINE_SIZE = 512,
        FLAG_COMPRESSED = 0x20000
    };

    std::function<uint16_t(uint32_t)> read_;
    uint32_t key = 0;

    std::unique_ptr<uint8_t[]> buffer;
    std::unique_ptr<uint8_t[]> line_buffer;
    std::unique_ptr<uint8_t[]> line_buffer_prev;
    uint32_t prot_cur_address = 0;
    uint16_t subkey = 0, dec_hist = 0;
    uint32_t dec_header = 0;

    bool enc_ready = false;

    int buffer_pos = 0, line_buffer_pos = 0, line_buffer_size = 0, buffer_bit = 0, buffer_bit2 = 0;
    uint8_t buffer2[2]{};
    uint16_t buffer2a = 0;

    int block_size = 0;
    int block_pos = 0;
    int block_numlines = 0;
    int done_compression = 0;

    struct sbox {
        uint8_t table[64];
        int inputs[6];   // positions of the inputs bits, -1 means no input except from key
        int outputs[2];  // positions of the output bits
    };

    static const sbox fn1_sboxes[4][4];
    static const sbox fn2_sboxes[4][4];

    static const int FN1GK = 38;
    static const int FN2GK = 32;
    static const int fn1_game_key_scheduling[FN1GK][2];
    static const int fn2_game_key_scheduling[FN2GK][2];
    static const int fn1_sequence_key_scheduling[20][2];
    static const int fn2_sequence_key_scheduling[16];
    static const int fn2_middle_result_scheduling[16];

    static const uint8_t trees[9][2][32];

    int feistel_function(int input, const struct sbox* sboxes, uint32_t subkeys);
    uint16_t block_decrypt(uint32_t game_key, uint16_t sequence_key, uint16_t counter, uint16_t data);

    uint16_t get_decrypted_16();
    int get_compressed_bit();

    void enc_start();
    void enc_fill();
    void line_fill();
};

}  // namespace dsp
