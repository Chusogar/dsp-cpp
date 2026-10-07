// PlayStation MDEC.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#include "machine/psx_mdec.h"

#include <algorithm>
#include <cstring>

namespace dsp {

const uint8_t PsxMdec::kZagzig[64] = {
    0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

PsxMdec::PsxMdec() {
    out_buffer_.assign(0x30000, 0);
    reset();
}

void PsxMdec::reset() {
    data_in_fifo_full_ = false;
    data_out_fifo_empty_ = true;
    command_busy_ = false;
    data_in_requested_ = false;
    data_out_requested_ = false;
    data_output_depth_ = 0;
    is_signed_ = false;
    bit15_ = 0;
    current_block_ = 0;
    remaining_data_words_ = 0;
    is_colored_ = false;
    command_ = CommandKind::None;
    in_buffer_.clear();
    std::fill(out_buffer_.begin(), out_buffer_.end(), 0);
    out_buffer_pos_ = 0;
    pending_bytes_ = 0;
    yuv_block_pos_ = 0;
    block_pointer_ = 64;
    q_scale_ = 0;
    val_ = 0;
    n_ = 0;
    std::memset(luminance_qt_, 0, sizeof(luminance_qt_));
    std::memset(color_qt_, 0, sizeof(color_qt_));
    std::memset(scale_table_, 0, sizeof(scale_table_));
    std::memset(block_, 0, sizeof(block_));
}

void PsxMdec::write(uint32_t addr, uint32_t value) {
    const uint32_t reg = addr & 0xF;
    if (reg == 0) write_command(value);
    else if (reg == 4) write_control(value);
}

void PsxMdec::dma_write(const uint32_t* data, int words) {
    for (int i = 0; i < words; i++) write_command(data[i]);
}

void PsxMdec::dma_read(uint32_t* dest, int words) {
    if (data_output_depth_ == 2) {
        const int size = words * 4;
        if (out_buffer_pos_ + size > int(out_buffer_.size())) {
            std::memset(dest, 0, size_t(words) * 4);
            return;
        }
        std::memcpy(dest, out_buffer_.data() + out_buffer_pos_, size_t(size));
        out_buffer_pos_ += size;
        pending_bytes_ -= size;
        handle_data_out_end();
        return;
    }
    if (data_output_depth_ == 3) {
        const int size = words * 6;
        if (out_buffer_pos_ + size > int(out_buffer_.size())) {
            std::memset(dest, 0, size_t(words) * 4);
            return;
        }
        const uint8_t* span = out_buffer_.data() + out_buffer_pos_;
        out_buffer_pos_ += size;
        pending_bytes_ -= size;
        handle_data_out_end();
        for (int i = 0; i < words; i++) {
            const int b24 = i * 6;
            const uint16_t lo = uint16_t(bit15_ << 15 |
                                         convert24to15(span[b24 + 0], span[b24 + 1], span[b24 + 2]));
            const uint16_t hi = uint16_t(bit15_ << 15 |
                                         convert24to15(span[b24 + 3], span[b24 + 4], span[b24 + 5]));
            dest[i] = (uint32_t(hi) << 16) | lo;
        }
        return;
    }
    for (int i = 0; i < words; i++) dest[i] = 0xFF00FF00u;
}

uint32_t PsxMdec::read_data() {
    if (data_output_depth_ == 2) {
        if (out_buffer_pos_ + 4 > int(out_buffer_.size())) return 0;
        uint32_t v = 0;
        std::memcpy(&v, out_buffer_.data() + out_buffer_pos_, 4);
        out_buffer_pos_ += 4;
        pending_bytes_ -= 4;
        handle_data_out_end();
        return v;
    }
    if (data_output_depth_ == 3) {
        if (out_buffer_pos_ + 6 > int(out_buffer_.size())) return 0;
        const uint8_t* span = out_buffer_.data() + out_buffer_pos_;
        out_buffer_pos_ += 6;
        pending_bytes_ -= 6;
        handle_data_out_end();
        const uint16_t lo = uint16_t(bit15_ << 15 | convert24to15(span[0], span[1], span[2]));
        const uint16_t hi = uint16_t(bit15_ << 15 | convert24to15(span[3], span[4], span[5]));
        return (uint32_t(hi) << 16) | lo;
    }
    return 0x00FF00FFu;
}

uint32_t PsxMdec::read_status() const {
    uint32_t status = 0;
    status |= (data_out_fifo_empty_ ? 1u : 0u) << 31;
    status |= (data_in_fifo_full_ ? 1u : 0u) << 30;
    status |= (command_busy_ ? 1u : 0u) << 29;
    status |= (data_in_requested_ ? 1u : 0u) << 28;
    status |= ((data_out_requested_ && pending_bytes_ != 0) ? 1u : 0u) << 27;
    status |= (data_output_depth_ & 3u) << 25;
    status |= (is_signed_ ? 1u : 0u) << 24;
    status |= (bit15_ & 1u) << 23;
    status |= ((current_block_ + 4) % kNumBlocks) << 16;
    status |= uint16_t(remaining_data_words_ - 1);
    return status;
}

void PsxMdec::write_command(uint32_t value) {
    command_busy_ = true;
    if (remaining_data_words_ == 0) {
        decode_command(value);
    } else {
        in_buffer_.push_back(uint16_t(value));
        in_buffer_.push_back(uint16_t(value >> 16));
        remaining_data_words_--;
        if (command_ == CommandKind::DecodeMacroBlocks) {
            decode_macro_blocks();
        }
    }
    if (remaining_data_words_ == 0) {
        if (command_ != CommandKind::DecodeMacroBlocks) {
            run_command();
            command_busy_ = false;
        }
    }
}

void PsxMdec::write_control(uint32_t value) {
    if (((value >> 31) & 1u) != 0) {
        out_buffer_pos_ = 0;
        current_block_ = 0;
        remaining_data_words_ = 0;
        pending_bytes_ = 0;
        yuv_block_pos_ = 0;
        in_buffer_.clear();
        block_pointer_ = 64;
        q_scale_ = 0;
        val_ = 0;
        n_ = 0;
        command_ = CommandKind::None;
    }
    data_in_requested_ = ((value >> 30) & 1u) != 0;
    data_out_requested_ = ((value >> 29) & 1u) != 0;
}

void PsxMdec::decode_command(uint32_t value) {
    const uint32_t raw = value >> 29;
    data_output_depth_ = (value >> 27) & 3u;
    is_signed_ = ((value >> 26) & 1u) != 0;
    bit15_ = (value >> 25) & 1u;
    remaining_data_words_ = value & 0xFFFFu;
    is_colored_ = (value & 1u) != 0;
    switch (raw) {
        case 1:
            command_ = CommandKind::DecodeMacroBlocks;
            break;
        case 2:
            command_ = CommandKind::SetQuantTable;
            remaining_data_words_ = 16 + (is_colored_ ? 16u : 0u);
            break;
        case 3:
            command_ = CommandKind::SetScaleTable;
            remaining_data_words_ = 32;
            break;
        default:
            command_ = CommandKind::None;
            break;
    }
}

void PsxMdec::run_command() {
    switch (command_) {
        case CommandKind::SetQuantTable: set_quant_table(); break;
        case CommandKind::SetScaleTable: set_scale_table(); break;
        default: break;
    }
}

void PsxMdec::decode_macro_blocks() {
    while (!in_buffer_.empty()) {
        for (; current_block_ < kNumBlocks; current_block_++) {
            const uint8_t* qt = current_block_ >= 2 ? luminance_qt_ : color_qt_;
            if (!rl_decode_block(block_[current_block_], qt)) return;
            idct_core(block_[current_block_]);
        }
        current_block_ = 0;
        block_pointer_ = 64;
        q_scale_ = 0;
        val_ = 0;
        n_ = 0;
        yuv_to_rgb(block_[2], 0, 0);
        yuv_to_rgb(block_[3], 8, 0);
        yuv_to_rgb(block_[4], 0, 8);
        yuv_to_rgb(block_[5], 8, 8);
        data_out_fifo_empty_ = false;
        yuv_block_pos_ += kMacroBlockDecodedBytes;
        pending_bytes_ += kMacroBlockDecodedBytes;
        if (yuv_block_pos_ + kMacroBlockDecodedBytes > int(out_buffer_.size())) {
            // Wrap output buffer if a long stream overruns the scratch area.
            yuv_block_pos_ = 0;
            out_buffer_pos_ = 0;
        }
    }
}

void PsxMdec::yuv_to_rgb(const int16_t* y_blk, int xx, int yy) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int R = block_[0][((x + xx) / 2) + ((y + yy) / 2) * 8];
            int B = block_[1][((x + xx) / 2) + ((y + yy) / 2) * 8];
            int G = int((-0.3437 * B) + (-0.7143 * R));
            R = int(1.402 * R);
            B = int(1.772 * B);
            const int Y = y_blk[x + y * 8];
            R = std::min(std::max(Y + R, -128), 127);
            G = std::min(std::max(Y + G, -128), 127);
            B = std::min(std::max(Y + B, -128), 127);
            R ^= 0x80;
            G ^= 0x80;
            B ^= 0x80;
            const int position = ((x + xx + ((y + yy) * 16)) * 3) + yuv_block_pos_;
            if (position + 2 < int(out_buffer_.size())) {
                out_buffer_[size_t(position)] = uint8_t(R);
                out_buffer_[size_t(position + 1)] = uint8_t(G);
                out_buffer_[size_t(position + 2)] = uint8_t(B);
            }
        }
    }
}

bool PsxMdec::rl_decode_block(int16_t* blk, const uint8_t* qt) {
    if (block_pointer_ >= 63) {
        std::memset(blk, 0, 64 * sizeof(int16_t));
        if (in_buffer_.empty()) return false;
        n_ = in_buffer_.front();
        in_buffer_.pop_front();
        while (n_ == 0xFE00) {
            if (in_buffer_.empty()) return false;
            n_ = in_buffer_.front();
            in_buffer_.pop_front();
        }
        q_scale_ = (n_ >> 10) & 0x3F;
        val_ = signed10bit(n_ & 0x3FF) * qt[0];
        block_pointer_ = 0;
    }

    while (block_pointer_ < 63) {
        if (q_scale_ == 0) val_ = signed10bit(n_ & 0x3FF) * 2;
        val_ = std::min(std::max(val_, -0x400), 0x3FF);
        if (q_scale_ > 0) blk[kZagzig[block_pointer_]] = int16_t(val_);
        if (q_scale_ == 0) blk[block_pointer_] = int16_t(val_);
        if (in_buffer_.empty()) return false;
        n_ = in_buffer_.front();
        in_buffer_.pop_front();
        block_pointer_ += ((n_ >> 10) & 0x3F) + 1;
        val_ = (signed10bit(n_ & 0x3FF) * qt[block_pointer_ & 0x3F] * q_scale_ + 4) / 8;
    }
    return true;
}

void PsxMdec::idct_core(int16_t* src) {
    int16_t* a = src;
    int16_t* b = idct_dst_;
    for (int pass = 0; pass < 2; pass++) {
        for (int x = 0; x < 8; x++) {
            for (int y = 0; y < 8; y++) {
                int sum = 0;
                for (int z = 0; z < 8; z++) {
                    sum += a[y + z * 8] * (scale_table_[x + z * 8] / 8);
                }
                b[x + y * 8] = int16_t((sum + 0xFFF) / 0x2000);
            }
        }
        std::swap(a, b);
    }
    if (a != src) std::memcpy(src, a, 64 * sizeof(int16_t));
}

int PsxMdec::signed10bit(int n) { return (n << 22) >> 22; }

void PsxMdec::set_quant_table() {
    for (int i = 0; i < 32; i++) {
        if (in_buffer_.empty()) return;
        const uint16_t value = in_buffer_.front();
        in_buffer_.pop_front();
        luminance_qt_[i * 2 + 0] = uint8_t(value);
        luminance_qt_[i * 2 + 1] = uint8_t(value >> 8);
    }
    if (!is_colored_) return;
    for (int i = 0; i < 32; i++) {
        if (in_buffer_.empty()) return;
        const uint16_t value = in_buffer_.front();
        in_buffer_.pop_front();
        color_qt_[i * 2 + 0] = uint8_t(value);
        color_qt_[i * 2 + 1] = uint8_t(value >> 8);
    }
}

void PsxMdec::set_scale_table() {
    for (int i = 0; i < 64; i++) {
        if (in_buffer_.empty()) return;
        scale_table_[i] = int16_t(in_buffer_.front());
        in_buffer_.pop_front();
    }
}

uint16_t PsxMdec::convert24to15(uint8_t r, uint8_t g, uint8_t b) const {
    return uint16_t((b >> 3) << 10 | (g >> 3) << 5 | (r >> 3));
}

void PsxMdec::handle_data_out_end() {
    if (pending_bytes_ <= 0 && remaining_data_words_ == 0) {
        out_buffer_pos_ = 0;
        yuv_block_pos_ = 0;
        command_busy_ = false;
        data_out_fifo_empty_ = true;
    }
}

}  // namespace dsp
