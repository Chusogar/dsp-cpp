// PlayStation MDEC (MJPEG/macroblock decoder).
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

namespace dsp {

class PsxMdec {
public:
    PsxMdec();
    void reset();

    void write(uint32_t addr, uint32_t value);
    uint32_t read_data();
    uint32_t read_status() const;

    // DMA0: words from RAM into command/parameter FIFO.
    void dma_write(const uint32_t* data, int words);
    // DMA1: decoded pixels from output FIFO into RAM.
    void dma_read(uint32_t* dest, int words);

private:
    static constexpr int kNumBlocks = 6;
    static constexpr int kMacroBlockDecodedBytes = 256 * 3;

    enum class CommandKind { None, DecodeMacroBlocks, SetQuantTable, SetScaleTable };

    void write_command(uint32_t value);
    void write_control(uint32_t value);
    void decode_command(uint32_t value);
    void run_command();
    void decode_macro_blocks();
    void set_quant_table();
    void set_scale_table();
    void yuv_to_rgb(const int16_t* y_blk, int xx, int yy);
    bool rl_decode_block(int16_t* blk, const uint8_t* qt);
    void idct_core(int16_t* src);
    static int signed10bit(int n);
    uint16_t convert24to15(uint8_t r, uint8_t g, uint8_t b) const;
    void handle_data_out_end();

    bool data_in_fifo_full_ = false;
    bool data_out_fifo_empty_ = true;
    bool command_busy_ = false;
    bool data_in_requested_ = false;
    bool data_out_requested_ = false;
    uint32_t data_output_depth_ = 0;
    bool is_signed_ = false;
    uint32_t bit15_ = 0;
    uint32_t current_block_ = 0;
    uint32_t remaining_data_words_ = 0;
    bool is_colored_ = false;

    uint8_t luminance_qt_[64]{};
    uint8_t color_qt_[64]{};
    int16_t scale_table_[64]{};

    CommandKind command_ = CommandKind::None;

    int16_t block_[kNumBlocks][64]{};
    int16_t idct_dst_[64]{};

    std::deque<uint16_t> in_buffer_;
    std::vector<uint8_t> out_buffer_;
    int out_buffer_pos_ = 0;
    int pending_bytes_ = 0;
    int yuv_block_pos_ = 0;

    int block_pointer_ = 64;
    int q_scale_ = 0;
    int val_ = 0;
    uint16_t n_ = 0;

    static const uint8_t kZagzig[64];
};

}  // namespace dsp
