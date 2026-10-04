#include "video/real3d.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace dsp {
namespace {

constexpr uint32_t kPciIdStep2 = 0x178611DB;
constexpr uint32_t kPciIdStep1 = 0x16C311DB;

inline float as_float(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// Real3D 16-bit "pro float" (culling radii).
float pro_float16(uint16_t v) {
    const uint32_t a = uint32_t(v) << 15;
    uint32_t exponent = (a & 0x7E000000u) >> 25;
    if (exponent <= 31) exponent += 127;
    else exponent += 127 - 64;
    const uint32_t mantissa = (a & 0x1FFFFFFu) >> 2;
    return as_float((a & 0x80000000u) | (exponent << 23) | mantissa);
}

// Tiled texel order of the uploaded texture data.
constexpr unsigned kDecode8x8[64] = {
    1,  0,  5,  4,  9,  8,  13, 12, 3,  2,  7,  6,  11, 10, 15, 14,
    17, 16, 21, 20, 25, 24, 29, 28, 19, 18, 23, 22, 27, 26, 31, 30,
    33, 32, 37, 36, 41, 40, 45, 44, 35, 34, 39, 38, 43, 42, 47, 46,
    49, 48, 53, 52, 57, 56, 61, 60, 51, 50, 55, 54, 59, 58, 63, 62};
constexpr unsigned kDecode8x4[32] = {1,  0,  5,  4,  3,  2,  7,  6,  9,  8,  13,
                                     12, 11, 10, 15, 14, 17, 16, 21, 20, 19, 18,
                                     23, 22, 25, 24, 29, 28, 27, 26, 31, 30};
constexpr unsigned kDecode8x2[16] = {1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14};
constexpr int kMipXBase[] = {0, 1024, 1536, 1792, 1920, 1984, 2016, 2032, 2040, 2044, 2046, 2047};
constexpr int kMipYBase[] = {0, 512, 768, 896, 960, 992, 1008, 1016, 1020, 1022, 1023};

// JTAG TAP state machine: next state for tms = 0 / 1.
constexpr int kTapNext[16][2] = {
    {1, 0},  {1, 2},  {3, 9},  {4, 5},  {4, 5},  {6, 8},   {6, 7},  {4, 8},
    {1, 2},  {10, 0}, {11, 12}, {11, 12}, {13, 15}, {13, 14}, {11, 15}, {1, 2}};
enum TapState { kTestLogicReset = 0, kCaptureDR = 3, kShiftDR = 4, kUpdateDR = 8,
                kCaptureIR = 10, kShiftIR = 11, kUpdateIR = 15 };

}  // namespace

// ---------------------------------------------------------------------------

Real3D::Real3D()
    : cull_lo_(0x100000, 0), cull_hi_(0x40000, 0), poly_(0x100000, 0), tex_(2048 * 2048, 0) {
    fifo_.reserve(0x40000);
    zbuf_.assign(size_t(kWidth) * kHeight, 0.0f);
    colour_.assign(size_t(kWidth) * kHeight, 0);
    // JTAG chain of a Step 2.1 video board.
    static constexpr int kChain[17] = {4, 0, 1, 2, -1, 3, 3, -1, -1, -1, 2, -1, 3, 3, -1, -1, -1};
    static constexpr uint32_t kId[5] = {0x416c3057, 0x316c4057, 0x516c5057, 0x316c6057, 0x516c7057};
    for (int i = 0; i < 17; ++i) {
        jtag_[size_t(i)].asic = kChain[i] >= 0;
        jtag_[size_t(i)].asic_index = kChain[i];
        jtag_[size_t(i)].idcode = kChain[i] >= 0 ? kId[kChain[i]] : 0;
    }
    reset();
}

void Real3D::set_vrom(const uint8_t* data, size_t size) {
    vrom_ = data;
    vrom_words_ = size / 4;
}

void Real3D::reset() {
    std::fill(cull_lo_.begin(), cull_lo_.end(), 0);
    std::fill(cull_hi_.begin(), cull_hi_.end(), 0);
    std::fill(poly_.begin(), poly_.end(), 0);
    std::fill(tex_.begin(), tex_.end(), 0);
    fifo_.clear();
    pending_hi_.clear();
    pending_poly_.clear();
    config_.fill(0);
    vrom_fifo_idx_ = 0;
    ping_pong_ = ping_pong_copy_ = false;
    command_written_ = tilegen_draw_ = false;
    frame_ready_ = false;
    block_culling_ = false;
    dma_src_ = dma_dst_ = dma_len_ = dma_data_ = 0;
    dma_status_ = dma_config_ = 0;
    modeword_.fill(0);
    jtag_state_ = kTestLogicReset;
    jtag_last_tck_ = false;
    jtag_tdo_ = false;
    jtag_reset_devices();
    stats_polys_ = stats_viewports_ = stats_uploads_ = 0;
}

uint32_t Real3D::vrom_word(uint32_t index) const {
    if (index >= vrom_words_) return 0;
    uint32_t v;
    std::memcpy(&v, vrom_ + size_t(index) * 4, 4);
    return v;
}

// ---------------------------------------------------------------------------
// Registers and memories

uint32_t Real3D::read_register(unsigned reg) {
    if (reg == 0) return 0xfdffffffu | (ping_pong_ ? 0x02000000u : 0u);
    if (reg >= 20 && reg <= 32) return 0;  // line-of-sight results (float 0.0)
    return 0xffffffffu;
}

void Real3D::flush() {
    command_written_ = true;
    if (!ping_pong_flipped()) {
        flush_textures();
        if (tilegen_draw_) draw_frame();
    }
}

void Real3D::write_culling_low(uint32_t offset, uint32_t value) {
    cull_lo_[(offset / 4) & 0xfffff] = value;
}

void Real3D::write_culling_high(uint32_t offset, uint32_t value) {
    const uint32_t index = (offset / 4) & 0x3ffff;
    if (ping_pong_flipped()) pending_hi_.push_back({index, value});
    else cull_hi_[index] = value;
}

void Real3D::write_polygon_ram(uint32_t offset, uint32_t value) {
    const uint32_t index = (offset / 4) & 0xfffff;
    if (ping_pong_flipped()) pending_poly_.push_back({index, value});
    else poly_[index] = value;
}

void Real3D::write_texture_fifo(uint32_t value) {
    if (fifo_.size() < 0x100000 / 4) fifo_.push_back(value);
}

void Real3D::write_texture_port(uint32_t value) {
    // Step 2.x: pairs of (VROM word address, texture header).
    if (vrom_fifo_idx_ == 2) {
        const uint32_t addr = vrom_fifo_[0] & 0xffffff;
        const uint32_t header = vrom_fifo_[1];
        // Enough source data for a full mipmapped 1024x1024 16-bit texture.
        constexpr size_t kMaxTexels = 1024 * 1024 * 2;
        std::vector<uint16_t> data(kMaxTexels, 0);
        if (addr < 0x100000) {
            for (size_t i = 0; i < kMaxTexels / 2 && addr + i < 0x100000; ++i) {
                const uint32_t w = poly_[addr + i];
                data[i * 2] = uint16_t(w);
                data[i * 2 + 1] = uint16_t(w >> 16);
            }
        } else if (size_t(addr) < vrom_words_) {
            const size_t avail = std::min(kMaxTexels, (vrom_words_ - addr) * 2);
            std::memcpy(data.data(), vrom_ + size_t(addr) * 4, avail * 2);
        }
        upload_texture(header, data.data());
        vrom_fifo_idx_ = 0;
    } else {
        vrom_fifo_[size_t(vrom_fifo_idx_++)] = value;
    }
}

void Real3D::flush_textures() {
    const size_t n = fifo_.size();
    if (n > 2) {
        std::vector<uint16_t> data;
        for (size_t i = 0; i + 2 < n;) {
            const uint32_t size = (2 + fifo_[i] / 2) / 4;
            const uint32_t header = fifo_[i + 1];
            if (size == 0) break;
            // Texels: low half of each word first.
            data.assign(1024 * 1024 * 2, 0);
            for (size_t k = 0; i + 2 + k < n && k * 2 + 1 < data.size(); ++k) {
                data[k * 2] = uint16_t(fifo_[i + 2 + k]);
                data[k * 2 + 1] = uint16_t(fifo_[i + 2 + k] >> 16);
            }
            upload_texture(header, data.data());
            i += size;
        }
    }
    fifo_.clear();
}

void Real3D::store_texture(unsigned x0, unsigned y0, unsigned width, unsigned height,
                           const uint16_t* data, bool sixteen_bit, bool write_lsb, bool write_msb,
                           uint32_t& consumed) {
    const unsigned tile_x = std::min(8u, width);
    const unsigned tile_y = std::min(8u, height);
    const unsigned* decode = tile_x == 8 ? kDecode8x8 : tile_x == 4 ? kDecode8x4 : tile_x == 2 ? kDecode8x2 : nullptr;
    consumed = 0;
    if (decode == nullptr) return;
    if (sixteen_bit) {
        for (unsigned y = y0; y < y0 + height; y += tile_y) {
            for (unsigned x = x0; x < x0 + width; x += tile_x) {
                for (unsigned yy = 0; yy < tile_y; ++yy) {
                    for (unsigned xx = 0; xx < tile_x; ++xx) {
                        tex_[((y + yy) & 2047) * 2048 + ((x + xx) & 2047)] = data[decode[yy * tile_x + xx]];
                        ++consumed;
                    }
                }
                data += tile_x * tile_y;
            }
        }
    } else {
        // 8-bit texels are unpacked into one byte of the 16-bit texel.
        const unsigned select = (write_lsb ? 1u : 0u) | (write_msb ? 2u : 0u);
        static constexpr uint16_t kMask[4] = {0xffff, 0xff00, 0x00ff, 0x0000};
        const unsigned step = std::max(1u, (tile_x * tile_y) / 2);
        for (unsigned y = y0; y < y0 + height; y += tile_y) {
            for (unsigned x = x0; x < x0 + width; x += tile_x) {
                for (unsigned yy = 0; yy < tile_y; ++yy) {
                    for (unsigned xx = 0; xx < tile_x; ++xx) {
                        if (select == 0) continue;
                        uint16_t& t = tex_[((y + yy) & 2047) * 2048 + ((x + xx) & 2047)];
                        t &= kMask[select];
                        const unsigned shift = 8 * ((xx & 1) ^ 1);
                        const unsigned index = (yy ^ 1) * tile_x + (xx ^ 1) - (tile_x & 1);
                        uint16_t v = uint16_t((data[decode[index] / 2] >> shift) & 0xff);
                        v = uint16_t(v | (v << 8));
                        v &= uint16_t(kMask[select] ^ 0xffff);
                        t |= v;
                    }
                }
                data += step;
                consumed += step;
            }
        }
    }
}

void Real3D::upload_texture(uint32_t header, const uint16_t* data) {
    if (before_texture_write_) before_texture_write_();
    const unsigned x = 32 * (header & 0x3f);
    const unsigned y = 32 * ((header >> 7) & 0x1f);
    const unsigned page = (header >> 20) & 1;
    unsigned width = 32u << ((header >> 14) & 7);
    unsigned height = 32u << ((header >> 17) & 7);
    const unsigned type = (header >> 24) & 0xff;
    const bool sixteen = (header >> 23) & 1;
    const bool upper = (header >> 22) & 1;
    const bool lower = (header >> 21) & 1;
    uint32_t used = 0;
    ++stats_uploads_;
    if (width > 2048 || height > 1024) return;
    switch (type) {
        case 0x00:
        case 0x01:
            store_texture(x, y + page * 1024, width, height, data, sixteen, lower, upper, used);
            data += used;
            if (type == 0x01) break;
            [[fallthrough]];
        case 0x02:
            for (int i = 1; width >= 4 && height >= 4 && i < 11; ++i) {
                const unsigned mx = unsigned(kMipXBase[i]) + (x >> i);
                const unsigned my = unsigned(kMipYBase[i]) + (y >> i);
                width /= 2;
                height /= 2;
                store_texture(mx, my + page * 1024, width, height, data, sixteen, lower, upper, used);
                data += used;
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// DMA

uint8_t Real3D::read_dma8(unsigned reg) const {
    switch (reg) {
        case 0xc: return dma_status_;
        case 0xe: return dma_config_;
        default: return 0;
    }
}

void Real3D::write_dma8(unsigned reg, uint8_t value) {
    if (reg == 0xd) {
        if (value & 1) {
            dma_status_ &= uint8_t(~1);
            if (host_.dma_irq) host_.dma_irq(false);
        }
    } else if (reg == 0xe) {
        dma_config_ = value;
    }
}

uint32_t Real3D::read_dma32(unsigned reg) {
    if (reg == 0x14) return dma_data_;
    return 0;
}

void Real3D::write_dma32(unsigned reg, uint32_t value) {
    switch (reg) {
        case 0x00: dma_src_ = value; break;
        case 0x04: dma_dst_ = value; break;
        case 0x08:
            dma_len_ = value;
            while (dma_len_ != 0) {
                uint32_t d = host_.read32 ? host_.read32(dma_src_) : 0;
                if (dma_config_ & 0x80) d = __builtin_bswap32(d);
                if (host_.write32) host_.write32(dma_dst_, d);
                dma_src_ += 4;
                dma_dst_ += 4;
                --dma_len_;
            }
            if (dma_config_ & 1) {
                dma_status_ |= 1;
                if (host_.dma_irq) host_.dma_irq(true);
            }
            break;
        case 0x10:
            if (value & 0x20000000) dma_data_ = kPciIdStep1;
            else if (value & 0x80000000) dma_data_ = read_register(value & 0x3f);
            break;
        case 0x14: dma_data_ = 0xffffffff; break;
        default: break;
    }
}

uint32_t Real3D::pci_config_read(unsigned reg) const {
    if (reg == 0) return __builtin_bswap32(kPciIdStep2);
    return 0;
}

// ---------------------------------------------------------------------------
// JTAG

void Real3D::jtag_reset_devices() {
    for (auto& d : jtag_) {
        if (d.asic) {
            d.size = 32;
            d.shift = d.idcode;
            d.ir = 3;
        } else {
            d.size = 1;
            d.shift = 0;
            d.ir = 15;
        }
    }
}

void Real3D::jtag_write(bool tck, bool tms, bool tdi, bool trst) {
    if (!trst) {
        jtag_reset_devices();
        return;
    }
    if (tck == jtag_last_tck_) return;
    jtag_last_tck_ = tck;
    if (tck) {
        switch (jtag_state_) {
            case kTestLogicReset: jtag_reset_devices(); break;
            case kCaptureDR:
                for (auto& d : jtag_) {
                    if (d.asic && d.ir == 3) { d.size = 32; d.shift = d.idcode; }
                    else if (d.asic && d.ir == 8) { d.size = 32; d.shift = modeword_[size_t(d.asic_index)]; }
                    else { d.size = 1; d.shift = 0; }
                }
                break;
            case kCaptureIR:
                for (auto& d : jtag_) {
                    if (d.asic) { d.size = 5; d.shift = 1; }
                    else { d.size = 4; d.shift = 0x9; }
                }
                break;
            case kShiftDR:
            case kShiftIR: {
                bool bit = tdi;
                for (auto& d : jtag_) {
                    const bool out = d.shift & 1;
                    d.shift >>= 1;
                    if (bit) d.shift |= uint64_t(1) << (d.size - 1);
                    bit = out;
                }
                break;
            }
            default: break;
        }
        jtag_state_ = kTapNext[jtag_state_][tms ? 1 : 0];
    } else {
        switch (jtag_state_) {
            case kShiftDR:
            case kShiftIR: jtag_tdo_ = jtag_.back().shift & 1; break;
            case kUpdateDR:
                for (auto& d : jtag_) {
                    if (d.asic && d.ir == 8) modeword_[size_t(d.asic_index)] = uint32_t(d.shift);
                }
                break;
            case kUpdateIR:
                for (auto& d : jtag_) d.ir = uint8_t(d.shift);
                break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Frame protocol

void Real3D::begin_vblank() {
    ping_pong_copy_ = ping_pong_;
    for (const auto& w : pending_hi_) cull_hi_[w.index] = w.value;
    for (const auto& w : pending_poly_) poly_[w.index] = w.value;
    pending_hi_.clear();
    pending_poly_.clear();
    if (command_written_) flush_textures();
    if (tilegen_draw_ && command_written_) draw_frame();
}

void Real3D::flip_ping_pong() {
    ping_pong_ = !ping_pong_;
    tilegen_draw_ = false;
    command_written_ = false;
}

void Real3D::tilegen_draw_frame() {
    tilegen_draw_ = true;
    if (!ping_pong_flipped() && command_written_) draw_frame();
}

void Real3D::draw_frame() {
    tilegen_draw_ = false;
    command_written_ = false;
    block_culling_ = (modeword_[0] & 4) != 0;  // Mercury
    frame_ready_ = true;
}

// ---------------------------------------------------------------------------
// Scene traversal

const uint32_t* Real3D::cull_ptr(uint32_t addr) const {
    addr &= 0xffffff;
    if (addr >= 0x800000 && addr < 0x840000) return &cull_hi_[addr & 0x3ffff];
    if (addr < 0x100000) return &cull_lo_[addr];
    return nullptr;
}

const uint32_t* Real3D::model_ptr(uint32_t addr) const {
    addr &= 0xffffff;
    if (addr < 0x100000) return &poly_[addr];
    if (size_t(addr) + 16 < vrom_words_) return reinterpret_cast<const uint32_t*>(vrom_ + size_t(addr) * 4);
    return nullptr;
}

namespace {

// out = a * b (column major)
void mat_mul(float* out, const float* a, const float* b) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr)
            r[c * 4 + rr] = a[0 * 4 + rr] * b[c * 4 + 0] + a[1 * 4 + rr] * b[c * 4 + 1] +
                            a[2 * 4 + rr] * b[c * 4 + 2] + a[3 * 4 + rr] * b[c * 4 + 3];
    std::memcpy(out, r, sizeof(r));
}


}  // namespace

void Real3D::mult_matrix(uint32_t index) {
    if (matrix_base_ == nullptr) return;
    const uint32_t* s = matrix_base_ + size_t(index) * 12;
    // Guard against matrices beyond the end of the memory.
    const uint32_t* lo_end = cull_lo_.data() + cull_lo_.size();
    const uint32_t* hi_end = cull_hi_.data() + cull_hi_.size();
    const bool in_lo = s >= cull_lo_.data() && s + 12 <= lo_end;
    const bool in_hi = s >= cull_hi_.data() && s + 12 <= hi_end;
    if (!in_lo && !in_hi) return;
    float m[16];
    m[0] = as_float(s[3]);  m[4] = as_float(s[4]);  m[8] = as_float(s[5]);   m[12] = as_float(s[0]);
    m[1] = as_float(s[6]);  m[5] = as_float(s[7]);  m[9] = as_float(s[8]);   m[13] = as_float(s[1]);
    m[2] = as_float(s[9]);  m[6] = as_float(s[10]); m[10] = as_float(s[11]); m[14] = as_float(s[2]);
    m[3] = 0;               m[7] = 0;               m[11] = 0;               m[15] = 1;
    mat_mul(mat_.m, mat_.m, m);
}

void Real3D::reset_matrix_rotation() {
    float m[16];
    std::memcpy(m, mat_.m, sizeof(m));
    std::swap(m[1], m[4]);
    std::swap(m[2], m[8]);
    std::swap(m[6], m[9]);
    m[12] = m[13] = m[14] = 0;
    m[3] = m[7] = m[11] = 0;
    m[15] = 1;
    const float s1 = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const float s2 = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    const float s3 = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    if (s1 > 0) { m[0] /= s1; m[1] /= s1; m[2] /= s1; }
    if (s2 > 0) { m[4] /= s2; m[5] /= s2; m[6] /= s2; }
    if (s3 > 0) { m[8] /= s3; m[9] /= s3; m[10] /= s3; }
    mat_mul(mat_.m, mat_.m, m);
}

void Real3D::render_viewport(uint32_t addr, int priority, int depth) {
    for (int guard = 0; guard < 256; ++guard) {
        if ((addr & 0xffffff) == 0) return;
        const uint32_t* vp = cull_ptr(addr);
        if (vp == nullptr) return;
        const int vp_priority = int((vp[0] >> 3) & 3);
        const bool disabled = (vp[0] & 0x20) != 0;
        if (vp_priority == priority) {
            ViewportState s{};
            s.priority = vp_priority;
            s.x = float(vp[0x1a] & 0xffff) / 16.0f;
            s.y = float(vp[0x1a] >> 16) / 16.0f;
            s.w = float(vp[0x14] & 0xffff) / 4.0f;
            s.h = float(vp[0x14] >> 16) / 4.0f;
            const float cv = as_float(vp[0x8]);
            const float cw = as_float(vp[0x9]);
            const float io = as_float(vp[0xa]);
            const float jo = as_float(vp[0xb]);
            for (int i = 0; i < 8; ++i) planes_[size_t(i)] = as_float(vp[0xc + i]);
            s.l = (0.0f - jo) / cv;
            s.r = (1.0f - jo) / cv;
            s.b = -(1.0f - io) / cw;
            s.t = io / cw;
            s.sun[0] = as_float(vp[0x05]);
            s.sun[1] = -as_float(vp[0x06]);
            s.sun[2] = -as_float(vp[0x04]);
            s.sun_intensity = std::clamp(as_float(vp[0x07]), 0.0f, 1.0f);
            if (std::isnan(s.sun_intensity)) s.sun_intensity = 0;
            s.ambient = float((vp[0x24] >> 8) & 0xff) / 255.0f;
            s.fog_rgb[0] = float((vp[0x22] >> 16) & 0xff) / 255.0f;
            s.fog_rgb[1] = float((vp[0x22] >> 8) & 0xff) / 255.0f;
            s.fog_rgb[2] = float(vp[0x22] & 0xff) / 255.0f;
            s.fog_density = std::fabs(as_float(vp[0x23]));
            if (!std::isfinite(s.fog_density)) s.fog_density = 0;
            s.fog_start = float(int16_t(vp[0x25] & 0xffff)) / 255.0f;
            s.fog_ambient = float((vp[0x25] >> 16) & 0xff) / 255.0f;
            s.scroll_fog = float(vp[0x20] & 0xff) / 255.0f;
            const bool sane = std::isfinite(s.l) && std::isfinite(s.r) && std::isfinite(s.b) &&
                              std::isfinite(s.t) && s.r != s.l && s.t != s.b && s.w > 0 && s.h > 0;
            if (sane) {
                viewports_.push_back(s);
                ++stats_viewports_;
                // Node attributes reset per viewport.
                lod_table_ = cull_ptr(vp[0x17] & 0xffffff);
                model_scale_ = 1.0f;
                model_alpha_ = 1.0f;
                disable_culling_ = false;
                tex_off_x_ = tex_off_y_ = tex_page_ = 0;
                // Base matrix: Model 3 (Z, X, Y) order to OpenGL view space.
                std::memset(mat_.m, 0, sizeof(mat_.m));
                mat_.m[4] = 1.0f;    // row 0, col 1
                mat_.m[9] = -1.0f;   // row 1, col 2
                mat_.m[2] = -1.0f;   // row 2, col 0
                mat_.m[15] = 1.0f;
                matrix_base_ = cull_ptr(vp[0x16] & 0xffffff);
                mult_matrix(0);
                mat_stack_.clear();
                if (!disabled && ((vp[0x02] >> 24) & 5) == 0) descend_ptr(vp[0x02], depth);
            }
        }
        if (vp[0x01] == 0x01000000) return;
        addr = vp[0x01];
    }
}

void Real3D::descend_ptr(uint32_t addr, int depth) {
    if ((addr & 0xffffff) == 0) return;
    switch ((addr >> 24) & 5) {
        case 0: descend_node(addr & 0xffffff, depth + 1); break;
        case 1: draw_model(addr & 0xffffff); break;
        case 4: descend_list(addr & 0xffffff, depth + 1); break;
        default: break;
    }
}

void Real3D::descend_list(uint32_t addr, int depth) {
    const uint32_t* list = cull_ptr(addr);
    if (list == nullptr) return;
    for (int i = 0; i < 4096; ++i) {
        if (list[i] & 0x01000000) break;
        descend_node(list[i] & 0xffffff, depth + 1);
        if (list[i] & 0x02000000) break;
    }
}

void Real3D::descend_node(uint32_t addr, int depth) {
    if (depth > 48 || cur_polys_ > 200000) return;
    const uint32_t* node = cull_ptr(addr);
    if (node == nullptr) return;
    if ((node[0] & 3) == 0) return;               // viewport nodes are not rendered
    if ((node[0] & 0x300) == 0x300) return;       // discarded node

    const uint32_t child = node[7] & 0x7ffffff;
    const uint32_t sibling = node[8] & 0x1ffffff;
    const uint32_t matrix_index = node[3] & 0xfff;
    const uint32_t lod_index = (node[3] >> 12) & 0x7f;

    if ((node[0] & 7) != 6 && !(sibling & 0x1000000) && sibling) descend_node(sibling, depth + 1);

    if (node[0] & 4) {
        color_table_ = ((node[3] >> 19) | ((node[7] >> 28) << 13) | ((node[8] >> 25) << 17)) & 0xfffff;
    }

    // Save attributes.
    const float saved_scale = model_scale_, saved_alpha = model_alpha_;
    const bool saved_disable = disable_culling_;
    const int sx = tex_off_x_, sy = tex_off_y_, sp = tex_page_;
    const Mat4 saved_mat = mat_;

    if (node[1] & 1) model_scale_ = as_float(node[1] & ~3u);
    if (node[1] & 2) disable_culling_ = true;
    if (node[2] & 0x8000) {
        tex_off_x_ = 32 * int((node[2] >> 7) & 0x3f);
        tex_off_y_ = 32 * int(node[2] & 0x1f);
        tex_page_ = int((node[2] & 0x4000) >> 14);
    }
    if (node[0] & 0x10) {
        const float t[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                             as_float(node[4]), as_float(node[5]), as_float(node[6]), 1};
        mat_mul(mat_.m, mat_.m, t);
    } else if (matrix_index) {
        mult_matrix(matrix_index);
    }
    if (node[0] & 0x80) reset_matrix_rotation();

    const float x = mat_.m[12], y = mat_.m[13], z = mat_.m[14];
    const float cull_radius = pro_float16(uint16_t(node[9] & 0xffff)) * model_scale_;
    const float blend_radius = pro_float16(uint16_t(node[9] >> 16)) * model_scale_;
    const auto& pl = planes_;
    const bool outside = (z * pl[0] - x * pl[1]) > cull_radius || (z * pl[2] + y * pl[3]) > cull_radius ||
                         (z * pl[4] - x * pl[5]) > cull_radius || (z * pl[6] + y * pl[7]) > cull_radius;
    float lod_scale = disable_culling_ ? 3.4e38f : blend_radius / std::sqrt(x * x + y * y + z * z);
    if (!(lod_scale >= 0)) lod_scale = 0;  // NaN guard
    float del[4] = {0, 0, 0, 0}, blend[4] = {1, 1, 1, 1};
    if (lod_table_ != nullptr) {
        const uint32_t* e = lod_table_ + size_t(lod_index) * 8;
        for (int i = 0; i < 4; ++i) {
            del[i] = as_float(e[i * 2]);
            blend[i] = as_float(e[i * 2 + 1]);
            if (!std::isfinite(del[i])) del[i] = 0;
            if (!std::isfinite(blend[i])) blend[i] = 1;
        }
    }

    if (disable_culling_ || (!outside && lod_scale >= del[3])) {
        if (node[0] & 0x08) {
            // 4-element LOD table.
            const uint32_t* lod = cull_ptr(child);
            if (lod != nullptr) {
                int level = 0;
                for (; level < 3; ++level)
                    if (lod_scale >= del[level] && (lod[level] & 0x1000000)) break;
                const float parent_alpha = model_alpha_;
                float node_alpha = std::clamp(blend[level] * (lod_scale - del[level]), 0.0f, 1.0f);
                if (node_alpha > 31.0f / 32.0f) node_alpha = 1.0f;
                else if (node_alpha < 1.0f / 32.0f) node_alpha = 0.0f;
                model_alpha_ *= node_alpha;
                const bool nodes = (node[3] & 0x20000000) != 0;
                if (nodes) descend_node(lod[level] & 0xffffff, depth + 1);
                else draw_model(lod[level] & 0xffffff);
                if (node_alpha < 1.0f && level != 3) {
                    model_alpha_ = (1.0f - node_alpha) * parent_alpha;
                    if (nodes) descend_node(lod[level + 1] & 0xffffff, depth + 1);
                    else draw_model(lod[level + 1] & 0xffffff);
                }
            }
        } else {
            const float node_alpha = std::clamp(blend[3] * (lod_scale - del[3]), 0.0f, 1.0f);
            model_alpha_ *= node_alpha;
            descend_ptr(child, depth);
        }
    }

    mat_ = saved_mat;
    model_scale_ = saved_scale;
    model_alpha_ = saved_alpha;
    disable_culling_ = saved_disable;
    tex_off_x_ = sx;
    tex_off_y_ = sy;
    tex_page_ = sp;
}

void Real3D::draw_model(uint32_t addr) {
    const uint32_t* h = model_ptr(addr);
    if (h == nullptr || model_alpha_ <= 0.0f) return;
    const uint32_t* end = (addr < 0x100000) ? poly_.data() + poly_.size()
                                            : reinterpret_cast<const uint32_t*>(vrom_ + vrom_words_ * 4);
    const int vp_index = int(viewports_.size()) - 1;
    const ViewportState& vs = viewports_[size_t(vp_index)];
    const float* M = mat_.m;
    const float inv_scale = model_scale_ != 0 ? 1.0f / model_scale_ : 1.0f;

    struct Prev {
        float x, y, z, nx, ny, nz, shade;
        uint16_t u, v;
    };
    static thread_local Prev prev[4] = {};
    static constexpr int kSharedCount[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

    for (int guard = 0; guard < 65536; ++guard) {
        if (h + 7 > end) return;
        if (h[6] == 0) return;
        const int num = (h[0] & 0x40) ? 4 : 3;
        const int shared = kSharedCount[h[0] & 0xf];
        const uint32_t* vdata = h + 7;
        if (vdata + (num - shared) * 4 > end) return;

        Poly p;
        const bool smooth = (h[1] & 0x8) != 0;
        const bool fixed = (h[1] & 0x20) != 0;
        const float uv_scale = (h[1] & 0x40) ? 1.0f : 0.125f;
        p.textured = (h[6] & 0x400) != 0;
        p.alpha_test = (h[6] & 0x80000000u) != 0;
        p.texture_alpha = (h[6] & 7) != 0;
        p.poly_alpha = (h[6] & 0x800000) == 0;
        p.lighting = (h[6] & 0x10000) == 0;
        p.fixed_shading = fixed && !smooth;
        p.high_priority = (h[6] & 0x10) != 0;
        p.fog_intensity = float((h[6] >> 11) & 0x1f) / 16.0f;
        int tw = 32 << (((h[3] >> 3) & 7) >= 6 ? 0 : ((h[3] >> 3) & 7));
        int th = 32 << ((h[3] & 7) >= 6 ? 0 : (h[3] & 7));
        p.tw = tw;
        p.th = th;
        p.tx = ((32 * (int(((h[4] & 0x1f) << 1) | ((h[5] >> 7) & 1)))) + tex_off_x_) & 2047;
        p.ty = ((32 * int(h[5] & 0x1f)) + tex_off_y_) & 2047;
        p.page = int((h[4] & 0x40) >> 6) ^ tex_page_;
        int format = int((h[6] >> 7) & 7);
        if (p.alpha_test && format >= 1 && format <= 4) format += 7;
        if (format == 7) p.alpha_test = false;
        p.format = format;
        p.mirror_u = (h[2] & 2) != 0;
        p.mirror_v = (h[2] & 1) != 0;
        uint32_t rgb;
        if (h[1] & 2) {
            rgb = h[4] >> 8;
        } else {
            const uint32_t idx = (h[4] >> 8) & 0xfff;
            rgb = poly_[(color_table_ + idx) & 0xfffff] & 0xffffff;
        }
        // Translator map: colours are scaled by 16 (clamped when shading).
        const float cscale = (h[4] & 0x80) ? 16.0f / 255.0f : 1.0f / 255.0f;
        p.r = float((rgb >> 16) & 0xff) * cscale;
        p.g = float((rgb >> 8) & 0xff) * cscale;
        p.b = float(rgb & 0xff) * cscale;
        p.inverted = ((h[6] >> 24) & 0x7f) == 2;
        int alpha8 = (h[6] & 0x800000) ? 255 : int(((h[6] >> 18) & 0x3f) * 255 / 32);
        if ((h[0] & 0x200) && !(h[0] & 0x100)) alpha8 /= 2;
        if (alpha8 > 255) alpha8 = 255;
        p.a = float(alpha8) / 255.0f * model_alpha_;
        p.node_alpha = model_alpha_ < 1.0f;
        p.vp = vp_index;

        const float fnx = float(int32_t(h[1]) >> 8) / 4194304.0f;
        const float fny = float(int32_t(h[2]) >> 8) / 4194304.0f;
        const float fnz = float(int32_t(h[3]) >> 8) / 4194304.0f;

        Prev cur[4];
        int j = 0;
        for (int i = 0; i < 4; ++i)
            if (h[0] & (1u << i)) cur[j++] = prev[i];
        for (int i = 0; i < num && !smooth; ++i) {
            cur[i].nx = fnx;
            cur[i].ny = fny;
            cur[i].nz = fnz;
        }
        for (; j < num; ++j) {
            const uint32_t ix = vdata[0], iy = vdata[1], iz = vdata[2], it = vdata[3];
            cur[j].x = float(int32_t(ix) >> 8) / 2048.0f;
            cur[j].y = float(int32_t(iy) >> 8) / 2048.0f;
            cur[j].z = float(int32_t(iz) >> 8) / 2048.0f;
            if (smooth) {
                cur[j].nx = float(int8_t(ix & 0xff)) / 128.0f;
                cur[j].ny = float(int8_t(iy & 0xff)) / 128.0f;
                cur[j].nz = float(int8_t(iz & 0xff)) / 128.0f;
            } else {
                cur[j].nx = fnx;
                cur[j].ny = fny;
                cur[j].nz = fnz;
            }
            cur[j].shade = (h[0] & 0x80) ? float(ix & 0xff) / 255.0f : float(int8_t(ix & 0xff)) / 128.0f;
            cur[j].u = uint16_t(it >> 16);
            cur[j].v = uint16_t(it & 0xffff);
            vdata += 4;
        }
        for (int i = 0; i < 4; ++i) prev[i] = cur[i];

        const bool discard = (h[0] & 0x300) == 0x300;
        const bool last = (h[1] & 4) != 0;
        const uint32_t* next = h + 7 + (num - shared) * 4;

        if (!discard && alpha8 > 0 && p.a > 0.0f) {
            Deferred d;
            d.p = p;
            d.n = num;
            // Transform to view space.
            for (int i = 0; i < num; ++i) {
                const Prev& c = cur[i];
                Vtx& v = d.v[i];
                v.x = M[0] * c.x + M[4] * c.y + M[8] * c.z + M[12];
                v.y = M[1] * c.x + M[5] * c.y + M[9] * c.z + M[13];
                v.z = M[2] * c.x + M[6] * c.y + M[10] * c.z + M[14];
                v.nx = (M[0] * c.nx + M[4] * c.ny + M[8] * c.nz) * inv_scale;
                v.ny = (M[1] * c.nx + M[5] * c.ny + M[9] * c.nz) * inv_scale;
                v.nz = (M[2] * c.nx + M[6] * c.ny + M[10] * c.nz) * inv_scale;
                v.shade = c.shade;
                v.u = float(c.u) * uv_scale / float(tw);
                v.v = float(c.v) * uv_scale / float(th);
            }
            // Back face test with the face normal.
            const float vnx = M[0] * fnx + M[4] * fny + M[8] * fnz;
            const float vny = M[1] * fnx + M[5] * fny + M[9] * fnz;
            const float vnz = M[2] * fnx + M[6] * fny + M[10] * fnz;
            const float facing = d.v[0].x * vnx + d.v[0].y * vny + d.v[0].z * vnz;
            const bool double_sided = (h[1] & 0x10) != 0;
            if (facing <= 0.0f || double_sided) {
                if (facing > 0.0f) {  // back side of a double sided polygon: flip normals
                    for (int i = 0; i < num; ++i) {
                        d.v[i].nx = -d.v[i].nx;
                        d.v[i].ny = -d.v[i].ny;
                        d.v[i].nz = -d.v[i].nz;
                    }
                }
                (void)vs;
                frame_polys_[size_t(vs.priority)].push_back(d);
                ++cur_polys_;
            }
        }
        if (last) return;
        h = next;
    }
}

// ---------------------------------------------------------------------------
// Rasterizer

namespace {

struct RV {  // projected vertex
    float sx, sy;      // screen
    float iw;          // 1/w (w = -z)
    float uw, vw;      // u/w, v/w
    float r, g, b;     // lighting (multiplier)
    float fog;         // fog factor
};

// Texel decoding (ExtractColour) for the 12 texture formats, as 0xAARRGGBB.
const uint32_t* texel_lut() {
    static const std::vector<uint32_t> lut = [] {
        std::vector<uint32_t> t(size_t(12) << 16);
        auto pack = [](float r, float g, float b, float a) {
            auto c = [](float x) { return uint32_t(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return c(a) << 24 | c(r) << 16 | c(g) << 8 | c(b);
        };
        for (int type = 0; type < 12; ++type) {
            for (uint32_t v = 0; v < 65536; ++v) {
                float r = 0, g = 0, b = 0, a = 0;
                switch (type) {
                    case 0:
                        r = float((v >> 10) & 31) / 31.0f;
                        g = float((v >> 5) & 31) / 31.0f;
                        b = float(v & 31) / 31.0f;
                        a = 1.0f - float((v >> 15) & 1);
                        break;
                    case 1: r = g = b = float(v & 15) / 15.0f; a = float((v >> 4) & 15) / 15.0f; break;
                    case 2: a = float(v & 15) / 15.0f; r = g = b = float((v >> 4) & 15) / 15.0f; break;
                    case 3: r = g = b = float((v >> 8) & 15) / 15.0f; a = float((v >> 12) & 15) / 15.0f; break;
                    case 4: a = float((v >> 8) & 15) / 15.0f; r = g = b = float((v >> 12) & 15) / 15.0f; break;
                    case 5:
                        r = g = b = float(v & 0xff) / 255.0f;
                        a = (v & 0xff) == 0xff ? 0.0f : 1.0f;
                        break;
                    case 6:
                        r = g = b = float((v >> 8) & 0xff) / 255.0f;
                        a = ((v >> 8) & 0xff) == 0xff ? 0.0f : 1.0f;
                        break;
                    case 7:
                        r = float((v >> 12) & 15) / 15.0f;
                        g = float((v >> 8) & 15) / 15.0f;
                        b = float((v >> 4) & 15) / 15.0f;
                        a = float(v & 15) / 15.0f;
                        break;
                    default: {
                        const uint32_t n = (v >> ((type - 8) * 4)) & 15;
                        r = g = b = float(n) / 15.0f;
                        a = n == 15 ? 0.0f : 1.0f;
                        break;
                    }
                }
                t[(size_t(type) << 16) | v] = pack(r, g, b, a);
            }
        }
        return t;
    }();
    return lut.data();
}

inline int wrap_texel(int i, int size, bool mirror) {
    if (mirror) {
        const int m = i & (2 * size - 1);
        return m < size ? m : 2 * size - 1 - m;
    }
    return i & (size - 1);
}

inline uint32_t lerp_rgba(uint32_t a, uint32_t b, uint32_t f) {  // f = 0..256
    const uint32_t rb = ((((b & 0x00ff00ffu) * f) + ((a & 0x00ff00ffu) * (256 - f))) >> 8) & 0x00ff00ffu;
    const uint32_t ag = ((((b >> 8) & 0x00ff00ffu) * f + ((a >> 8) & 0x00ff00ffu) * (256 - f))) & 0xff00ff00u;
    return rb | ag;
}

}  // namespace

void Real3D::raster_poly(const Vtx* in, int n, const Poly& p, bool translucent, int band_y0, int band_y1) {
    const ViewportState& vs = viewports_[size_t(p.vp)];
    constexpr float kNear = 1.0f / 1024.0f;

    // Per-vertex lighting and fog.
    struct LV {
        Vtx v;
        float r, g, b, fog;
    };
    LV lv[4];
    for (int i = 0; i < n; ++i) {
        lv[i].v = in[i];
        float intensity = 1.0f;
        if (p.lighting) {
            float sf = p.fixed_shading ? in[i].shade
                                       : vs.sun[0] * in[i].nx + vs.sun[1] * in[i].ny + vs.sun[2] * in[i].nz;
            sf = std::clamp(sf, -1.0f, 1.0f);
            if (sun_clamp_ || p.a < 0.999f) sf = std::max(sf, 0.0f);
            intensity = sf * vs.sun_intensity + vs.ambient;
        }
        lv[i].r = lv[i].g = lv[i].b = intensity;
        const float z = -in[i].z;
        lv[i].fog = p.fog_intensity * std::clamp(vs.fog_start + z * vs.fog_density, 0.0f, 1.0f);
    }

    // Clip against the near plane (z < -kNear is visible).
    LV clipped[8];
    int cn = 0;
    for (int i = 0; i < n; ++i) {
        const LV& a = lv[i];
        const LV& b = lv[(i + 1) % n];
        const bool ain = a.v.z <= -kNear, bin = b.v.z <= -kNear;
        if (ain) clipped[cn++] = a;
        if (ain != bin) {
            const float t = (-kNear - a.v.z) / (b.v.z - a.v.z);
            LV c{};
            auto lerp = [t](float x, float y) { return x + (y - x) * t; };
            c.v.x = lerp(a.v.x, b.v.x);
            c.v.y = lerp(a.v.y, b.v.y);
            c.v.z = -kNear;
            c.v.u = lerp(a.v.u, b.v.u);
            c.v.v = lerp(a.v.v, b.v.v);
            c.r = lerp(a.r, b.r);
            c.g = lerp(a.g, b.g);
            c.b = lerp(a.b, b.b);
            c.fog = lerp(a.fog, b.fog);
            clipped[cn++] = c;
        }
    }
    if (cn < 3) return;

    // Project.
    RV rv[8];
    const float rl = vs.r - vs.l, tb = vs.t - vs.b;
    for (int i = 0; i < cn; ++i) {
        const LV& c = clipped[i];
        const float iw = 1.0f / -c.v.z;
        const float nx = (2.0f * c.v.x * iw - vs.r - vs.l) / rl;
        const float ny = (2.0f * c.v.y * iw - vs.t - vs.b) / tb;
        rv[i].sx = vs.x + (nx + 1.0f) * 0.5f * vs.w;
        rv[i].sy = vs.y + (1.0f - ny) * 0.5f * vs.h;
        rv[i].iw = iw;
        rv[i].uw = c.v.u * iw;
        rv[i].vw = c.v.v * iw;
        rv[i].r = c.r;
        rv[i].g = c.g;
        rv[i].b = c.b;
        rv[i].fog = c.fog;
        if (!std::isfinite(rv[i].sx) || !std::isfinite(rv[i].sy)) return;
    }

    const int clip_x0 = std::max(0, int(std::floor(vs.x)));
    const int clip_y0 = std::max(band_y0, int(std::floor(vs.y)));
    const int clip_x1 = std::min(kWidth, int(std::ceil(vs.x + vs.w)));
    const int clip_y1 = std::min(band_y1, int(std::ceil(vs.y + vs.h)));
    if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) return;

    const float fog_r = vs.fog_rgb[0] * vs.fog_ambient * 255.0f;
    const float fog_g = vs.fog_rgb[1] * vs.fog_ambient * 255.0f;
    const float fog_b = vs.fog_rgb[2] * vs.fog_ambient * 255.0f;
    const int page_base = (p.page & 1) * 1024;
    const uint32_t* lut = texel_lut() + (size_t(p.format) << 16);
    const uint16_t* tex = tex_.data();
    const float base_r = p.r * 255.0f, base_g = p.g * 255.0f, base_b = p.b * 255.0f;
    const float tw = float(p.tw), th = float(p.th);
    // Mip levels go down to 2x2 for square textures (else the smaller side / 4).
    int max_level = 0;
    for (int m = std::min(p.tw, p.th); m > 4; m >>= 1) ++max_level;
    max_level = std::min(max_level, 10);

    for (int t = 1; t + 1 < cn; ++t) {
        const RV* v0 = &rv[0];
        const RV* v1 = &rv[t];
        const RV* v2 = &rv[t + 1];
        const float area = (v1->sx - v0->sx) * (v2->sy - v0->sy) - (v2->sx - v0->sx) * (v1->sy - v0->sy);
        if (!(std::fabs(area) >= 1e-6f)) continue;
        const float inv_area = 1.0f / area;
        const float miny = std::min({v0->sy, v1->sy, v2->sy});
        const float maxy = std::max({v0->sy, v1->sy, v2->sy});
        const int y0 = std::max(clip_y0, int(std::ceil(miny - 0.5f)));
        const int y1 = std::min(clip_y1 - 1, int(std::ceil(maxy - 0.5f)) - 1);
        if (y0 > y1) continue;

        auto grad = [&](float a0, float a1, float a2, float& dx, float& dy) {
            dx = ((a1 - a0) * (v2->sy - v0->sy) - (a2 - a0) * (v1->sy - v0->sy)) * inv_area;
            dy = ((a2 - a0) * (v1->sx - v0->sx) - (a1 - a0) * (v2->sx - v0->sx)) * inv_area;
        };
        float iw_dx, iw_dy, uw_dx, uw_dy, vw_dx, vw_dy, r_dx, r_dy, g_dx, g_dy, b_dx, b_dy, f_dx, f_dy;
        grad(v0->iw, v1->iw, v2->iw, iw_dx, iw_dy);
        grad(v0->uw, v1->uw, v2->uw, uw_dx, uw_dy);
        grad(v0->vw, v1->vw, v2->vw, vw_dx, vw_dy);
        grad(v0->r, v1->r, v2->r, r_dx, r_dy);
        grad(v0->g, v1->g, v2->g, g_dx, g_dy);
        grad(v0->b, v1->b, v2->b, b_dx, b_dy);
        grad(v0->fog, v1->fog, v2->fog, f_dx, f_dy);

        const float sign = area > 0 ? 1.0f : -1.0f;
        const RV* edges[3][2] = {{v0, v1}, {v1, v2}, {v2, v0}};

        for (int y = y0; y <= y1; ++y) {
            const float py = float(y) + 0.5f;
            // Span of the row inside all three edges.
            float lo = float(clip_x0), hi = float(clip_x1 - 1);
            bool empty = false;
            for (const auto& e : edges) {
                const RV* a = e[0];
                const RV* b = e[1];
                // E(px) = k * px + c >= 0
                const float k = -(b->sy - a->sy) * sign;
                const float c = ((b->sx - a->sx) * (py - a->sy) + (b->sy - a->sy) * a->sx) * sign;
                if (k > 0) lo = std::max(lo, std::ceil(-c / k - 0.5f));
                else if (k < 0) hi = std::min(hi, std::floor(-c / k - 0.5f));
                else if (c < 0) empty = true;
            }
            if (empty || lo > hi) continue;
            const int xa = int(lo), xb = int(hi);

            uint32_t* row = colour_.data() + size_t(y) * kWidth;
            float* zrow = zbuf_.data() + size_t(y) * kWidth;
            const float dx0 = float(xa) + 0.5f - v0->sx, dy0 = py - v0->sy;
            float iw = v0->iw + iw_dx * dx0 + iw_dy * dy0;
            float uw = v0->uw + uw_dx * dx0 + uw_dy * dy0;
            float vw = v0->vw + vw_dx * dx0 + vw_dy * dy0;
            float lr = v0->r + r_dx * dx0 + r_dy * dy0;
            float lg = v0->g + g_dx * dx0 + g_dy * dy0;
            float lb = v0->b + b_dx * dx0 + b_dy * dy0;
            float fg = v0->fog + f_dx * dx0 + f_dy * dy0;
            for (int x = xa; x <= xb; ++x, iw += iw_dx, uw += uw_dx, vw += vw_dx, lr += r_dx, lg += g_dx,
                     lb += b_dx, fg += f_dx) {
                if (iw <= 0 || (translucent ? iw <= zrow[x] : iw < zrow[x])) continue;

                float cr = base_r, cg = base_g, cb = base_b, ca = p.a;
                if (p.textured) {
                    const float w = 1.0f / iw;
                    const float un = uw * w, vn = vw * w;  // normalized texture coordinates
                    // Mipmap level from the screen-space texel footprint.
                    int level = 0;
                    if (max_level > 0) {
                        const float dudx = (uw_dx - un * iw_dx) * w * tw, dvdx = (vw_dx - vn * iw_dx) * w * th;
                        const float dudy = (uw_dy - un * iw_dy) * w * tw, dvdy = (vw_dy - vn * iw_dy) * w * th;
                        const float rho2 = std::max(dudx * dudx + dvdx * dvdx, dudy * dudy + dvdy * dvdy);
                        if (rho2 >= 4.0f && std::isfinite(rho2)) level = std::min(max_level, std::ilogb(rho2) >> 1);
                    }
                    const int lw = p.tw >> level, lh = p.th >> level;
                    const int bx = kMipXBase[level] + (p.tx >> level);
                    const int by = kMipYBase[level] + (p.ty >> level);
                    const int mx = 2047 >> level, my = 1023 >> level;
                    float u = un * float(lw) - 0.5f;
                    float v = vn * float(lh) - 0.5f;
                    u = std::clamp(u, -1.0e9f, 1.0e9f);
                    v = std::clamp(v, -1.0e9f, 1.0e9f);
                    const float fu = std::floor(u), fv = std::floor(v);
                    const int iu = int(int64_t(fu) & 0x3fffffff), iv = int(int64_t(fv) & 0x3fffffff);
                    const uint32_t wu = uint32_t((u - fu) * 256.0f), wv = uint32_t((v - fv) * 256.0f);
                    const int tx0 = kMipXBase[level] + ((bx - kMipXBase[level] + wrap_texel(iu, lw, p.mirror_u)) & mx);
                    const int tx1 = kMipXBase[level] + ((bx - kMipXBase[level] + wrap_texel(iu + 1, lw, p.mirror_u)) & mx);
                    const int ty0 = page_base + kMipYBase[level] + ((by - kMipYBase[level] + wrap_texel(iv, lh, p.mirror_v)) & my);
                    const int ty1 = page_base + kMipYBase[level] + ((by - kMipYBase[level] + wrap_texel(iv + 1, lh, p.mirror_v)) & my);
                    const uint32_t t00 = lut[tex[size_t(ty0 & 2047) * 2048 + size_t(tx0 & 2047)]];
                    const uint32_t t10 = lut[tex[size_t(ty0 & 2047) * 2048 + size_t(tx1 & 2047)]];
                    const uint32_t t01 = lut[tex[size_t(ty1 & 2047) * 2048 + size_t(tx0 & 2047)]];
                    const uint32_t t11 = lut[tex[size_t(ty1 & 2047) * 2048 + size_t(tx1 & 2047)]];
                    const uint32_t tc = lerp_rgba(lerp_rgba(t00, t10, wu), lerp_rgba(t01, t11, wu), wv);
                    float ta = float(tc >> 24) * (1.0f / 255.0f);
                    if (p.alpha_test && ta < 32.0f / 255.0f) continue;
                    if (p.texture_alpha) {
                        if (!translucent) {
                            if (ta < 1.0f) continue;
                        } else if (ta * p.a >= 1.0f) {
                            continue;
                        }
                    }
                    if (!p.texture_alpha && !p.alpha_test) ta = 1.0f;
                    const uint32_t trgb = p.inverted ? ~tc : tc;
                    cr *= float((trgb >> 16) & 0xff) * (1.0f / 255.0f);
                    cg *= float((trgb >> 8) & 0xff) * (1.0f / 255.0f);
                    cb *= float(trgb & 0xff) * (1.0f / 255.0f);
                    ca *= ta;
                }
                if (ca < 1.0f / 32.0f) continue;
                if (p.lighting) {
                    cr *= lr;
                    cg *= lg;
                    cb *= lb;
                }
                cr = std::clamp(cr, 0.0f, 255.0f);
                cg = std::clamp(cg, 0.0f, 255.0f);
                cb = std::clamp(cb, 0.0f, 255.0f);
                const float fog = std::clamp(fg, 0.0f, 1.0f);
                if (fog > 0) {
                    cr += (fog_r - cr) * fog;
                    cg += (fog_g - cg) * fog;
                    cb += (fog_b - cb) * fog;
                }
                if (!translucent) {
                    zrow[x] = iw;
                    row[x] = 0xff000000u | uint32_t(cr + 0.5f) << 16 | uint32_t(cg + 0.5f) << 8 | uint32_t(cb + 0.5f);
                } else {
                    // Premultiplied "over" blend.
                    const float a = std::min(ca, 1.0f), ia = 1.0f - a;
                    const uint32_t d = row[x];
                    const float oa = a * 255.0f + float(d >> 24) * ia;
                    const float orr = cr * a + float((d >> 16) & 0xff) * ia;
                    const float og = cg * a + float((d >> 8) & 0xff) * ia;
                    const float ob = cb * a + float(d & 0xff) * ia;
                    row[x] = uint32_t(std::min(oa, 255.0f) + 0.5f) << 24 | uint32_t(std::min(orr, 255.0f) + 0.5f) << 16 |
                             uint32_t(std::min(og, 255.0f) + 0.5f) << 8 | uint32_t(std::min(ob, 255.0f) + 0.5f);
                }
            }
        }
    }
}

void Real3D::render(std::vector<uint32_t>& out) {
    prepare();
    rasterize(out);
}

void Real3D::prepare() {
    stats_polys_ = 0;
    stats_viewports_ = 0;
    cur_polys_ = 0;
    viewports_.clear();
    for (auto& l : frame_polys_) l.clear();
    prepared_blocked_ = block_culling_;
    if (block_culling_) return;
    sun_clamp_ = (modeword_[3] & 0x40000) == 0;  // Mars
    for (int pri = 0; pri < 4; ++pri) render_viewport(0x800000, pri, 0);
    for (const auto& l : frame_polys_) stats_polys_ += int(l.size());
}

void Real3D::rasterize(std::vector<uint32_t>& out) {
    out.assign(size_t(kWidth) * kHeight, 0);
    std::fill(colour_.begin(), colour_.end(), 0);
    if (prepared_blocked_) return;

    // Scroll fog: lowest priority viewport with the highest value fills the background.
    for (int pri = 0; pri < 4; ++pri) {
        const ViewportState* best = nullptr;
        for (const auto& vs : viewports_)
            if (vs.priority == pri && vs.scroll_fog > 0 && (best == nullptr || vs.scroll_fog > best->scroll_fog))
                best = &vs;
        if (best == nullptr) continue;
        const float a = best->scroll_fog;
        const uint32_t px = uint32_t(a * 255.0f + 0.5f) << 24 |
                            uint32_t(best->fog_rgb[0] * best->fog_ambient * a * 255.0f + 0.5f) << 16 |
                            uint32_t(best->fog_rgb[1] * best->fog_ambient * a * 255.0f + 0.5f) << 8 |
                            uint32_t(best->fog_rgb[2] * best->fog_ambient * a * 255.0f + 0.5f);
        const int x0 = std::max(0, int(best->x)), x1 = std::min(kWidth, int(best->x + best->w));
        const int y0 = std::max(0, int(best->y)), y1 = std::min(kHeight, int(best->y + best->h));
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) colour_[size_t(y) * kWidth + size_t(x)] = px;
        break;
    }

    // Horizontal bands rendered in parallel; each band processes every
    // polygon in order, so the result does not depend on the band count.
    const int bands = std::clamp(band_count_, 1, 16);
    auto band = [this, bands](int index) {
        const int y0 = kHeight * index / bands;
        const int y1 = kHeight * (index + 1) / bands;
        for (int pri = 0; pri < 4; ++pri) {
            const auto& list = frame_polys_[size_t(pri)];
            if (list.empty()) continue;
            for (int overlay = 0; overlay < 2; ++overlay) {
                std::fill(zbuf_.begin() + ptrdiff_t(y0) * kWidth, zbuf_.begin() + ptrdiff_t(y1) * kWidth, 0.0f);
                bool any = false;
                for (const auto& d : list) {
                    if (d.p.high_priority != (overlay == 1)) continue;
                    any = true;
                    if (d.p.poly_alpha || d.p.node_alpha) continue;
                    raster_poly(d.v, d.n, d.p, false, y0, y1);
                }
                if (!any) continue;
                for (const auto& d : list) {
                    if (d.p.high_priority != (overlay == 1)) continue;
                    if (!d.p.texture_alpha && !d.p.poly_alpha && !d.p.node_alpha) continue;
                    raster_poly(d.v, d.n, d.p, true, y0, y1);
                }
            }
        }
    };
    if (bands == 1) {
        band(0);
    } else {
        std::vector<std::thread> threads;
        for (int i = 1; i < bands; ++i) threads.emplace_back(band, i);
        band(0);
        for (auto& t : threads) t.join();
    }
    out = colour_;
}

}  // namespace dsp
