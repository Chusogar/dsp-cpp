#include "cpu/sh2.h"

#include <algorithm>
#include <cstring>

namespace dsp {
namespace {

constexpr uint32_t kT = 0x001;
constexpr uint32_t kS = 0x002;
constexpr uint32_t kQ = 0x100;
constexpr uint32_t kM = 0x200;
constexpr uint32_t kSrMask = 0x3f3;

inline uint32_t sext8(uint32_t v) { return uint32_t(int32_t(int8_t(v))); }
inline uint32_t sext16(uint32_t v) { return uint32_t(int32_t(int16_t(v))); }
inline uint32_t sext12(uint32_t v) { return (v & 0x800) ? (v | 0xfffff000u) : (v & 0xfff); }

// On-chip register offsets from $FFFFFE00.
constexpr uint32_t kTier = 0x10, kFtcsr = 0x11, kFrcH = 0x12, kFrcL = 0x13, kOcrH = 0x14,
                   kOcrL = 0x15, kTcr = 0x16, kTocr = 0x17;
constexpr uint32_t kIprb = 0x60, kVcrc = 0x66, kVcrd = 0x68;
constexpr uint32_t kIpra = 0xe2, kVcrwdt = 0xe4;

}  // namespace

void Sh2::map_memory(int page, uint8_t* data, uint32_t size, bool writable) {
    FastPage& p = pages_[size_t(page) & 31];
    p.data = data;
    p.mask = size - 1;
    p.writable = writable;
}

void Sh2::reset() {
    r_.fill(0);
    sr_ = 0xf0;
    vbr_ = 0;
    gbr_ = 0;
    pr_ = 0;
    mach_ = macl_ = 0;
    sleeping_ = false;
    in_slot_ = false;
    regs_.fill(0);
    frc_ = 0;
    ocra_ = ocrb_ = 0xffff;
    ftcsr_ = 0;
    frt_div_ = 0;
    wtcsr_ = 0x18;
    wtcnt_ = 0;
    wdt_div_ = 0;
    dvsr_ = dvdnth_ = dvdntl_ = dvcr_ = 0;
    for (DmaChannel& c : dma_) c = DmaChannel{};
    dmaor_ = 0;
    periph_dirty_ = true;
    pc_ = read32(0);
    r_[15] = read32(4);
}

// ---------------------------------------------------------------------------
// Memory

uint8_t Sh2::read8(uint32_t a) {
    switch (a >> 29) {
        case 2:
        case 3: return 0;
        case 6: return cache_ram_[a & 0xfff];
        case 7: return (a >= 0xfffffe00u) ? onchip_read8(a) : 0;
        default: break;
    }
    a &= 0x1fffffff;
    const FastPage& p = pages_[a >> 24];
    if (p.data) return p.data[a & p.mask];
    return bus_->read8(a);
}

uint16_t Sh2::read16(uint32_t a) {
    a &= ~1u;
    switch (a >> 29) {
        case 2:
        case 3: return 0;
        case 6: return uint16_t((cache_ram_[a & 0xfff] << 8) | cache_ram_[(a + 1) & 0xfff]);
        case 7: return (a >= 0xfffffe00u) ? onchip_read16(a) : 0;
        default: break;
    }
    a &= 0x1fffffff;
    const FastPage& p = pages_[a >> 24];
    if (p.data) {
        const uint8_t* d = p.data + (a & p.mask);
        return uint16_t((d[0] << 8) | d[1]);
    }
    return bus_->read16(a);
}

uint32_t Sh2::read32(uint32_t a) {
    a &= ~3u;
    switch (a >> 29) {
        case 2:
        case 3: return 0;
        case 6: {
            const uint8_t* d = &cache_ram_[a & 0xffc];
            return (uint32_t(d[0]) << 24) | (uint32_t(d[1]) << 16) | (uint32_t(d[2]) << 8) | d[3];
        }
        case 7: return (a >= 0xfffffe00u) ? onchip_read32(a) : 0;
        default: break;
    }
    a &= 0x1fffffff;
    const FastPage& p = pages_[a >> 24];
    if (p.data) {
        const uint8_t* d = p.data + (a & p.mask);
        return (uint32_t(d[0]) << 24) | (uint32_t(d[1]) << 16) | (uint32_t(d[2]) << 8) | d[3];
    }
    return bus_->read32(a);
}

void Sh2::write8(uint32_t a, uint8_t v) {
    switch (a >> 29) {
        case 2:
        case 3: return;
        case 6: cache_ram_[a & 0xfff] = v; return;
        case 7:
            if (a >= 0xfffffe00u) onchip_write8(a, v);
            return;
        default: break;
    }
    a &= 0x1fffffff;
    const FastPage& p = pages_[a >> 24];
    if (p.data && p.writable) {
        p.data[a & p.mask] = v;
        return;
    }
    bus_->write8(a, v);
}

void Sh2::write16(uint32_t a, uint16_t v) {
    a &= ~1u;
    switch (a >> 29) {
        case 2:
        case 3: return;
        case 6:
            cache_ram_[a & 0xfff] = uint8_t(v >> 8);
            cache_ram_[(a + 1) & 0xfff] = uint8_t(v);
            return;
        case 7:
            if (a >= 0xfffffe00u) onchip_write16(a, v);
            return;
        default: break;
    }
    a &= 0x1fffffff;
    const FastPage& p = pages_[a >> 24];
    if (p.data && p.writable) {
        uint8_t* d = p.data + (a & p.mask);
        d[0] = uint8_t(v >> 8);
        d[1] = uint8_t(v);
        return;
    }
    bus_->write16(a, v);
}

void Sh2::write32(uint32_t a, uint32_t v) {
    a &= ~3u;
    switch (a >> 29) {
        case 2:
        case 3: return;
        case 6: {
            uint8_t* d = &cache_ram_[a & 0xffc];
            d[0] = uint8_t(v >> 24);
            d[1] = uint8_t(v >> 16);
            d[2] = uint8_t(v >> 8);
            d[3] = uint8_t(v);
            return;
        }
        case 7:
            if (a >= 0xfffffe00u) onchip_write32(a, v);
            return;
        default: break;
    }
    a &= 0x1fffffff;
    const FastPage& p = pages_[a >> 24];
    if (p.data && p.writable) {
        uint8_t* d = p.data + (a & p.mask);
        d[0] = uint8_t(v >> 24);
        d[1] = uint8_t(v >> 16);
        d[2] = uint8_t(v >> 8);
        d[3] = uint8_t(v);
        return;
    }
    bus_->write32(a, v);
}

uint16_t Sh2::fetch(uint32_t a) {
    if ((a >> 29) <= 1) {
        const uint32_t e = a & 0x1ffffffe;
        const FastPage& p = pages_[e >> 24];
        if (p.data) {
            const uint8_t* d = p.data + (e & p.mask);
            return uint16_t((d[0] << 8) | d[1]);
        }
    }
    return read16(a);
}

// ---------------------------------------------------------------------------
// On-chip modules

uint8_t Sh2::onchip_read8(uint32_t a) {
    const uint32_t o = a & 0x1ff;
    switch (o) {
        case kFtcsr: return ftcsr_;
        case kFrcH: return uint8_t(frc_ >> 8);
        case kFrcL: return uint8_t(frc_);
        case kOcrH: return uint8_t(((regs_[kTocr] & 0x10) ? ocrb_ : ocra_) >> 8);
        case kOcrL: return uint8_t((regs_[kTocr] & 0x10) ? ocrb_ : ocra_);
        case 0x80: return uint8_t(wtcsr_ | 0x18);
        case 0x81: return wtcnt_;
        case 0x82: return uint8_t(regs_[0x83] | 0x1f);
        default: break;
    }
    if (o >= 0x100) {
        const uint32_t v = onchip_read32(a & ~3u);
        return uint8_t(v >> (8 * (3 - (o & 3))));
    }
    return regs_[o];
}

uint16_t Sh2::onchip_read16(uint32_t a) {
    const uint32_t o = a & 0x1fe;
    if (o >= 0x100) {
        const uint32_t v = onchip_read32(a & ~3u);
        return uint16_t((o & 2) ? v : (v >> 16));
    }
    if ((o >= 0x60 && o < 0x6a) || (o >= 0xe0 && o < 0xe6)) {
        return uint16_t((regs_[o] << 8) | regs_[o + 1]);
    }
    return uint16_t((onchip_read8(a) << 8) | onchip_read8(a + 1));
}

uint32_t Sh2::onchip_read32(uint32_t a) {
    const uint32_t o = a & 0x1fc;
    if (o < 0x100) return (uint32_t(onchip_read16(a)) << 16) | onchip_read16(a + 2);
    switch (o) {
        case 0x100: return dvsr_;
        case 0x104: return dvdntl_;
        case 0x108: return dvcr_;
        case 0x10c: return (uint32_t(regs_[0x10e]) << 8) | regs_[0x10f];
        case 0x110:
        case 0x118: return dvdnth_;
        case 0x114:
        case 0x11c: return dvdntl_;
        case 0x180: return dma_[0].sar;
        case 0x184: return dma_[0].dar;
        case 0x188: return dma_[0].tcr;
        case 0x18c: return dma_[0].chcr;
        case 0x190: return dma_[1].sar;
        case 0x194: return dma_[1].dar;
        case 0x198: return dma_[1].tcr;
        case 0x19c: return dma_[1].chcr;
        case 0x1a0: return dma_[0].vcr;
        case 0x1a8: return dma_[1].vcr;
        case 0x1b0: return dmaor_;
        default: break;
    }
    return (uint32_t(regs_[o]) << 24) | (uint32_t(regs_[o + 1]) << 16) | (uint32_t(regs_[o + 2]) << 8) |
           regs_[o + 3];
}

void Sh2::onchip_write8(uint32_t a, uint8_t v) {
    periph_dirty_ = true;
    const uint32_t o = a & 0x1ff;
    if (o >= 0x100) {
        // Byte writes to the 32-bit modules: merge into the long.
        const uint32_t base = a & ~3u;
        const int shift = 8 * int(3 - (o & 3));
        uint32_t cur = onchip_read32(base);
        cur = (cur & ~(0xffu << shift)) | (uint32_t(v) << shift);
        onchip_write32(base, cur);
        return;
    }
    switch (o) {
        case kFtcsr:
            // Flags clear by writing 0 after reading 1; CCLRA is plain.
            ftcsr_ = uint8_t((ftcsr_ & v & 0x8e) | (v & 0x01));
            return;
        case kFrcH: frc_ = uint16_t((frc_ & 0x00ff) | (v << 8)); return;
        case kFrcL: frc_ = uint16_t((frc_ & 0xff00) | v); return;
        case kOcrH:
            if (regs_[kTocr] & 0x10) ocrb_ = uint16_t((ocrb_ & 0x00ff) | (v << 8));
            else ocra_ = uint16_t((ocra_ & 0x00ff) | (v << 8));
            return;
        case kOcrL:
            if (regs_[kTocr] & 0x10) ocrb_ = uint16_t((ocrb_ & 0xff00) | v);
            else ocra_ = uint16_t((ocra_ & 0xff00) | v);
            return;
        case 0x71:
        case 0x72: regs_[o] = v; return;
        case 0x92:
            // CCR: CP (bit 4) purges the cache and reads back 0.
            regs_[o] = uint8_t(v & ~0x10);
            return;
        default: break;
    }
    regs_[o] = v;
}

void Sh2::onchip_write16(uint32_t a, uint16_t v) {
    periph_dirty_ = true;
    const uint32_t o = a & 0x1fe;
    if (o >= 0x100) {
        const uint32_t base = a & ~3u;
        uint32_t cur = onchip_read32(base);
        if (o & 2) cur = (cur & 0xffff0000u) | v;
        else cur = (cur & 0x0000ffffu) | (uint32_t(v) << 16);
        onchip_write32(base, cur);
        return;
    }
    if (o == 0x80) {
        // Watchdog: $A5xx writes WTCSR, $5Axx writes WTCNT.
        if ((v >> 8) == 0xa5) {
            const uint8_t nv = uint8_t(v);
            wtcsr_ = uint8_t((nv & 0x7f) | (wtcsr_ & nv & 0x80));
        } else if ((v >> 8) == 0x5a) {
            wtcnt_ = uint8_t(v);
        }
        return;
    }
    if (o == 0x82) {
        if ((v >> 8) == 0x5a) regs_[0x83] = uint8_t(v & 0x60);
        return;
    }
    if ((o >= 0x60 && o < 0x6a) || (o >= 0xe0 && o < 0xe6)) {
        regs_[o] = uint8_t(v >> 8);
        regs_[o + 1] = uint8_t(v);
        return;
    }
    onchip_write8(a, uint8_t(v >> 8));
    onchip_write8(a + 1, uint8_t(v));
}

void Sh2::onchip_write32(uint32_t a, uint32_t v) {
    periph_dirty_ = true;
    const uint32_t o = a & 0x1fc;
    if (o < 0x100) {
        onchip_write16(a, uint16_t(v >> 16));
        onchip_write16(a + 2, uint16_t(v));
        return;
    }
    switch (o) {
        case 0x100: dvsr_ = v; return;
        case 0x104: dvdntl_ = v; divu_32(); return;
        case 0x108: dvcr_ = v & 3; return;
        case 0x110:
        case 0x118: dvdnth_ = v; return;
        case 0x114:
        case 0x11c: dvdntl_ = v; divu_64(); return;
        case 0x180: dma_[0].sar = v; return;
        case 0x184: dma_[0].dar = v; return;
        case 0x188: dma_[0].tcr = v & 0xffffff; return;
        case 0x18c:
            // TE is cleared by writing 0 after reading 1.
            dma_[0].chcr = (v & ~2u) | (dma_[0].chcr & v & 2u);
            dma_check(0);
            return;
        case 0x190: dma_[1].sar = v; return;
        case 0x194: dma_[1].dar = v; return;
        case 0x198: dma_[1].tcr = v & 0xffffff; return;
        case 0x19c:
            dma_[1].chcr = (v & ~2u) | (dma_[1].chcr & v & 2u);
            dma_check(1);
            return;
        case 0x1a0: dma_[0].vcr = v & 0x7f; return;
        case 0x1a8: dma_[1].vcr = v & 0x7f; return;
        case 0x1b0:
            dmaor_ = (v & 0x9) | (dmaor_ & v & 0x6);
            dma_check(0);
            dma_check(1);
            return;
        default: break;
    }
    regs_[o] = uint8_t(v >> 24);
    regs_[o + 1] = uint8_t(v >> 16);
    regs_[o + 2] = uint8_t(v >> 8);
    regs_[o + 3] = uint8_t(v);
}

void Sh2::divu_32() {
    const int32_t d = int32_t(dvdntl_);
    const int32_t s = int32_t(dvsr_);
    if (s == 0 || (d == INT32_MIN && s == -1)) {
        dvcr_ |= 1;
        dvdnth_ = uint32_t(d);
        dvdntl_ = (d < 0) ? 0x80000000u : 0x7fffffffu;
        return;
    }
    dvdntl_ = uint32_t(d / s);
    dvdnth_ = uint32_t(d % s);
}

void Sh2::divu_64() {
    const int64_t d = int64_t((uint64_t(dvdnth_) << 32) | dvdntl_);
    const int32_t s = int32_t(dvsr_);
    if (s == 0) {
        dvcr_ |= 1;
        dvdntl_ = (d < 0) ? 0x80000000u : 0x7fffffffu;
        return;
    }
    const int64_t q = d / s;
    const int64_t rem = d % s;
    if (q > INT32_MAX || q < INT32_MIN) {
        dvcr_ |= 1;
        dvdntl_ = (q < 0) ? 0x80000000u : 0x7fffffffu;
        return;
    }
    dvdntl_ = uint32_t(int32_t(q));
    dvdnth_ = uint32_t(int32_t(rem));
}

void Sh2::dma_check(int channel) {
    const DmaChannel& c = dma_[size_t(channel)];
    // Auto request runs the whole block as soon as it is enabled.
    if ((c.chcr & 0x200) != 0) dma_run(channel, false);
    else dma_run(channel, true);
}

void Sh2::dma_request(int channel) { dma_run(channel, true); }

void Sh2::dma_run(int channel, bool external) {
    if (dma_busy_) return;
    DmaChannel& c = dma_[size_t(channel)];
    if ((dmaor_ & 1) == 0 || (dmaor_ & 6) != 0) return;
    if ((c.chcr & 1) == 0 || (c.chcr & 2) != 0) return;
    if (external && (c.chcr & 0x200) != 0) return;
    if (!external && (c.chcr & 0x200) == 0) return;
    dma_busy_ = true;
    const int ts = int((c.chcr >> 10) & 3);
    const uint32_t unit = ts == 0 ? 1 : ts == 1 ? 2 : 4;
    const int sm = int((c.chcr >> 12) & 3);
    const int dm = int((c.chcr >> 14) & 3);
    auto step = [&](uint32_t& addr, int mode, uint32_t size) {
        if (mode == 1) addr += size;
        else if (mode == 2) addr -= size;
    };
    uint32_t count = c.tcr ? c.tcr : 0x1000000;
    while (count > 0) {
        if (external && !bus_->dreq(channel)) break;
        if (ts == 3) {
            // 16-byte unit: four longs, the count drops by 4.
            for (int i = 0; i < 4; i++) {
                write32(c.dar, read32(c.sar));
                if (sm) c.sar += 4;
                step(c.dar, dm, 4);
            }
            count = count > 4 ? count - 4 : 0;
        } else {
            if (unit == 1) write8(c.dar, read8(c.sar));
            else if (unit == 2) write16(c.dar, read16(c.sar));
            else write32(c.dar, read32(c.sar));
            step(c.sar, sm, unit);
            step(c.dar, dm, unit);
            count--;
        }
    }
    c.tcr = count & 0xffffff;
    if (count == 0) c.chcr |= 2;  // TE
    periph_dirty_ = true;
    dma_busy_ = false;
}

int Sh2::peripheral_level(int* vector) {
    int best = 0;
    auto offer = [&](int level, int vec) {
        if (level > best) {
            best = level;
            *vector = vec;
        }
    };
    const uint16_t ipra = uint16_t((regs_[kIpra] << 8) | regs_[kIpra + 1]);
    const uint16_t iprb = uint16_t((regs_[kIprb] << 8) | regs_[kIprb + 1]);
    // DMA transfer end.
    for (int ch = 0; ch < 2; ch++) {
        const DmaChannel& c = dma_[size_t(ch)];
        if ((c.chcr & 0x6) == 0x6) offer((ipra >> 8) & 15, int(c.vcr & 0x7f));
    }
    // Division overflow.
    if ((dvcr_ & 3) == 3) offer((ipra >> 12) & 15, regs_[0x10f] & 0x7f);
    // Watchdog interval.
    if ((wtcsr_ & 0xc0) == 0x80) offer((ipra >> 4) & 15, regs_[kVcrwdt] & 0x7f);
    // Free-running timer.
    const uint8_t tier = regs_[kTier];
    const int frt = (iprb >> 8) & 15;
    if ((ftcsr_ & tier & 0x80) != 0) offer(frt, regs_[kVcrc] & 0x7f);
    else if ((ftcsr_ & tier & 0x0c) != 0) offer(frt, regs_[kVcrc + 1] & 0x7f);
    else if ((ftcsr_ & tier & 0x02) != 0) offer(frt, regs_[kVcrd] & 0x7f);
    return best;
}

void Sh2::tick_peripherals(int cycles) {
    // FRT: Pφ / 8, 32 or 128.
    static const int kFrtDiv[4] = {8, 32, 128, 128};
    frt_div_ += cycles;
    const int fdiv = kFrtDiv[regs_[kTcr] & 3];
    if (frt_div_ < fdiv) goto wdt;
    {
        // Jump straight to just before the next compare / overflow.
        const int steps = frt_div_ / fdiv;
        auto dist = [&](uint16_t target) { return uint16_t(target - frc_); };
        int safe = std::min({int(dist(ocra_)), int(dist(ocrb_)), int(dist(0))}) - 1;
        if (safe > 0) {
            const int skip = std::min(safe, steps);
            frc_ = uint16_t(frc_ + skip);
            frt_div_ -= skip * fdiv;
        }
    }
    while (frt_div_ >= fdiv) {
        frt_div_ -= fdiv;
        frc_++;
        if (frc_ == ocra_) {
            ftcsr_ |= 0x08;
            periph_dirty_ = true;
            if (ftcsr_ & 0x01) frc_ = 0;
        }
        if (frc_ == ocrb_) {
            ftcsr_ |= 0x04;
            periph_dirty_ = true;
        }
        if (frc_ == 0) {
            ftcsr_ |= 0x02;
            periph_dirty_ = true;
        }
    }
wdt:
    // Watchdog interval timer.
    if (wtcsr_ & 0x20) {
        static const int kWdtDiv[8] = {2, 64, 128, 256, 512, 1024, 4096, 8192};
        wdt_div_ += cycles;
        const int wdiv = kWdtDiv[wtcsr_ & 7];
        while (wdt_div_ >= wdiv) {
            wdt_div_ -= wdiv;
            if (++wtcnt_ == 0) {
                wtcsr_ |= 0x80;
                periph_dirty_ = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Execution

void Sh2::exception(uint32_t vector) {
    r_[15] -= 4;
    write32(r_[15], sr_);
    r_[15] -= 4;
    write32(r_[15], pc_);
    pc_ = read32(vbr_ + vector * 4);
}

bool Sh2::check_interrupts() {
    const int mask = int((sr_ >> 4) & 15);
    if (periph_dirty_) {
        periph_dirty_ = false;
        periph_level_ = peripheral_level(&periph_vector_);
    }
    if (irl_ <= mask && periph_level_ <= mask) return false;
    int vector = periph_vector_;
    int level = periph_level_;
    if (irl_ > level) {
        level = irl_;
        vector = 64 + (irl_ >> 1);
    }
    if (level <= mask) return false;
    sleeping_ = false;
    exception(uint32_t(vector));
    sr_ = (sr_ & ~0xf0u) | (uint32_t(level) << 4);
    cycles_ += 13;
    return true;
}

void Sh2::delay_slot(uint32_t target) {
    const uint16_t op = fetch(pc_);
    pc_ += 2;
    in_slot_ = true;
    execute(op);
    in_slot_ = false;
    pc_ = target;
}

int Sh2::run(int cycles) {
    cycles_ = 0;
    int ticked = 0;
    while (cycles_ < cycles) {
        if (!check_interrupts() && sleeping_) {
            cycles_ = cycles;
            break;
        }
        const uint16_t op = fetch(pc_);
        pc_ += 2;
        cycles_++;
        execute(op);
        // Timers advance in small batches: their flags (and interrupts)
        // show up within 32 cycles.
        if (cycles_ - ticked >= 32) {
            tick_peripherals(cycles_ - ticked);
            ticked = cycles_;
        }
    }
    tick_peripherals(cycles_ - ticked);
    total_cycles_ += uint64_t(cycles_);
    return cycles_;
}

void Sh2::execute(uint16_t op) {
    const int n = (op >> 8) & 15;
    const int m = (op >> 4) & 15;
    uint32_t& rn = r_[size_t(n)];
    uint32_t& rm = r_[size_t(m)];
    auto set_t = [this](bool v) { sr_ = v ? (sr_ | kT) : (sr_ & ~kT); };
    auto t = [this]() { return (sr_ & kT) != 0; };

    switch (op >> 12) {
        case 0x0:
            switch (op & 0xf) {
                case 0x2:
                    if (m == 0) rn = sr_;
                    else if (m == 1) rn = gbr_;
                    else if (m == 2) rn = vbr_;
                    return;
                case 0x3:
                    if (m == 0) {  // BSRF Rn
                        pr_ = pc_ + 2;
                        cycles_++;
                        delay_slot(pc_ + 2 + rn);
                    } else if (m == 2) {  // BRAF Rn
                        cycles_++;
                        delay_slot(pc_ + 2 + rn);
                    }
                    return;
                case 0x4: write8(rn + r_[0], uint8_t(rm)); cycles_++; return;
                case 0x5: write16(rn + r_[0], uint16_t(rm)); cycles_++; return;
                case 0x6: write32(rn + r_[0], rm); cycles_++; return;
                case 0x7: macl_ = rn * rm; cycles_ += 2; return;
                case 0x8:
                    if (m == 0) sr_ &= ~kT;
                    else if (m == 1) sr_ |= kT;
                    else if (m == 2) mach_ = macl_ = 0;
                    return;
                case 0x9:
                    if (m == 0) return;  // NOP
                    if (m == 1) {  // DIV0U
                        sr_ &= ~(kM | kQ | kT);
                    } else if (m == 2) {  // MOVT
                        rn = sr_ & kT;
                    }
                    return;
                case 0xa:
                    if (m == 0) rn = mach_;
                    else if (m == 1) rn = macl_;
                    else if (m == 2) rn = pr_;
                    return;
                case 0xb:
                    if (m == 0) {  // RTS
                        cycles_++;
                        delay_slot(pr_);
                    } else if (m == 1) {  // SLEEP
                        sleeping_ = true;
                        cycles_ += 2;
                    } else if (m == 2) {  // RTE
                        const uint32_t target = read32(r_[15]);
                        r_[15] += 4;
                        sr_ = read32(r_[15]) & kSrMask;
                        r_[15] += 4;
                        cycles_ += 3;
                        delay_slot(target);
                    }
                    return;
                case 0xc: rn = sext8(read8(rm + r_[0])); cycles_++; return;
                case 0xd: rn = sext16(read16(rm + r_[0])); cycles_++; return;
                case 0xe: rn = read32(rm + r_[0]); cycles_++; return;
                case 0xf: {  // MAC.L @Rm+,@Rn+
                    const int64_t a = int32_t(read32(rn));
                    rn += 4;
                    const int64_t b = int32_t(read32(rm));
                    rm += 4;
                    int64_t acc = int64_t((uint64_t(mach_) << 32) | macl_);
                    acc += a * b;
                    if (sr_ & kS) {
                        const int64_t lo = -(int64_t(1) << 47);
                        const int64_t hi = (int64_t(1) << 47) - 1;
                        if (acc < lo) acc = lo;
                        if (acc > hi) acc = hi;
                    }
                    mach_ = uint32_t(uint64_t(acc) >> 32);
                    macl_ = uint32_t(acc);
                    cycles_ += 3;
                    return;
                }
                default: return;
            }
        case 0x1: write32(rn + uint32_t(op & 0xf) * 4, rm); cycles_++; return;
        case 0x2:
            switch (op & 0xf) {
                case 0x0: write8(rn, uint8_t(rm)); cycles_++; return;
                case 0x1: write16(rn, uint16_t(rm)); cycles_++; return;
                case 0x2: write32(rn, rm); cycles_++; return;
                case 0x4: write8(rn - 1, uint8_t(rm)); rn -= 1; cycles_++; return;
                case 0x5: write16(rn - 2, uint16_t(rm)); rn -= 2; cycles_++; return;
                case 0x6: write32(rn - 4, rm); rn -= 4; cycles_++; return;
                case 0x7:  // DIV0S
                    sr_ = (rn & 0x80000000u) ? (sr_ | kQ) : (sr_ & ~kQ);
                    sr_ = (rm & 0x80000000u) ? (sr_ | kM) : (sr_ & ~kM);
                    set_t(((sr_ & kQ) != 0) != ((sr_ & kM) != 0));
                    return;
                case 0x8: set_t((rn & rm) == 0); return;
                case 0x9: rn &= rm; return;
                case 0xa: rn ^= rm; return;
                case 0xb: rn |= rm; return;
                case 0xc: {  // CMP/STR
                    const uint32_t x = rn ^ rm;
                    set_t((x & 0xff000000u) == 0 || (x & 0x00ff0000u) == 0 || (x & 0x0000ff00u) == 0 ||
                          (x & 0x000000ffu) == 0);
                    return;
                }
                case 0xd: rn = (rn >> 16) | (rm << 16); return;  // XTRCT
                case 0xe: macl_ = (rn & 0xffff) * (rm & 0xffff); return;  // MULU.W
                case 0xf: macl_ = uint32_t(int32_t(int16_t(rn)) * int32_t(int16_t(rm))); return;
                default: return;
            }
        case 0x3:
            switch (op & 0xf) {
                case 0x0: set_t(rn == rm); return;
                case 0x2: set_t(rn >= rm); return;
                case 0x3: set_t(int32_t(rn) >= int32_t(rm)); return;
                case 0x4: {  // DIV1
                    const bool old_q = (sr_ & kQ) != 0;
                    const bool mq = (sr_ & kM) != 0;
                    bool q = (rn & 0x80000000u) != 0;
                    const uint32_t tmp2 = rm;
                    rn = (rn << 1) | (sr_ & kT);
                    const uint32_t tmp0 = rn;
                    bool tmp1;
                    if (old_q == mq) {
                        rn -= tmp2;
                        tmp1 = rn > tmp0;
                    } else {
                        rn += tmp2;
                        tmp1 = rn < tmp0;
                    }
                    if (!old_q) {
                        q = (!mq) ? (q ? !tmp1 : tmp1) : (q ? tmp1 : !tmp1);
                    } else {
                        q = (!mq) ? (q ? !tmp1 : tmp1) : (q ? tmp1 : !tmp1);
                    }
                    sr_ = q ? (sr_ | kQ) : (sr_ & ~kQ);
                    set_t(q == mq);
                    return;
                }
                case 0x5: {
                    const uint64_t p = uint64_t(rn) * uint64_t(rm);
                    mach_ = uint32_t(p >> 32);
                    macl_ = uint32_t(p);
                    cycles_ += 2;
                    return;
                }
                case 0x6: set_t(rn > rm); return;
                case 0x7: set_t(int32_t(rn) > int32_t(rm)); return;
                case 0x8: rn -= rm; return;
                case 0xa: {  // SUBC
                    const uint32_t tmp1 = rn - rm;
                    const uint32_t tmp0 = rn;
                    rn = tmp1 - (sr_ & kT);
                    set_t(tmp0 < tmp1 || tmp1 < rn);
                    return;
                }
                case 0xb: {  // SUBV
                    const uint32_t res = rn - rm;
                    set_t((((rn ^ rm) & (rn ^ res)) >> 31) != 0);
                    rn = res;
                    return;
                }
                case 0xc: rn += rm; return;
                case 0xd: {
                    const int64_t p = int64_t(int32_t(rn)) * int64_t(int32_t(rm));
                    mach_ = uint32_t(uint64_t(p) >> 32);
                    macl_ = uint32_t(p);
                    cycles_ += 2;
                    return;
                }
                case 0xe: {  // ADDC
                    const uint32_t tmp1 = rn + rm;
                    const uint32_t tmp0 = rn;
                    rn = tmp1 + (sr_ & kT);
                    set_t(tmp0 > tmp1 || tmp1 > rn);
                    return;
                }
                case 0xf: {  // ADDV
                    const uint32_t res = rn + rm;
                    set_t(((~(rn ^ rm) & (rn ^ res)) >> 31) != 0);
                    rn = res;
                    return;
                }
                default: return;
            }
        case 0x4:
            if ((op & 0xf) == 0xf) {  // MAC.W @Rm+,@Rn+
                const int32_t a = int16_t(read16(rn));
                rn += 2;
                const int32_t b = int16_t(read16(rm));
                rm += 2;
                const int64_t prod = int64_t(a) * b;
                if (sr_ & kS) {
                    int64_t res = int64_t(int32_t(macl_)) + prod;
                    if (res > INT32_MAX) {
                        res = INT32_MAX;
                        mach_ |= 1;
                    } else if (res < INT32_MIN) {
                        res = INT32_MIN;
                        mach_ |= 1;
                    }
                    macl_ = uint32_t(int32_t(res));
                } else {
                    const int64_t acc = int64_t((uint64_t(mach_) << 32) | macl_) + prod;
                    mach_ = uint32_t(uint64_t(acc) >> 32);
                    macl_ = uint32_t(acc);
                }
                cycles_ += 2;
                return;
            }
            switch (op & 0xff) {
                case 0x00: set_t((rn >> 31) != 0); rn <<= 1; return;           // SHLL
                case 0x01: set_t((rn & 1) != 0); rn >>= 1; return;             // SHLR
                case 0x02: rn -= 4; write32(rn, mach_); cycles_++; return;     // STS.L MACH
                case 0x03: rn -= 4; write32(rn, sr_); cycles_++; return;       // STC.L SR
                case 0x04: set_t((rn >> 31) != 0); rn = (rn << 1) | (rn >> 31); return;  // ROTL
                case 0x05: set_t((rn & 1) != 0); rn = (rn >> 1) | (rn << 31); return;    // ROTR
                case 0x06: mach_ = read32(rn); rn += 4; cycles_++; return;
                case 0x07: sr_ = read32(rn) & kSrMask; rn += 4; cycles_ += 2; return;
                case 0x08: rn <<= 2; return;
                case 0x09: rn >>= 2; return;
                case 0x0a: mach_ = rn; return;
                case 0x0b: {  // JSR @Rn
                    const uint32_t target = rn;
                    pr_ = pc_ + 2;
                    cycles_++;
                    delay_slot(target);
                    return;
                }
                case 0x0e: sr_ = rn & kSrMask; return;
                case 0x10: rn--; set_t(rn == 0); return;  // DT
                case 0x11: set_t(int32_t(rn) >= 0); return;
                case 0x12: rn -= 4; write32(rn, macl_); cycles_++; return;
                case 0x13: rn -= 4; write32(rn, gbr_); cycles_++; return;
                case 0x15: set_t(int32_t(rn) > 0); return;
                case 0x16: macl_ = read32(rn); rn += 4; cycles_++; return;
                case 0x17: gbr_ = read32(rn); rn += 4; cycles_ += 2; return;
                case 0x18: rn <<= 8; return;
                case 0x19: rn >>= 8; return;
                case 0x1a: macl_ = rn; return;
                case 0x1b: {  // TAS.B
                    const uint8_t v = read8(rn);
                    set_t(v == 0);
                    write8(rn, uint8_t(v | 0x80));
                    cycles_ += 3;
                    return;
                }
                case 0x1e: gbr_ = rn; return;
                case 0x20: set_t((rn >> 31) != 0); rn <<= 1; return;  // SHAL
                case 0x21: set_t((rn & 1) != 0); rn = uint32_t(int32_t(rn) >> 1); return;  // SHAR
                case 0x22: rn -= 4; write32(rn, pr_); cycles_++; return;
                case 0x23: rn -= 4; write32(rn, vbr_); cycles_++; return;
                case 0x24: {  // ROTCL
                    const bool c = (rn >> 31) != 0;
                    rn = (rn << 1) | (sr_ & kT);
                    set_t(c);
                    return;
                }
                case 0x25: {  // ROTCR
                    const bool c = (rn & 1) != 0;
                    rn = (rn >> 1) | ((sr_ & kT) << 31);
                    set_t(c);
                    return;
                }
                case 0x26: pr_ = read32(rn); rn += 4; cycles_++; return;
                case 0x27: vbr_ = read32(rn); rn += 4; cycles_ += 2; return;
                case 0x28: rn <<= 16; return;
                case 0x29: rn >>= 16; return;
                case 0x2a: pr_ = rn; return;
                case 0x2b: cycles_++; delay_slot(rn); return;  // JMP
                case 0x2e: vbr_ = rn; return;
                default: return;
            }
        case 0x5: rn = read32(rm + uint32_t(op & 0xf) * 4); cycles_++; return;
        case 0x6:
            switch (op & 0xf) {
                case 0x0: rn = sext8(read8(rm)); cycles_++; return;
                case 0x1: rn = sext16(read16(rm)); cycles_++; return;
                case 0x2: rn = read32(rm); cycles_++; return;
                case 0x3: rn = rm; return;
                case 0x4: {
                    const uint32_t v = sext8(read8(rm));
                    if (n != m) rm += 1;
                    rn = v;
                    cycles_++;
                    return;
                }
                case 0x5: {
                    const uint32_t v = sext16(read16(rm));
                    if (n != m) rm += 2;
                    rn = v;
                    cycles_++;
                    return;
                }
                case 0x6: {
                    const uint32_t v = read32(rm);
                    if (n != m) rm += 4;
                    rn = v;
                    cycles_++;
                    return;
                }
                case 0x7: rn = ~rm; return;
                case 0x8: rn = (rm & 0xffff0000u) | ((rm & 0xff) << 8) | ((rm >> 8) & 0xff); return;
                case 0x9: rn = (rm << 16) | (rm >> 16); return;
                case 0xa: {  // NEGC
                    const uint32_t tmp = 0u - rm;
                    rn = tmp - (sr_ & kT);
                    set_t(tmp != 0 || tmp < rn);
                    return;
                }
                case 0xb: rn = 0u - rm; return;
                case 0xc: rn = rm & 0xff; return;
                case 0xd: rn = rm & 0xffff; return;
                case 0xe: rn = sext8(rm); return;
                case 0xf: rn = sext16(rm); return;
                default: return;
            }
        case 0x7: rn += sext8(op & 0xff); return;
        case 0x8: {
            const uint32_t disp = op & 0xf;
            const uint32_t d8 = sext8(op & 0xff);
            switch (n) {
                case 0x0: write8(rm + disp, uint8_t(r_[0])); cycles_++; return;
                case 0x1: write16(rm + disp * 2, uint16_t(r_[0])); cycles_++; return;
                case 0x4: r_[0] = sext8(read8(rm + disp)); cycles_++; return;
                case 0x5: r_[0] = sext16(read16(rm + disp * 2)); cycles_++; return;
                case 0x8: set_t(r_[0] == d8); return;
                case 0x9:  // BT
                    if (t()) {
                        pc_ = pc_ + 2 + d8 * 2;
                        cycles_ += 2;
                    }
                    return;
                case 0xb:  // BF
                    if (!t()) {
                        pc_ = pc_ + 2 + d8 * 2;
                        cycles_ += 2;
                    }
                    return;
                case 0xd:  // BT/S
                    if (t()) {
                        cycles_++;
                        delay_slot(pc_ + 2 + d8 * 2);
                    }
                    return;
                case 0xf:  // BF/S
                    if (!t()) {
                        cycles_++;
                        delay_slot(pc_ + 2 + d8 * 2);
                    }
                    return;
                default: return;
            }
        }
        case 0x9: rn = sext16(read16(pc_ + 2 + uint32_t(op & 0xff) * 2)); cycles_++; return;
        case 0xa: cycles_++; delay_slot(pc_ + 2 + sext12(op & 0xfff) * 2); return;  // BRA
        case 0xb: {  // BSR
            pr_ = pc_ + 2;
            cycles_++;
            delay_slot(pc_ + 2 + sext12(op & 0xfff) * 2);
            return;
        }
        case 0xc: {
            const uint32_t imm = op & 0xff;
            switch (n) {
                case 0x0: write8(gbr_ + imm, uint8_t(r_[0])); cycles_++; return;
                case 0x1: write16(gbr_ + imm * 2, uint16_t(r_[0])); cycles_++; return;
                case 0x2: write32(gbr_ + imm * 4, r_[0]); cycles_++; return;
                case 0x3:  // TRAPA
                    exception(imm);
                    cycles_ += 7;
                    return;
                case 0x4: r_[0] = sext8(read8(gbr_ + imm)); cycles_++; return;
                case 0x5: r_[0] = sext16(read16(gbr_ + imm * 2)); cycles_++; return;
                case 0x6: r_[0] = read32(gbr_ + imm * 4); cycles_++; return;
                case 0x7: r_[0] = ((pc_ + 2) & ~3u) + imm * 4; return;  // MOVA
                case 0x8: set_t((r_[0] & imm) == 0); return;
                case 0x9: r_[0] &= imm; return;
                case 0xa: r_[0] ^= imm; return;
                case 0xb: r_[0] |= imm; return;
                case 0xc: set_t((read8(gbr_ + r_[0]) & imm) == 0); cycles_ += 2; return;
                case 0xd: {
                    const uint32_t a = gbr_ + r_[0];
                    write8(a, uint8_t(read8(a) & imm));
                    cycles_ += 2;
                    return;
                }
                case 0xe: {
                    const uint32_t a = gbr_ + r_[0];
                    write8(a, uint8_t(read8(a) ^ imm));
                    cycles_ += 2;
                    return;
                }
                case 0xf: {
                    const uint32_t a = gbr_ + r_[0];
                    write8(a, uint8_t(read8(a) | imm));
                    cycles_ += 2;
                    return;
                }
                default: return;
            }
        }
        case 0xd: rn = read32(((pc_ + 2) & ~3u) + uint32_t(op & 0xff) * 4); cycles_++; return;
        case 0xe: rn = sext8(op & 0xff); return;
        default: return;
    }
}

}  // namespace dsp
