#include "drivers/consoles/sega32x.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kBiosG = {{"32x_g_bios.bin", 0x100, 0x0000, 0}};
const std::vector<RomEntry> kBiosM = {{"32x_m_bios.bin", 0x800, 0x0000, 0}};
const std::vector<RomEntry> kBiosS = {{"32x_s_bios.bin", 0x400, 0x0000, 0}};

// Pending interrupt bits (same order as the $4000 mask bits).
constexpr int kIrqPwm = 0;
constexpr int kIrqCmd = 1;
constexpr int kIrqH = 2;
constexpr int kIrqV = 3;
constexpr int kIrqVres = 4;
constexpr int kIrqLevel[5] = {6, 8, 10, 12, 14};

inline uint16_t be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }

inline uint32_t rgb555(uint16_t c) {
    const uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return 0xff000000u | ((r * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (b * 255 / 31);
}

inline uint16_t merge(uint16_t old_value, uint16_t v, uint16_t mask) {
    return uint16_t((old_value & ~mask) | (v & mask));
}

}  // namespace

// ---------------------------------------------------------------------------
// SH-2 bus

uint8_t Sega32X::Bus::read8(uint32_t a) { return owner_->sh2_read8(cpu_, a); }
uint16_t Sega32X::Bus::read16(uint32_t a) { return owner_->sh2_read16(cpu_, a); }
uint32_t Sega32X::Bus::read32(uint32_t a) {
    const uint32_t hi = owner_->sh2_read16(cpu_, a);
    return (hi << 16) | owner_->sh2_read16(cpu_, a + 2);
}
void Sega32X::Bus::write8(uint32_t a, uint8_t v) { owner_->sh2_write8(cpu_, a, v); }
void Sega32X::Bus::write16(uint32_t a, uint16_t v) { owner_->sh2_write16(cpu_, a, v); }
void Sega32X::Bus::write32(uint32_t a, uint32_t v) {
    owner_->sh2_write16(cpu_, a, uint16_t(v >> 16));
    owner_->sh2_write16(cpu_, a + 2, uint16_t(v));
}
bool Sega32X::Bus::dreq(int channel) {
    if (cpu_ != 0) return false;
    if (channel == 0) return owner_->fifo_count_ > 0 && (owner_->dreq_ctl_ & 4) != 0;
    return false;
}

// ---------------------------------------------------------------------------

Sega32X::Sega32X(Region region) : Genesis(region) {
    has_ext_ = true;
    for (Sh2* cpu : {&master_, &slave_}) cpu->map_memory(6, sdram_.data(), kSdramSize, true);
}

bool Sega32X::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    bios_g_.assign(0x100, 0xff);
    bios_m_.assign(0x800, 0xff);
    bios_s_.assign(0x400, 0xff);
    if (!loader.load(kBiosG, bios_g_, error) || !loader.load(kBiosM, bios_m_, error) ||
        !loader.load(kBiosS, bios_s_, error)) {
        if (error && error->empty()) *error = "32X BIOS not found in " + rom_path;
        return false;
    }
    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    bios_loaded_ = true;
    reset();
    return true;
}

bool Sega32X::load_media(const std::string& path, std::string* error) {
    if (!Genesis::load_media(path, error)) return false;
    // SH-2 view of the cartridge: a 4 MiB window, mirrored.
    uint32_t size = 0x100000;
    while (size < rom_.size() && size < 0x400000) size <<= 1;
    cart_.assign(size, 0xff);
    std::memcpy(cart_.data(), rom_.data(), std::min<size_t>(rom_.size(), size));
    for (Sh2* cpu : {&master_, &slave_}) cpu->map_memory(2, cart_.data(), size, false);
    reset();
    return true;
}

void Sega32X::reset() {
    aden_ = false;
    sh2_running_ = false;
    fm_ = false;
    rv_ = false;
    int_ctl_ = 0;
    bank_ = 0;
    int_mask_.fill(0);
    hcount_ = 0;
    hint_counter_ = 0;
    pending_.fill(0);
    comm_.fill(0);
    sega_tv_ = 0;
    dreq_ctl_ = 0;
    dreq_src_ = dreq_dst_ = 0;
    dreq_len_ = 0;
    dreq_left_ = 0;
    fifo_head_ = fifo_count_ = 0;
    bitmap_mode_ = 0;
    shift_ = fill_len_ = fill_addr_ = fill_data_ = 0;
    fs_ = 0;
    fs_pending_ = -1;
    vblank_ = hblank_ = false;
    pwm_ctl_ = pwm_cycle_ = 0;
    pwm_count_.fill(0);
    pwm_out_.fill(0);
    pwm_acc_ = 0;
    pwm_tm_ = 0;
    pwm_dc_ = 0;
    sh2_debt_ = 0;
    sdram_.fill(0);
    for (auto& d : dram_) d.fill(0);
    palette_.fill(0);
    master_.set_irl(0);
    slave_.set_irl(0);
    Genesis::reset();
}

// ---------------------------------------------------------------------------
// Interrupts, reset, FIFO

void Sega32X::update_irls() {
    for (int cpu = 0; cpu < 2; cpu++) {
        const uint16_t active = uint16_t(pending_[size_t(cpu)] & ((int_mask_[size_t(cpu)] & 0x0f) | 0x10));
        int level = 0;
        for (int bit = 4; bit >= 0; bit--) {
            if (active & (1 << bit)) {
                level = kIrqLevel[bit];
                break;
            }
        }
        (cpu == 0 ? master_ : slave_).set_irl(level);
    }
}

void Sega32X::raise_irq(int bit) {
    for (int cpu = 0; cpu < 2; cpu++) pending_[size_t(cpu)] = uint16_t(pending_[size_t(cpu)] | (1 << bit));
    update_irls();
}

void Sega32X::sh2_reset_line(bool released) {
    if (released && !sh2_running_ && bios_loaded_) {
        master_.reset();
        slave_.reset();
        sh2_running_ = true;
    } else if (!released) {
        sh2_running_ = false;
    }
}

void Sega32X::fifo_push(uint16_t v) {
    if ((dreq_ctl_ & 4) == 0 || fifo_count_ >= 8) return;
    fifo_[size_t((fifo_head_ + fifo_count_) & 7)] = v;
    fifo_count_++;
    if (sh2_running_) master_.dma_request(0);
}

uint16_t Sega32X::fifo_pop() {
    if (fifo_count_ == 0) return 0;
    const uint16_t v = fifo_[size_t(fifo_head_)];
    fifo_head_ = (fifo_head_ + 1) & 7;
    fifo_count_--;
    if (dreq_left_ > 0 && --dreq_left_ == 0) dreq_ctl_ = uint16_t(dreq_ctl_ & ~4);
    return v;
}

// ---------------------------------------------------------------------------
// Registers

uint16_t Sega32X::sys_read(int cpu, uint32_t o) {
    o &= 0x3e;
    if (o >= 0x20 && o < 0x30) return comm_[(o - 0x20) >> 1];
    if (o >= 0x30) return pwm_read(o);
    switch (o) {
        case 0x00:
            return uint16_t((fm_ ? 0x8000 : 0) | (aden_ ? 0x0200 : 0) | int_mask_[size_t(cpu)]);
        case 0x04: return hcount_;
        case 0x06: return uint16_t((fifo_count_ >= 8 ? 0x4000 : 0) | (dreq_ctl_ & 6) | (rv_ ? 1 : 0));
        case 0x08: return uint16_t((dreq_src_ >> 16) & 0xff);
        case 0x0a: return uint16_t(dreq_src_);
        case 0x0c: return uint16_t((dreq_dst_ >> 16) & 0xff);
        case 0x0e: return uint16_t(dreq_dst_);
        case 0x10: return dreq_len_;
        case 0x12: return fifo_pop();
        default: return 0;
    }
}

void Sega32X::sys_write(int cpu, uint32_t o, uint16_t v, uint16_t mask) {
    o &= 0x3e;
    if (o >= 0x20 && o < 0x30) {
        uint16_t& c = comm_[(o - 0x20) >> 1];
        c = merge(c, v, mask);
        return;
    }
    if (o >= 0x30) {
        pwm_write(o, v, mask);
        return;
    }
    switch (o) {
        case 0x00:
            if (mask & 0xff00) fm_ = (v & 0x8000) != 0;
            if (mask & 0x00ff) int_mask_[size_t(cpu)] = uint16_t(v & 0x8f);
            update_irls();
            return;
        case 0x04:
            if (mask & 0x00ff) hcount_ = uint16_t(v & 0xff);
            return;
        case 0x14:
        case 0x16:
        case 0x18:
        case 0x1a:
        case 0x1c: {
            static const int kBit[5] = {kIrqVres, kIrqV, kIrqH, kIrqCmd, kIrqPwm};
            const int bit = kBit[(o - 0x14) >> 1];
            pending_[size_t(cpu)] = uint16_t(pending_[size_t(cpu)] & ~(1 << bit));
            if (bit == kIrqCmd) int_ctl_ = uint16_t(int_ctl_ & ~(1 << cpu));
            update_irls();
            return;
        }
        default: return;
    }
}

uint16_t Sega32X::m68k_reg_read(uint32_t o) {
    o &= 0x3e;
    if (o >= 0x20 && o < 0x30) return comm_[(o - 0x20) >> 1];
    if (o >= 0x30) return pwm_read(o);
    switch (o) {
        case 0x00:
            return uint16_t((fm_ ? 0x8000 : 0) | 0x0080 | (sh2_running_ ? 0x0002 : 0) | (aden_ ? 0x0001 : 0));
        case 0x02: return int_ctl_;
        case 0x04: return bank_;
        case 0x06: return uint16_t((fifo_count_ >= 8 ? 0x0080 : 0) | (dreq_ctl_ & 6) | (rv_ ? 1 : 0));
        case 0x08: return uint16_t((dreq_src_ >> 16) & 0xff);
        case 0x0a: return uint16_t(dreq_src_);
        case 0x0c: return uint16_t((dreq_dst_ >> 16) & 0xff);
        case 0x0e: return uint16_t(dreq_dst_);
        case 0x10: return dreq_len_;
        case 0x1a: return sega_tv_;
        default: return 0;
    }
}

void Sega32X::m68k_reg_write(uint32_t o, uint16_t v, uint16_t mask) {
    o &= 0x3e;
    if (o >= 0x20 && o < 0x30) {
        uint16_t& c = comm_[(o - 0x20) >> 1];
        c = merge(c, v, mask);
        return;
    }
    if (o >= 0x30) {
        pwm_write(o, v, mask);
        return;
    }
    switch (o) {
        case 0x00:
            if (mask & 0xff00) fm_ = (v & 0x8000) != 0;
            if (mask & 0x00ff) {
                if (v & 1) aden_ = true;  // only a reset clears ADEN
                sh2_reset_line((v & 2) != 0);
            }
            return;
        case 0x02:
            if (mask & 0x00ff) {
                int_ctl_ = uint16_t(v & 3);
                for (int cpu = 0; cpu < 2; cpu++) {
                    if (v & (1 << cpu)) pending_[size_t(cpu)] = uint16_t(pending_[size_t(cpu)] | (1 << kIrqCmd));
                }
                update_irls();
            }
            return;
        case 0x04:
            if (mask & 0x00ff) bank_ = uint16_t(v & 3);
            return;
        case 0x06:
            if (mask & 0x00ff) {
                rv_ = (v & 1) != 0;
                const bool start = (v & 4) != 0 && (dreq_ctl_ & 4) == 0;
                dreq_ctl_ = uint16_t(v & 6);
                if (start) {
                    fifo_head_ = fifo_count_ = 0;
                    dreq_left_ = dreq_len_;
                }
                if ((v & 4) == 0) fifo_head_ = fifo_count_ = 0;
            }
            return;
        case 0x08: dreq_src_ = (dreq_src_ & 0xffff) | (uint32_t(v & 0xff) << 16); return;
        case 0x0a: dreq_src_ = (dreq_src_ & 0xff0000) | (v & 0xfffe); return;
        case 0x0c: dreq_dst_ = (dreq_dst_ & 0xffff) | (uint32_t(v & 0xff) << 16); return;
        case 0x0e: dreq_dst_ = (dreq_dst_ & 0xff0000) | v; return;
        case 0x10: dreq_len_ = uint16_t(v & 0xfffc); return;
        case 0x12: fifo_push(v); return;
        case 0x1a: sega_tv_ = uint16_t(v & 1); return;
        default: return;
    }
}

uint16_t Sega32X::vdp_read(uint32_t o) {
    switch (o & 0xe) {
        case 0x0: return uint16_t((pal_ ? 0 : 0x8000) | bitmap_mode_);
        case 0x2: return shift_;
        case 0x4: return fill_len_;
        case 0x6: return fill_addr_;
        case 0x8: return fill_data_;
        default: {
            const bool pen = vblank_ || hblank_ || (bitmap_mode_ & 3) == 0;
            return uint16_t((vblank_ ? 0x8000 : 0) | (hblank_ ? 0x4000 : 0) | (pen ? 0x2000 : 0) | fs_);
        }
    }
}

void Sega32X::vdp_write(uint32_t o, uint16_t v, uint16_t mask) {
    switch (o & 0xe) {
        case 0x0: bitmap_mode_ = uint16_t(merge(bitmap_mode_, v, mask) & 0xc3); return;
        case 0x2: shift_ = uint16_t(merge(shift_, v, mask) & 1); return;
        case 0x4: fill_len_ = uint16_t(merge(fill_len_, v, mask) & 0xff); return;
        case 0x6: fill_addr_ = merge(fill_addr_, v, mask); return;
        case 0x8: {
            fill_data_ = merge(fill_data_, v, mask);
            // Auto fill: len + 1 words, the address wraps in its 256-word row.
            uint8_t* d = dram_[size_t(fs_ ^ 1)].data();
            uint16_t addr = fill_addr_;
            for (int i = 0; i <= fill_len_; i++) {
                d[(uint32_t(addr) * 2) & 0x1fffe] = uint8_t(fill_data_ >> 8);
                d[((uint32_t(addr) * 2) & 0x1fffe) + 1] = uint8_t(fill_data_);
                addr = uint16_t((addr & 0xff00) | ((addr + 1) & 0xff));
            }
            fill_addr_ = addr;
            return;
        }
        default:
            if (mask & 0x00ff) {
                const int fs = v & 1;
                if (vblank_ || (bitmap_mode_ & 3) == 0) {
                    fs_ = fs;
                    fs_pending_ = -1;
                } else {
                    fs_pending_ = fs;
                }
            }
            return;
    }
}

uint16_t Sega32X::pwm_read(uint32_t o) {
    auto status = [&](int ch) {
        uint16_t s = 0;
        if (pwm_count_[size_t(ch)] >= 3) s |= 0x8000;
        if (pwm_count_[size_t(ch)] == 0) s |= 0x4000;
        return s;
    };
    switch (o & 0xe) {
        case 0x0: return pwm_ctl_;
        case 0x2: return pwm_cycle_;
        case 0x4: return status(0);
        case 0x6: return status(1);
        case 0x8: return uint16_t(status(0) & status(1));
        default: return 0;
    }
}

void Sega32X::pwm_write(uint32_t o, uint16_t v, uint16_t mask) {
    auto push = [&](int ch, uint16_t value) {
        auto& f = pwm_fifo_[size_t(ch)];
        int& n = pwm_count_[size_t(ch)];
        if (n < 3) f[size_t(n++)] = uint16_t(value & 0xfff);
        else f[2] = uint16_t(value & 0xfff);
    };
    switch (o & 0xe) {
        case 0x0:
            pwm_ctl_ = uint16_t(merge(pwm_ctl_, v, mask) & 0x0f8f);
            pwm_tm_ = 0;
            return;
        case 0x2: pwm_cycle_ = uint16_t(merge(pwm_cycle_, v, mask) & 0xfff); return;
        case 0x4: push(0, v); return;
        case 0x6: push(1, v); return;
        case 0x8:
            push(0, v);
            push(1, v);
            return;
        default: return;
    }
}

void Sega32X::pwm_advance(int sh2_cycles) {
    const int period = int((pwm_cycle_ - 1) & 0xfff);
    if (period < 16) return;
    pwm_acc_ += sh2_cycles;
    while (pwm_acc_ >= period) {
        pwm_acc_ -= period;
        for (int ch = 0; ch < 2; ch++) {
            int& n = pwm_count_[size_t(ch)];
            if (n == 0) continue;
            auto& f = pwm_fifo_[size_t(ch)];
            pwm_out_[size_t(ch)] = f[0];
            f[0] = f[1];
            f[1] = f[2];
            n--;
        }
        if (--pwm_tm_ <= 0) {
            const int tm = (pwm_ctl_ >> 8) & 15;
            pwm_tm_ = tm ? tm : 16;
            raise_irq(kIrqPwm);
        }
    }
}

int32_t Sega32X::ext_audio_sample() {
    const int period = int((pwm_cycle_ - 1) & 0xfff);
    int32_t sum = 0;
    if (period >= 16) {
        // Pulse widths above the cycle saturate at full scale.
        if (pwm_ctl_ & 3) sum += std::min(pwm_out_[0], period);
        if (pwm_ctl_ & 0xc) sum += std::min(pwm_out_[1], period);
        sum = sum * 6000 / period;
    }
    // The PWM output idles at a DC level (half the cycle, or wherever the
    // last sample left it): a one-pole high-pass keeps it out of the mix.
    pwm_dc_ += (sum * 1024 - pwm_dc_) >> 9;
    return sum - int32_t(pwm_dc_ >> 10);
}

// ---------------------------------------------------------------------------
// Frame buffer

uint16_t Sega32X::fb_read16(uint32_t offset) const {
    const uint8_t* d = dram_[size_t(fs_ ^ 1)].data() + (offset & 0x1fffe);
    return be16(d);
}

void Sega32X::fb_write16(uint32_t offset, uint16_t v, uint16_t mask, bool overwrite) {
    uint8_t* d = dram_[size_t(fs_ ^ 1)].data() + (offset & 0x1fffe);
    // Overwrite image: zero bytes leave the frame buffer as it is.
    if ((mask & 0xff00) && !(overwrite && (v & 0xff00) == 0)) d[0] = uint8_t(v >> 8);
    if ((mask & 0x00ff) && !(overwrite && (v & 0x00ff) == 0)) d[1] = uint8_t(v);
}

// ---------------------------------------------------------------------------
// SH-2 address space (bits 28-0)

uint16_t Sega32X::sh2_read16(int cpu, uint32_t a) {
    a &= 0x1ffffffe;
    if (a < 0x4000) {
        const std::vector<uint8_t>& bios = cpu == 0 ? bios_m_ : bios_s_;
        if (bios.empty()) return 0xffff;
        return be16(&bios[a & (bios.size() - 1)]);
    }
    if (a < 0x4100) return sys_read(cpu, a & 0xff);
    if (a < 0x4200) return vdp_read(a & 0xf);
    if (a < 0x4400) return palette_[(a & 0x1ff) >> 1];
    const uint32_t page = a >> 24;
    if (page == 2) return cart_.empty() ? 0xffff : be16(&cart_[a & (cart_.size() - 1)]);
    if (page == 4) return fb_read16(a & 0x1ffff);
    if (page == 6) return be16(&sdram_[a & (kSdramSize - 1)]);
    return 0;
}

uint8_t Sega32X::sh2_read8(int cpu, uint32_t a) {
    const uint16_t w = sh2_read16(cpu, a);
    return (a & 1) ? uint8_t(w) : uint8_t(w >> 8);
}

void Sega32X::sh2_write16(int cpu, uint32_t a, uint16_t v) {
    a &= 0x1ffffffe;
    if (a < 0x4000) return;
    if (a < 0x4100) {
        sys_write(cpu, a & 0xff, v, 0xffff);
        return;
    }
    if (a < 0x4200) {
        vdp_write(a & 0xf, v, 0xffff);
        return;
    }
    if (a < 0x4400) {
        palette_[(a & 0x1ff) >> 1] = v;
        return;
    }
    const uint32_t page = a >> 24;
    if (page == 4) {
        fb_write16(a & 0x1ffff, v, 0xffff, (a & 0x20000) != 0);
        return;
    }
    if (page == 6) {
        sdram_[a & (kSdramSize - 1)] = uint8_t(v >> 8);
        sdram_[(a & (kSdramSize - 1)) + 1] = uint8_t(v);
    }
}

void Sega32X::sh2_write8(int cpu, uint32_t a, uint8_t v) {
    a &= 0x1fffffff;
    const uint16_t mask = (a & 1) ? 0x00ff : 0xff00;
    const uint16_t w = (a & 1) ? v : uint16_t(v << 8);
    const uint32_t even = a & ~1u;
    if (even < 0x4000) return;
    if (even < 0x4100) {
        sys_write(cpu, even & 0xff, w, mask);
        return;
    }
    if (even < 0x4200) {
        vdp_write(even & 0xf, w, mask);
        return;
    }
    if (even < 0x4400) {
        uint16_t& p = palette_[(even & 0x1ff) >> 1];
        p = merge(p, w, mask);
        return;
    }
    const uint32_t page = a >> 24;
    if (page == 4) {
        fb_write16(even & 0x1ffff, w, mask, (a & 0x20000) != 0);
        return;
    }
    if (page == 6) sdram_[a & (kSdramSize - 1)] = v;
}

// ---------------------------------------------------------------------------
// 68000 side

bool Sega32X::ext_read16(uint32_t a, uint16_t* value) {
    if (a >= 0xa15100 && a < 0xa15400) {
        if (a < 0xa15180) *value = m68k_reg_read(a - 0xa15100);
        else if (a < 0xa15200) *value = vdp_read(a & 0xf);
        else *value = palette_[(a & 0x1ff) >> 1];
        return true;
    }
    if (a == 0xa130ec) {
        *value = 0x4d41;  // "MA"
        return true;
    }
    if (a == 0xa130ee) {
        *value = 0x5253;  // "RS"
        return true;
    }
    if (!aden_) return false;
    auto rom16 = [&](uint32_t off) {
        return off + 1 < rom_.size() ? be16(&rom_[off]) : uint16_t(0xffff);
    };
    if (a < 0x100 && !rv_) {
        *value = bios_g_.size() >= 0x100 ? be16(&bios_g_[a]) : uint16_t(0xffff);
        return true;
    }
    if (a >= 0x840000 && a < 0x880000) {
        *value = fb_read16(a & 0x1ffff);
        return true;
    }
    if (a >= 0x880000 && a < 0x900000) {
        *value = rom16(a - 0x880000);
        return true;
    }
    if (a >= 0x900000 && a < 0xa00000) {
        *value = rom16(uint32_t(bank_) * 0x100000 + (a - 0x900000));
        return true;
    }
    return false;
}

bool Sega32X::ext_read8(uint32_t a, uint8_t* value) {
    const bool mine = (a >= 0xa15100 && a < 0xa15400) || (a & ~3u) == 0xa130ec ||
                      (aden_ && ((a < 0x100 && !rv_) || (a >= 0x840000 && a < 0xa00000)));
    if (!mine) return false;
    uint16_t w = 0;
    ext_read16(a & ~1u, &w);
    *value = (a & 1) ? uint8_t(w) : uint8_t(w >> 8);
    return true;
}

bool Sega32X::ext_write16(uint32_t a, uint16_t v) {
    if (a >= 0xa15100 && a < 0xa15400) {
        if (a < 0xa15180) m68k_reg_write(a - 0xa15100, v, 0xffff);
        else if (a < 0xa15200) vdp_write(a & 0xf, v, 0xffff);
        else palette_[(a & 0x1ff) >> 1] = v;
        return true;
    }
    if (!aden_) return false;
    if (a < 0x100 && !rv_) return true;
    if (a >= 0x840000 && a < 0x880000) {
        fb_write16(a & 0x1ffff, v, 0xffff, a >= 0x860000);
        return true;
    }
    return a >= 0x880000 && a < 0xa00000;
}

bool Sega32X::ext_write8(uint32_t a, uint8_t v) {
    const uint16_t mask = (a & 1) ? 0x00ff : 0xff00;
    const uint16_t w = (a & 1) ? v : uint16_t(v << 8);
    const uint32_t even = a & ~1u;
    if (even >= 0xa15100 && even < 0xa15400) {
        if (even < 0xa15180) {
            m68k_reg_write(even - 0xa15100, w, mask);
        } else if (even < 0xa15200) {
            vdp_write(even & 0xf, w, mask);
        } else {
            uint16_t& p = palette_[(even & 0x1ff) >> 1];
            p = merge(p, w, mask);
        }
        return true;
    }
    if (!aden_) return false;
    if (even < 0x100 && !rv_) return true;
    if (even >= 0x840000 && even < 0x880000) {
        fb_write16(even & 0x1ffff, w, mask, even >= 0x860000);
        return true;
    }
    return even >= 0x880000 && even < 0xa00000;
}

// ---------------------------------------------------------------------------
// Video and frame loop

void Sega32X::render_32x_line(int line, uint32_t* out, const uint8_t* backdrop) {
    const int mode = bitmap_mode_ & 3;
    if (mode == 0) return;
    const uint8_t* d = dram_[size_t(fs_)].data();
    const uint32_t lt = be16(d + (uint32_t(line) * 2 & 0x1ff));
    const uint16_t inv = (bitmap_mode_ & 0x80) ? 0x8000 : 0;
    auto put = [&](int x, uint16_t c) {
        if (backdrop[x] || ((c ^ inv) & 0x8000)) out[x] = rgb555(c);
    };
    if (mode == 1) {  // packed pixel: one palette index per byte
        const uint32_t base = lt * 2 + (shift_ & 1);
        for (int x = 0; x < 320; x++) put(x, palette_[d[(base + uint32_t(x)) & 0x1ffff]]);
    } else if (mode == 2) {  // direct colour
        for (int x = 0; x < 320; x++) put(x, be16(d + ((lt + uint32_t(x)) * 2 & 0x1fffe)));
    } else {  // run length: colour index, length - 1
        uint32_t addr = lt * 2;
        int x = 0;
        while (x < 320) {
            const uint16_t w = be16(d + (addr & 0x1fffe));
            addr += 2;
            const uint16_t c = palette_[w >> 8];
            for (int i = 0; i <= (w & 0xff) && x < 320; i++) put(x++, c);
        }
    }
}

void Sega32X::run_frame() {
    const int lines = vdp_.total_scanlines();
    const double fps = frames_per_second();
    const int m68k_per_line = std::max(1, int(double(m68k_clock()) / fps / lines));
    const int z80_per_line = std::max(1, int(double(z80_clock()) / fps / lines));
    const double sh2_per_line = double(sh2_clock()) / fps / lines;
    const int height = vdp_.screen_height();
    if (int(framebuffer_.size()) < kScreenWidth * height) {
        framebuffer_.assign(size_t(kScreenWidth) * size_t(height), 0xff000000u);
    }
    constexpr int kSlices = 8;

    for (int line = 0; line < lines; line++) {
        cycles_on_line_ = 0;
        vdp_.handle_scanline(line);
        if (line < height) {
            uint32_t* row = &framebuffer_[size_t(line) * kScreenWidth];
            std::memcpy(row, vdp_.line_buffer(), size_t(kScreenWidth) * sizeof(uint32_t));
            if (aden_) render_32x_line(line, row, vdp_.line_backdrop());
        }
        if (line == 0) {
            vblank_ = false;
            hint_counter_ = hcount_;
        }
        if (line == height) {
            vblank_ = true;
            if (fs_pending_ >= 0) {
                fs_ = fs_pending_;
                fs_pending_ = -1;
            }
            raise_irq(kIrqV);
        }
        const bool hen = ((int_mask_[0] | int_mask_[1]) & 0x80) != 0;
        if (line < height || hen) {
            if (hint_counter_ <= 0) {
                hint_counter_ = hcount_;
                raise_irq(kIrqH);
            } else {
                hint_counter_--;
            }
        }
        for (int s = 0; s < kSlices; s++) {
            hblank_ = s == kSlices - 1;
            m68k_.run(m68k_per_line / kSlices);
            sh2_debt_ += int64_t(sh2_per_line * 1024.0 / kSlices);
            const int sh2_cycles = int(sh2_debt_ >> 10);
            sh2_debt_ -= int64_t(sh2_cycles) << 10;
            if (sh2_running_) {
                master_.run(sh2_cycles);
                slave_.run(sh2_cycles);
            }
            pwm_advance(sh2_cycles);
        }
        hblank_ = false;
        if (!z80_is_reset_ && z80_has_bus_) z80_.run(z80_per_line);
    }
    vdp_.handle_eof();
}

}  // namespace dsp
