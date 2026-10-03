#include "machine/zxn_dma.h"

namespace dsp {
namespace {

enum Param : uint8_t {
    kPortALo, kPortAHi, kLenLo, kLenHi, kTimingA, kTimingB, kPrescaler, kReadMask,
    kIgnore, kPortBLo, kPortBHi,
};

}  // namespace

void ZxnDma::reset() {
    wr0_ = wr1_ = wr2_ = wr3_ = wr4_ = wr5_ = 0;
    port_a_ = port_b_ = length_ = 0;
    timing_a_ = timing_b_ = 1;
    prescaler_ = 0;
    read_mask_ = 0x7f;
    follow_count_ = follow_pos_ = 0;
    enabled_ = false;
    addr_a_ = addr_b_ = 0;
    counter_ = 0;
    end_of_block_ = false;
    prescaler_wait_ = 0;
    read_count_ = read_pos_ = 0;
    status_ = 0x3a;
}

void ZxnDma::write(uint8_t value, bool z80_mode) {
    z80_mode_ = z80_mode;
    if (follow_pos_ < follow_count_) {
        switch (follow_[follow_pos_++]) {
            case kPortALo: port_a_ = uint16_t((port_a_ & 0xff00) | value); break;
            case kPortAHi: port_a_ = uint16_t((port_a_ & 0x00ff) | (value << 8)); break;
            case kLenLo: length_ = uint16_t((length_ & 0xff00) | value); break;
            case kLenHi: length_ = uint16_t((length_ & 0x00ff) | (value << 8)); break;
            case kTimingA: timing_a_ = value; break;
            case kTimingB:
                timing_b_ = value;
                if (value & 0x20) follow_[follow_count_++] = kPrescaler;
                break;
            case kPrescaler: prescaler_ = value; break;
            case kReadMask: read_mask_ = value & 0x7f; break;
            case kPortBLo: port_b_ = uint16_t((port_b_ & 0xff00) | value); break;
            case kPortBHi: port_b_ = uint16_t((port_b_ & 0x00ff) | (value << 8)); break;
            default: break;
        }
        return;
    }

    follow_count_ = follow_pos_ = 0;
    auto add = [this](uint8_t p) { follow_[follow_count_++] = p; };
    if ((value & 0x80) == 0) {
        if ((value & 0x03) != 0) {  // WR0
            // zxnDMA treats any WR0 as a transfer (search modes are not supported)
            wr0_ = value;
            if (value & 0x08) add(kPortALo);
            if (value & 0x10) add(kPortAHi);
            if (value & 0x20) add(kLenLo);
            if (value & 0x40) add(kLenHi);
        } else if ((value & 0x07) == 0x04) {  // WR1
            wr1_ = value;
            if (value & 0x40) add(kTimingA);
        } else if ((value & 0x07) == 0x00) {  // WR2
            wr2_ = value;
            if (value & 0x40) add(kTimingB);
        }
        return;
    }
    switch (value & 0x83) {
        case 0x80:  // WR3
            wr3_ = value;
            if (value & 0x08) add(kIgnore);  // mask byte
            if (value & 0x10) add(kIgnore);  // match byte
            if (value & 0x40) enabled_ = true;
            break;
        case 0x81:  // WR4
            wr4_ = value;
            if (value & 0x04) add(kPortBLo);
            if (value & 0x08) add(kPortBHi);
            if (value & 0x10) add(kIgnore);  // interrupt control
            break;
        case 0x82:  // WR5
            if ((value & 0x07) == 0x02) wr5_ = value;
            break;
        case 0x83:  // WR6
            command(value);
            break;
    }
}

void ZxnDma::command(uint8_t value) {
    switch (value) {
        case 0xc3:  // reset
            enabled_ = false;
            wr5_ = 0;
            timing_a_ = timing_b_ = 1;
            prescaler_ = 0;
            read_mask_ = 0x7f;
            status_ = 0x3a;
            break;
        case 0xc7: timing_a_ = 1; break;  // reset port A timing
        case 0xcb: timing_b_ = 1; prescaler_ = 0; break;  // reset port B timing
        case 0xcf: load(); break;
        case 0xd3:  // continue
            counter_ = 0;
            end_of_block_ = false;
            break;
        case 0x87:  // enable DMA
            enabled_ = true;
            counter_ = 0;
            end_of_block_ = false;
            prescaler_wait_ = 0;
            break;
        case 0x83: enabled_ = false; break;  // disable DMA
        case 0xbb: follow_[follow_count_++] = kReadMask; break;
        case 0xbf:  // read status byte
            read_seq_[0] = uint8_t(0x1a | (end_of_block_ ? 0 : 0x20) | (counter_ ? 1 : 0));
            read_count_ = 1;
            read_pos_ = 0;
            break;
        case 0x8b:  // reinitialize status byte
            end_of_block_ = false;
            break;
        case 0xa7: {  // initialize read sequence
            read_count_ = read_pos_ = 0;
            const uint8_t st = uint8_t(0x1a | (end_of_block_ ? 0 : 0x20) | (counter_ ? 1 : 0));
            const uint8_t vals[7] = {st,
                                     uint8_t(counter_), uint8_t(counter_ >> 8),
                                     uint8_t(addr_a_), uint8_t(addr_a_ >> 8),
                                     uint8_t(addr_b_), uint8_t(addr_b_ >> 8)};
            for (int i = 0; i < 7; ++i)
                if (read_mask_ & (1 << i)) read_seq_[read_count_++] = vals[i];
            break;
        }
        default:
            break;  // interrupt and ready commands are not used on the Next
    }
}

uint8_t ZxnDma::read() {
    if (read_count_ == 0) {
        command(0xa7);
        if (read_count_ == 0) return 0;
    }
    const uint8_t v = read_seq_[read_pos_];
    read_pos_ = (read_pos_ + 1) % read_count_;
    return v;
}

void ZxnDma::load() {
    addr_a_ = port_a_;
    addr_b_ = port_b_;
    counter_ = 0;
    end_of_block_ = false;
}

void ZxnDma::transfer_byte() {
    const bool a_to_b = (wr0_ & 0x04) != 0;
    const bool a_io = (wr1_ & 0x08) != 0;
    const bool b_io = (wr2_ & 0x08) != 0;
    const uint16_t src = a_to_b ? addr_a_ : addr_b_;
    const uint16_t dst = a_to_b ? addr_b_ : addr_a_;
    const bool src_io = a_to_b ? a_io : b_io;
    const bool dst_io = a_to_b ? b_io : a_io;
    const uint8_t v = src_io ? io_read(src) : mem_read(src);
    if (dst_io) io_write(dst, v);
    else mem_write(dst, v);

    auto step = [](uint16_t& addr, uint8_t wr) {
        switch ((wr >> 4) & 3) {
            case 0: addr = uint16_t(addr - 1); break;
            case 1: addr = uint16_t(addr + 1); break;
            default: break;
        }
    };
    step(addr_a_, wr1_);
    step(addr_b_, wr2_);
    ++counter_;
}

void ZxnDma::finish() {
    end_of_block_ = true;
    if (wr5_ & 0x20) {  // auto restart
        load();
        return;
    }
    enabled_ = false;
}

int ZxnDma::run(int budget) {
    if (!enabled_) return 0;
    const uint32_t total = z80_mode_ ? uint32_t(length_) + 1 : uint32_t(length_);
    if (total == 0) {
        finish();
        return 0;
    }
    const int mode = (wr4_ >> 5) & 3;  // 0 byte, 1 continuous, 2 burst
    auto cycle_len = [](uint8_t timing) {
        switch (timing & 3) {
            case 0: return 4;
            case 1: return 3;
            case 2: return 2;
            default: return 4;
        }
    };
    const int ticks_per_byte = (cycle_len(timing_a_) + cycle_len(timing_b_)) * ticks_per_t;
    int used = 0;
    while (enabled_ && used < budget) {
        if (prescaler_ && prescaler_wait_ > 0) {
            if (mode == 1) {  // continuous: the bus stays held while waiting
                const int w = prescaler_wait_ < budget - used ? prescaler_wait_ : budget - used;
                prescaler_wait_ -= w;
                used += w;
                continue;
            }
            break;  // burst: the CPU runs until the next sample is due
        }
        transfer_byte();
        used += ticks_per_byte;
        if (prescaler_) prescaler_wait_ += int(prescaler_) * 32;
        if (counter_ >= total) finish();
        if (mode == 0) break;  // byte mode: one byte per bus request
    }
    return used;
}

}  // namespace dsp
