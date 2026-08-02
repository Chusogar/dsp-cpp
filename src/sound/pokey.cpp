#include "sound/pokey.h"

#include <algorithm>

namespace dsp {
namespace {

// Write registers.
constexpr uint16_t kAudCtl = 0x08;
constexpr uint16_t kSTimer = 0x09;
constexpr uint16_t kSkRest = 0x0a;
constexpr uint16_t kPotGo = 0x0b;
constexpr uint16_t kSerOut = 0x0d;
constexpr uint16_t kIrqEn = 0x0e;
constexpr uint16_t kSkCtl = 0x0f;
// Read registers.
constexpr uint16_t kAllPot = 0x08;
constexpr uint16_t kKbCode = 0x09;
constexpr uint16_t kRandom = 0x0a;
constexpr uint16_t kSerIn = 0x0d;
constexpr uint16_t kIrqSt = 0x0e;
constexpr uint16_t kSkStat = 0x0f;

// AUDCx
constexpr uint8_t kNotPoly5 = 0x80;
constexpr uint8_t kPoly4 = 0x40;
constexpr uint8_t kPure = 0x20;
constexpr uint8_t kVolumeOnly = 0x10;
constexpr uint8_t kVolumeMask = 0x0f;
// AUDCTL
constexpr uint8_t kPoly9 = 0x80;
constexpr uint8_t kCh1HiClk = 0x40;
constexpr uint8_t kCh3HiClk = 0x20;
constexpr uint8_t kCh12Joined = 0x10;
constexpr uint8_t kCh34Joined = 0x08;
constexpr uint8_t kCh1Filter = 0x04;
constexpr uint8_t kCh2Filter = 0x02;
constexpr uint8_t kClk15Khz = 0x01;
// IRQEN / IRQST
constexpr uint8_t kIrqTimer4 = 0x04;
constexpr uint8_t kIrqTimer2 = 0x02;
constexpr uint8_t kIrqTimer1 = 0x01;
// SKSTAT
constexpr uint8_t kSkFrame = 0x80;
constexpr uint8_t kSkKbErr = 0x40;
constexpr uint8_t kSkOverrun = 0x20;
constexpr uint8_t kSkSerOut = 0x02;
// SKCTL
constexpr uint8_t kSkPaddle = 0x04;
constexpr uint8_t kSkReset = 0x03;

constexpr int kClk1 = 0, kClk28 = 1, kClk114 = 2;
constexpr int kClockDivisors[3] = {1, 28, 114};
constexpr int kDefaultGain = 32767 / 11 / 4;

constexpr int kChan1 = 0, kChan2 = 1, kChan3 = 2, kChan4 = 3;

std::vector<uint32_t> poly_init_4_5(int size, int xorbit, uint32_t invert) {
    const int mask = (1 << size) - 1;
    std::vector<uint32_t> poly(static_cast<size_t>(mask));
    uint32_t lfsr = 0;
    for (int i = 0; i < mask; ++i) {
        const uint32_t in = (~(lfsr >> 0) & 1) ^ ((lfsr >> xorbit) & 1);
        lfsr >>= 1;
        lfsr = (in << (size - 1)) | lfsr;
        poly[size_t(i)] = lfsr ^ invert;
    }
    return poly;
}

std::vector<uint32_t> poly_init_9_17(int size) {
    const int mask = (1 << size) - 1;
    std::vector<uint32_t> poly(static_cast<size_t>(mask));
    uint32_t lfsr = uint32_t(mask);
    for (int i = 0; i < mask; ++i) {
        if (size == 17) {
            const uint32_t in8 = ((lfsr >> 8) & 1) ^ ((lfsr >> 13) & 1);
            const uint32_t in = lfsr & 1;
            lfsr >>= 1;
            lfsr = (lfsr & 0xff7f) | (in8 << 7);
            lfsr = (in << 16) | lfsr;
        } else {
            const uint32_t in = ((lfsr >> 0) & 1) ^ ((lfsr >> 5) & 1);
            lfsr >>= 1;
            lfsr = (in << 8) | lfsr;
        }
        poly[size_t(i)] = lfsr;
    }
    return poly;
}

}  // namespace

Pokey::Pokey(uint32_t clock, float amplitude) : clock_(clock), amplitude_(amplitude) {
    poly4_ = poly_init_4_5(4, 1, 0);
    poly5_ = poly_init_4_5(5, 2, 1);
    poly9_ = poly_init_9_17(9);
    poly17_ = poly_init_9_17(17);
    reset();
}

void Pokey::reset() {
    output_ = 0;
    for (int i = 0; i < kChannels; ++i) {
        channel_[i] = Channel{};
    }
    channel_[kChan1].int_mask = kIrqTimer1;
    channel_[kChan2].int_mask = kIrqTimer2;
    channel_[kChan4].int_mask = kIrqTimer4;
    kbcode_ = 0x09;      // Atari 800 'no key'
    skctl_ = kSkReset;   // let the RNG run after reset
    skstat_ = 0;
    irqst_ = 0;
    irqen_ = 0;
    audctl_ = 0;
    serin_ = 0;
    p4_ = p5_ = p9_ = p17_ = 0;
    allpot_ = 0;
    pot_counter_ = 0;
    for (int i = 0; i < 3; ++i) clock_cnt_[i] = 0;
    for (int i = 0; i < 8; ++i) potx_[i] = 0;
}

bool Pokey::Channel::check_borrow() {
    if (borrow_cnt > 0) {
        borrow_cnt--;
        return borrow_cnt == 0;
    }
    return false;
}

void Pokey::inc_chan(Channel& channel) {
    channel.counter = (channel.counter + 1) & 0xff;
    if (channel.counter == 0 && channel.borrow_cnt == 0) {
        channel.borrow_cnt = 3;
        if (irqen_ & channel.int_mask) irqst_ |= channel.int_mask;
    }
}

uint8_t Pokey::read(uint16_t offset) {
    uint8_t data = 0;
    switch (offset & 0x0f) {
        case 0: case 1: case 2: case 3:
        case 4: case 5: case 6: case 7: {
            const uint8_t pot = offset & 7;
            data = (allpot_ & (1 << pot)) ? potx_[pot] : pot_counter_;
            break;
        }
        case kAllPot:
            // ALLPOT is disabled while the interface is held in reset.
            if ((skctl_ & kSkReset) == 0) {
                data = 0;
            } else if (allpot_read_) {
                data = allpot_read_(uint8_t(offset));
            } else {
                data = uint8_t(allpot_ ^ 0xff);
            }
            break;
        case kKbCode:
            data = kbcode_;
            break;
        case kRandom:
            if (audctl_ & kPoly9) {
                data = uint8_t(poly9_[p9_] & 0xff);
            } else {
                data = uint8_t((poly17_[p17_] >> 8) & 0xff);
            }
            break;
        case kSerIn:
            if (serin_read_) serin_ = serin_read_(uint8_t(offset));
            data = serin_;
            break;
        case kIrqSt:
            data = uint8_t(irqst_ ^ 0xff);  // active low port
            break;
        case kSkStat:
            data = uint8_t(skstat_ ^ 0xff);  // active low port
            break;
        default:
            break;
    }
    return data;
}

void Pokey::potgo() {
    allpot_ = 0;
    pot_counter_ = 0;
    for (uint8_t pot = 0; pot < 8; ++pot) {
        potx_[pot] = 228;
        if (!pot_read_[pot]) continue;
        uint8_t r = pot_read_[pot](pot);
        if (r >= 228) r = 228;
        // Without a capacitor the value is available immediately.
        if (r == 0) allpot_ = uint8_t(allpot_ | (1 << pot));
        potx_[pot] = r;
    }
}

void Pokey::write(uint16_t offset, uint8_t data) { write_internal(offset, data); }

void Pokey::write_internal(uint16_t offset, uint8_t data) {
    switch (offset & 0x0f) {
        case 0x00: channel_[kChan1].audf = data; break;
        case 0x01: channel_[kChan1].audc = data; break;
        case 0x02: channel_[kChan2].audf = data; break;
        case 0x03: channel_[kChan2].audc = data; break;
        case 0x04: channel_[kChan3].audf = data; break;
        case 0x05: channel_[kChan3].audc = data; break;
        case 0x06: channel_[kChan4].audf = data; break;
        case 0x07: channel_[kChan4].audc = data; break;
        case kAudCtl: audctl_ = data; break;
        case kSTimer:
            for (int i = 0; i < kChannels; ++i) {
                channel_[i].reset_counter();
                channel_[i].output = 0;
                channel_[i].filter_sample = uint8_t(i < 2 ? 1 : 0);
            }
            break;
        case kSkRest:
            skstat_ = uint8_t(skstat_ & ~(kSkFrame | kSkOverrun | kSkKbErr));
            break;
        case kPotGo:
            potgo();
            break;
        case kSerOut:
            skstat_ |= kSkSerOut;
            break;
        case kIrqEn:
            if (irqst_ & ~data) irqst_ &= data;
            irqen_ = data;
            break;
        case kSkCtl:
            skctl_ = data;
            if ((data & kSkReset) == 0) {
                write_internal(kIrqEn, 0);
                write_internal(kSkRest, 0);
                // The polynomial counters are held in reset as well.
                p9_ = p17_ = p4_ = p5_ = 0;
                clock_cnt_[0] = clock_cnt_[1] = clock_cnt_[2] = 0;
            }
            break;
        default:
            break;
    }
}

void Pokey::process_channel(int ch) {
    if ((channel_[ch].audc & kNotPoly5) || (poly5_[p5_] & 1)) {
        if (channel_[ch].audc & kPure) {
            channel_[ch].output ^= 1;
        } else if (channel_[ch].audc & kPoly4) {
            channel_[ch].output = uint8_t(poly4_[p4_] & 1);
        } else if (audctl_ & kPoly9) {
            channel_[ch].output = uint8_t(poly9_[p9_] & 1);
        } else {
            channel_[ch].output = uint8_t(poly17_[p17_] & 1);
        }
    }
}

void Pokey::step_pot() {
    uint8_t upd = 0;
    pot_counter_++;
    for (int pot = 0; pot < 8; ++pot) {
        if (potx_[pot] < pot_counter_ || pot_counter_ == 228) upd = uint8_t(upd | (1 << pot));
    }
    allpot_ |= upd;
}

void Pokey::step_one_clock() {
    int clock_triggered[3] = {0, 0, 0};
    const int base_clock = (audctl_ & kClk15Khz) ? kClk114 : kClk28;

    if (skctl_ & kSkReset) {
        for (int clk = 0; clk < 3; ++clk) {
            clock_cnt_[clk]++;
            if (clock_cnt_[clk] >= kClockDivisors[clk]) {
                clock_cnt_[clk] = 0;
                clock_triggered[clk] = 1;
            }
        }
        p4_ = (p4_ + 1) % 0x0000f;
        p5_ = (p5_ + 1) % 0x0001f;
        p9_ = (p9_ + 1) % 0x001ff;
        p17_ = (p17_ + 1) % 0x1ffff;

        int clk = (audctl_ & kCh1HiClk) ? kClk1 : base_clock;
        if (clock_triggered[clk]) inc_chan(channel_[kChan1]);
        clk = (audctl_ & kCh3HiClk) ? kClk1 : base_clock;
        if (clock_triggered[clk]) inc_chan(channel_[kChan3]);
        if (clock_triggered[base_clock]) {
            if ((audctl_ & kCh12Joined) == 0) inc_chan(channel_[kChan2]);
            if ((audctl_ & kCh34Joined) == 0) inc_chan(channel_[kChan4]);
        }
        if ((clock_triggered[kClk114] || (skctl_ & kSkPaddle)) && pot_counter_ < 228) step_pot();
    }

    // CHAN2 goes before CHAN1 because CHAN1 may set its borrow.
    if (channel_[kChan2].check_borrow()) {
        if (audctl_ & kCh12Joined) channel_[kChan1].reset_counter();
        channel_[kChan2].reset_counter();
        process_channel(kChan2);
        if ((irqst_ & kIrqTimer2) && irq_handler_) irq_handler_(kIrqTimer2);
    }
    if (channel_[kChan1].check_borrow()) {
        if (audctl_ & kCh12Joined) {
            inc_chan(channel_[kChan2]);
        } else {
            channel_[kChan1].reset_counter();
        }
        process_channel(kChan1);
        if ((irqst_ & kIrqTimer1) && irq_handler_) irq_handler_(kIrqTimer1);
    }
    if (channel_[kChan4].check_borrow()) {
        if (audctl_ & kCh34Joined) channel_[kChan3].reset_counter();
        channel_[kChan4].reset_counter();
        process_channel(kChan4);
        if (audctl_ & kCh2Filter) channel_[kChan2].sample();
        else channel_[kChan2].filter_sample = 1;
        if ((irqst_ & kIrqTimer4) && irq_handler_) irq_handler_(kIrqTimer4);
    }
    if (channel_[kChan3].check_borrow()) {
        if (audctl_ & kCh34Joined) {
            inc_chan(channel_[kChan4]);
        } else {
            channel_[kChan3].reset_counter();
        }
        process_channel(kChan3);
        if (audctl_ & kCh1Filter) channel_[kChan1].sample();
        else channel_[kChan1].filter_sample = 1;
    }

    uint32_t sum = 0;
    for (int ch = 0; ch < kChannels; ++ch) {
        if ((channel_[ch].output ^ channel_[ch].filter_sample) ||
            (channel_[ch].audc & kVolumeOnly)) {
            sum |= uint32_t(channel_[ch].audc & kVolumeMask) << (ch * 4);
        }
    }
    output_ = sum;
}

void Pokey::run(int cycles) {
    for (int i = 0; i < cycles; ++i) step_one_clock();
}

int32_t Pokey::update() const {
    int32_t out = 0;
    for (int i = 0; i < kChannels; ++i) out += int32_t((output_ >> (4 * i)) & 0x0f);
    out = int32_t(out * kDefaultGain * amplitude_);
    return std::min(out, 0x7fff);
}

}  // namespace dsp
