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

// The polynomial counters: positions in the sequence of states of each
// LFSR, brought up to date with the clock only when read. The 4/5/9-bit
// states are small tables. The 17-bit one (512 KB per chip as a table of
// states) is kept as the one sequence of its bit 16, shared by every chip:
// bits 9-16 shift down unchanged, so bits 8-15 of state i are bit 16 of
// states i-8..i-1, and bit 0 (from the tap fed into bit 7) is bit 16 of
// state i-16 xor bit 16 of state i-11.
constexpr int kPoly17Length = 0x1ffff;

// In flash: 16 KB that every chip shares.
const uint32_t kPoly17Bit16[(kPoly17Length + 31) / 32] = {
#include "sound/pokey_poly17.inc"
};

const uint32_t* poly17_bit16() { return kPoly17Bit16; }

inline uint32_t poly17_bit16_at(const uint32_t* bits, int i) {
    if (i < 0) i += kPoly17Length;
    return (bits[unsigned(i) >> 5] >> (i & 31)) & 1;
}

template <int N>
struct PolyTable {
    uint16_t value[N];
};

template <int kSize>
constexpr PolyTable<(1 << kSize) - 1> poly_table_4_5(int xorbit, uint32_t invert) {
    PolyTable<(1 << kSize) - 1> table{};
    uint32_t lfsr = 0;
    for (int i = 0; i < (1 << kSize) - 1; ++i) {
        const uint32_t in = (~(lfsr >> 0) & 1) ^ ((lfsr >> xorbit) & 1);
        lfsr >>= 1;
        lfsr = (in << (kSize - 1)) | lfsr;
        table.value[i] = uint16_t(lfsr ^ invert);
    }
    return table;
}

constexpr PolyTable<511> poly_table_9() {
    PolyTable<511> table{};
    uint32_t lfsr = 0x1ff;
    for (int i = 0; i < 511; ++i) {
        const uint32_t in = ((lfsr >> 0) & 1) ^ ((lfsr >> 5) & 1);
        lfsr >>= 1;
        lfsr = (in << 8) | lfsr;
        table.value[i] = uint16_t(lfsr);
    }
    return table;
}

constexpr PolyTable<15> kPoly4Table = poly_table_4_5<4>(1, 0);
constexpr PolyTable<31> kPoly5Table = poly_table_4_5<5>(2, 1);
constexpr PolyTable<511> kPoly9Table = poly_table_9();

}  // namespace

Pokey::~Pokey() = default;

Pokey::Pokey(uint32_t clock, float amplitude)
    : clock_(clock), amplitude_(amplitude), bits17_(poly17_bit16()) {
    reset();
}

void Pokey::reset_polys() {
    i4_ = i5_ = i9_ = 0;
    i17_ = 0;
    at4_ = at5_ = at9_ = at17_ = ticks_;
}

uint32_t Pokey::poly4() {
    const uint32_t lag = ticks_ - at4_;
    at4_ = ticks_;
    i4_ += lag;
    if (i4_ >= 15u) i4_ %= 15u;
    return kPoly4Table.value[i4_];
}

uint32_t Pokey::poly5() {
    const uint32_t lag = ticks_ - at5_;
    at5_ = ticks_;
    i5_ += lag;
    if (i5_ >= 31u) i5_ %= 31u;
    return kPoly5Table.value[i5_];
}

uint32_t Pokey::poly9() {
    const uint32_t lag = ticks_ - at9_;
    at9_ = ticks_;
    i9_ += lag;
    if (i9_ >= 511u) i9_ %= 511u;
    return kPoly9Table.value[i9_];
}

void Pokey::sync_poly17() {
    const uint32_t lag = ticks_ - at17_;
    at17_ = ticks_;
    i17_ += lag;  // lags stay below 2^30 (see run())
    if (i17_ >= uint32_t(kPoly17Length)) i17_ %= uint32_t(kPoly17Length);
}

uint32_t Pokey::poly17_bit0() {
    sync_poly17();
    const int i = int(i17_);
    return poly17_bit16_at(bits17_, i - 16) ^ poly17_bit16_at(bits17_, i - 11);
}

uint32_t Pokey::poly17_byte() {
    sync_poly17();
    uint32_t value = 0;
    for (int k = 0; k < 8; ++k) value |= poly17_bit16_at(bits17_, int(i17_) - 8 + k) << k;
    return value;
}

void Pokey::reset() {
    output_ = 0;
    output_stale_ = false;
    borrowing_ = 0;
    deferred_ = 0;
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
    reset_polys();
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

void Pokey::set_key(uint8_t code, bool pressed, bool shift_held) {
    if (pressed) {
        kbcode_ = code;
        skstat_ |= 0x04;  // KEYDET: a key is down (SKSTAT reads active low)
        if (irqen_ & 0x40) {
            irqst_ |= 0x40;
            if (irq_handler_) irq_handler_(0x40);
        }
    } else {
        skstat_ = uint8_t(skstat_ & ~0x04);
    }
    if (shift_held) skstat_ |= 0x08; else skstat_ = uint8_t(skstat_ & ~0x08);
}

void Pokey::press_break() {
    if (irqen_ & 0x80) {
        irqst_ |= 0x80;
        if (irq_handler_) irq_handler_(0x80);
    }
}

uint8_t Pokey::read(uint16_t offset) {
    catch_up();
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
                data = uint8_t(poly9() & 0xff);
            } else {
                data = uint8_t(poly17_byte());
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

void Pokey::write(uint16_t offset, uint8_t data) {
    catch_up();
    // The output as the clocks so far left it, before the write changes it.
    if (output_stale_) {
        output_ = compute_output();
        output_stale_ = false;
    }
    write_internal(offset, data);
}

uint32_t Pokey::compute_output() const {
    uint32_t sum = 0;
    for (int ch = 0; ch < kChannels; ++ch) {
        if ((channel_[ch].output ^ channel_[ch].filter_sample) ||
            (channel_[ch].audc & kVolumeOnly)) {
            sum |= uint32_t(channel_[ch].audc & kVolumeMask) << (ch * 4);
        }
    }
    return sum;
}

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
                reset_polys();
                clock_cnt_[0] = clock_cnt_[1] = clock_cnt_[2] = 0;
            }
            break;
        default:
            break;
    }
}

void Pokey::process_channel(int ch) {
    if ((channel_[ch].audc & kNotPoly5) || (poly5() & 1)) {
        if (channel_[ch].audc & kPure) {
            channel_[ch].output ^= 1;
        } else if (channel_[ch].audc & kPoly4) {
            channel_[ch].output = uint8_t(poly4() & 1);
        } else if (audctl_ & kPoly9) {
            channel_[ch].output = uint8_t(poly9() & 1);
        } else {
            channel_[ch].output = uint8_t(poly17_bit0());
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
        ++ticks_;

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
    borrowing_ = uint8_t((channel_[0].borrow_cnt > 0 ? 1 : 0) | (channel_[1].borrow_cnt > 0 ? 2 : 0) |
                         (channel_[2].borrow_cnt > 0 ? 4 : 0) | (channel_[3].borrow_cnt > 0 ? 8 : 0));
}

// Clocks where nothing happens but counting are run in a batch: no borrow
// countdown ends, no 1.79 MHz channel's counter wraps to 0, no divider the
// channels (or the pot scan) use reaches its count. In those clocks the
// dividers, the 1.79 MHz channels' counters, the borrow countdowns, the
// polynomial counters and a fast pot scan only advance. The output is not
// worked out per clock either: it is a function of the channels' state,
// computed when update() wants it (write() keeps the value the clocks left).
void Pokey::run(int cycles) {
    if (cycles <= 0) return;
    // A silent chip (every channel at volume 0, no timer interrupt) only
    // counts: nothing outside sees it until a register is read or written,
    // so the clocks are kept and run then (catch_up()), most often in one
    // jump over the channels' repeating pattern.
    if (silent()) {
        output_stale_ = true;  // 0 from now: every volume is
        deferred_ += uint32_t(cycles);
        if (deferred_ >= 0x10000000u) catch_up();  // keep the lags well inside 32 bits
        return;
    }
    catch_up();
    run_clocks(cycles);
}

bool Pokey::silent() const {
    if (!fast_forward_on_) return false;
    constexpr uint8_t kAllFast = kCh1HiClk | kCh3HiClk | kCh12Joined | kCh34Joined;
    if ((skctl_ & kSkReset) == 0 || (audctl_ & kAllFast) != kAllFast || (irqen_ & 0x07) != 0) return false;
    if (pot_counter_ < 228) return false;
    for (int ch = 0; ch < kChannels; ++ch)
        if (channel_[ch].audc & (kVolumeMask | kVolumeOnly | kPure)) return false;
    return true;
}

void Pokey::catch_up() {
    if (deferred_ == 0) return;
    const uint32_t clocks = deferred_;
    deferred_ = 0;
    if (!fast_forward(clocks)) {
        for (uint32_t done = 0; done < clocks;) {
            const int n = int(std::min<uint32_t>(clocks - done, 0x100000));
            run_clocks(n);
            done += uint32_t(n);
        }
    }
}

void Pokey::run_clocks(int cycles) {
    // Keep the lazy polynomial counters well inside 32-bit lags.
    constexpr uint32_t kStale = 0x40000000u;
    if (ticks_ - at4_ > kStale) poly4();
    if (ticks_ - at5_ > kStale) poly5();
    if (ticks_ - at9_ > kStale) poly9();
    if (ticks_ - at17_ > kStale) sync_poly17();
    if (cycles <= 0) return;
    output_stale_ = true;
    borrowing_ = uint8_t((channel_[0].borrow_cnt > 0 ? 1 : 0) | (channel_[1].borrow_cnt > 0 ? 2 : 0) |
                         (channel_[2].borrow_cnt > 0 ? 4 : 0) | (channel_[3].borrow_cnt > 0 ? 8 : 0));
    // Registers only change through write(), so the clocking is fixed here.
    const bool counting = (skctl_ & kSkReset) != 0;
    const bool hi1 = (audctl_ & kCh1HiClk) != 0, hi3 = (audctl_ & kCh3HiClk) != 0;
    const bool base_used = !hi1 || !hi3 || (audctl_ & kCh12Joined) == 0 || (audctl_ & kCh34Joined) == 0;
    const int base = (audctl_ & kClk15Khz) ? kClk114 : kClk28;
    const bool paddle = (skctl_ & kSkPaddle) != 0;
    while (cycles > 0) {
        int quiet = cycles;
        if (borrowing_) {
            for (int ch = 0; ch < kChannels; ++ch)
                if (channel_[ch].borrow_cnt > 0 && channel_[ch].borrow_cnt - 1 < quiet)
                    quiet = channel_[ch].borrow_cnt - 1;
        }
        if (counting && quiet > 0) {
            if (base_used) {
                const int to = kClockDivisors[base] - clock_cnt_[base] - 1;
                if (to < quiet) quiet = to;
            }
            if (!paddle && pot_counter_ < 228) {
                const int to = kClockDivisors[kClk114] - clock_cnt_[kClk114] - 1;
                if (to < quiet) quiet = to;
            }
            if (hi1 && 255 - channel_[kChan1].counter < quiet) quiet = 255 - channel_[kChan1].counter;
            if (hi3 && 255 - channel_[kChan3].counter < quiet) quiet = 255 - channel_[kChan3].counter;
        }
        if (quiet > 0) {
            if (counting) {
                clock_cnt_[kClk28] = (clock_cnt_[kClk28] + quiet) % kClockDivisors[kClk28];
                clock_cnt_[kClk114] = (clock_cnt_[kClk114] + quiet) % kClockDivisors[kClk114];
                ticks_ += uint32_t(quiet);
                if (hi1) channel_[kChan1].counter += quiet;
                if (hi3) channel_[kChan3].counter += quiet;
                if (paddle && pot_counter_ < 228) {
                    const int steps = std::min(quiet, 228 - int(pot_counter_));
                    pot_counter_ = uint8_t(pot_counter_ + steps);
                    uint8_t upd = pot_counter_ == 228 ? 0xff : 0;
                    for (int pot = 0; pot < 8; ++pot)
                        if (potx_[pot] < pot_counter_) upd = uint8_t(upd | (1 << pot));
                    allpot_ |= upd;
                }
            }
            if (borrowing_)
                for (int ch = 0; ch < kChannels; ++ch)
                    if (channel_[ch].borrow_cnt > 0) channel_[ch].borrow_cnt -= quiet;
            cycles -= quiet;
            continue;
        }
        step_one_clock();
        --cycles;
    }
}

// ---- Jumping a silent chip forward -------------------------------------
//
// With every channel on the 1.79 MHz clock and joined in pairs (1+2, 3+4),
// a pair's counters and borrow countdowns (its "skeleton") step on their
// own, whatever the polynomials and outputs do, and soon repeat. A borrow
// ending is an event: the lead channel's (1 or 3) clocks the slave, the
// slave's reloads both, and each samples a polynomial into its output.
// For N clocks the skeleton is found from its cycle, and each output is the
// polynomial at the last event that took a sample (the 5-bit polynomial
// gates it unless NOTPOLY5); a filter flip-flop is the other output at the
// last event of channel 3 (filter 1) or 4 (filter 2).

namespace {

struct PairState {
    int lead_counter, slave_counter, lead_borrow, slave_borrow;
    bool operator==(const PairState& o) const {
        return lead_counter == o.lead_counter && slave_counter == o.slave_counter &&
               lead_borrow == o.lead_borrow && slave_borrow == o.slave_borrow;
    }
};

// One clock of a joined pair, the lead on the 1.79 MHz clock, in
// step_one_clock's order (the slave's borrow is checked first). Bit 0 of
// the result: the slave's event, bit 1: the lead's.
inline int pair_clock(PairState& p, int lead_reload, int slave_reload) {
    int events = 0;
    p.lead_counter = (p.lead_counter + 1) & 0xff;
    if (p.lead_counter == 0 && p.lead_borrow == 0) p.lead_borrow = 3;
    if (p.slave_borrow > 0 && --p.slave_borrow == 0) {
        p.lead_counter = lead_reload;
        p.slave_counter = slave_reload;
        events |= 1;
    }
    if (p.lead_borrow > 0 && --p.lead_borrow == 0) {
        p.slave_counter = (p.slave_counter + 1) & 0xff;
        if (p.slave_counter == 0 && p.slave_borrow == 0) p.slave_borrow = 3;
        events |= 2;
    }
    return events;
}

}  // namespace

// The events of one pair over `clocks` clocks: those of the first
// `prefix` clocks (before the cycle), then the cycle of `period` clocks
// repeating; each event a clock number (1-based) and who.
struct Pokey::PairPlan {
    PairState end{};
    uint32_t prefix = 0, period = 0, clocks = 0;
    uint16_t count_prefix = 0, count = 0;  // events in the prefix; in prefix + one period
    uint16_t at[2 * kMaxPlanClocks];       // clock number of each event
    uint8_t who[2 * kMaxPlanClocks];       // 1 slave, 2 lead
};

bool Pokey::plan_pair(int lead, int slave, uint32_t clocks, PairPlan& plan) const {
    const int lead_reload = channel_[lead].audf ^ 0xff;
    const int slave_reload = channel_[slave].audf ^ 0xff;
    const PairState start{channel_[lead].counter & 0xff, channel_[slave].counter & 0xff,
                          channel_[lead].borrow_cnt, channel_[slave].borrow_cnt};
    // Brent's cycle detection on the skeleton.
    PairState tortoise = start, hare = start;
    uint32_t power = 1, lambda = 1;
    pair_clock(hare, lead_reload, slave_reload);
    while (!(tortoise == hare)) {
        if (power == lambda) {
            tortoise = hare;
            power *= 2;
            lambda = 0;
        }
        pair_clock(hare, lead_reload, slave_reload);
        ++lambda;
        if (power > kMaxPlanClocks) return false;
    }
    PairState a = start, b = start;
    for (uint32_t i = 0; i < lambda; ++i) pair_clock(b, lead_reload, slave_reload);
    uint32_t mu = 0;
    while (!(a == b)) {
        pair_clock(a, lead_reload, slave_reload);
        pair_clock(b, lead_reload, slave_reload);
        ++mu;
        if (mu + lambda > kMaxPlanClocks) return false;
    }
    plan.prefix = mu;
    plan.period = lambda;
    plan.clocks = clocks;
    // The events of the prefix and of one period.
    PairState p = start;
    plan.count = 0;
    for (uint32_t k = 1; k <= mu + lambda; ++k) {
        if (k == mu + 1) plan.count_prefix = plan.count;
        const int events = pair_clock(p, lead_reload, slave_reload);
        if (events & 1) {
            plan.at[plan.count] = uint16_t(k);
            plan.who[plan.count++] = 1;
        }
        if (events & 2) {
            plan.at[plan.count] = uint16_t(k);
            plan.who[plan.count++] = 2;
        }
    }
    if (mu + lambda == 0) plan.count_prefix = 0;
    // The skeleton after `clocks` clocks.
    PairState e = start;
    const uint32_t steps = clocks <= mu ? clocks : mu + (clocks - mu) % lambda;
    for (uint32_t k = 0; k < steps; ++k) pair_clock(e, lead_reload, slave_reload);
    plan.end = e;
    return true;
}

// The clock number of the last event of `who` at or before clock `limit`
// for which `take(k)` holds, scanning back at most far enough for the
// 31-clock period of the 5-bit polynomial to come round; 0: none.
template <typename Take>
uint32_t Pokey::last_event(const PairPlan& plan, int who, uint32_t limit, Take take) {
    if (limit > plan.clocks) limit = plan.clocks;
    // The repeating part: cycle n (0-based) covers clocks prefix + n * period + [1, period].
    if (plan.period > 0 && limit > plan.prefix && plan.count > plan.count_prefix) {
        const uint32_t n_last = (limit - plan.prefix - 1) / plan.period;
        const uint32_t cycles_back = 32;
        for (uint32_t back = 0; back <= cycles_back && back <= n_last; ++back) {
            const uint32_t n = n_last - back;
            const uint32_t base = plan.prefix + n * plan.period - plan.prefix;
            for (int i = int(plan.count) - 1; i >= int(plan.count_prefix); --i) {
                if (plan.who[i] != who) continue;
                const uint32_t k = base + plan.at[i];
                if (k > limit) continue;
                if (take(k)) return k;
            }
        }
        // Over 33 cycles every phase of the 5-bit polynomial an event can
        // meet has come round: earlier cycles take no sample either.
    }
    for (int i = int(plan.count_prefix) - 1; i >= 0; --i) {
        if (plan.who[i] != who || plan.at[i] > limit) continue;
        if (take(plan.at[i])) return plan.at[i];
    }
    return 0;
}

bool Pokey::fast_forward(uint32_t clocks) {
    if (!silent()) return false;
    if (!plan_a_) plan_a_.reset(new PairPlan);
    if (!plan_b_) plan_b_.reset(new PairPlan);
    PairPlan& a = *plan_a_;
    PairPlan& b = *plan_b_;
    if (!plan_pair(kChan1, kChan2, clocks, a) || !plan_pair(kChan3, kChan4, clocks, b)) return false;

    // Polynomials at clock k of the window (clock k sees ticks_ + k).
    const uint32_t t0 = ticks_;
    auto poly5_at = [&](uint32_t k) {
        return kPoly5Table.value[(i5_ + (t0 + k - at5_) % 31u) % 31u] & 1;
    };
    auto sample_at = [&](int ch, uint32_t k) -> uint8_t {
        if (channel_[ch].audc & kPoly4) return uint8_t(kPoly4Table.value[(i4_ + (t0 + k - at4_) % 15u) % 15u] & 1);
        if (audctl_ & kPoly9) return uint8_t(kPoly9Table.value[(i9_ + (t0 + k - at9_) % 511u) % 511u] & 1);
        const uint32_t n = uint32_t(kPoly17Length);
        const int i = int((i17_ + (t0 + k - at17_) % n) % n);
        return uint8_t(poly17_bit16_at(bits17_, i - 16) ^ poly17_bit16_at(bits17_, i - 11));
    };
    // A channel's output as of clock `limit` (events of that clock included).
    auto output_at = [&](int ch, const PairPlan& plan, int who, uint32_t limit) -> uint8_t {
        const bool gated = (channel_[ch].audc & kNotPoly5) == 0;
        const uint32_t k = last_event(plan, who, limit, [&](uint32_t c) { return !gated || poly5_at(c); });
        return k ? sample_at(ch, k) : channel_[ch].output;
    };
    const uint8_t out1 = output_at(kChan1, a, 2, clocks);
    const uint8_t out2 = output_at(kChan2, a, 1, clocks);
    const uint8_t out3 = output_at(kChan3, b, 2, clocks);
    const uint8_t out4 = output_at(kChan4, b, 1, clocks);
    // Filters: sampled at every borrow of channel 3 (into 1) and 4 (into 2).
    const uint32_t k3 = last_event(b, 2, clocks, [](uint32_t) { return true; });
    const uint32_t k4 = last_event(b, 1, clocks, [](uint32_t) { return true; });
    if (k3) channel_[kChan1].filter_sample = (audctl_ & kCh1Filter) ? output_at(kChan1, a, 2, k3) : 1;
    if (k4) channel_[kChan2].filter_sample = (audctl_ & kCh2Filter) ? output_at(kChan2, a, 1, k4) : 1;
    channel_[kChan1].output = out1;
    channel_[kChan2].output = out2;
    channel_[kChan3].output = out3;
    channel_[kChan4].output = out4;

    channel_[kChan1].counter = a.end.lead_counter;
    channel_[kChan1].borrow_cnt = a.end.lead_borrow;
    channel_[kChan2].counter = a.end.slave_counter;
    channel_[kChan2].borrow_cnt = a.end.slave_borrow;
    channel_[kChan3].counter = b.end.lead_counter;
    channel_[kChan3].borrow_cnt = b.end.lead_borrow;
    channel_[kChan4].counter = b.end.slave_counter;
    channel_[kChan4].borrow_cnt = b.end.slave_borrow;
    // The dividers and the polynomials count on.
    clock_cnt_[kClk28] = int((uint32_t(clock_cnt_[kClk28]) + clocks) % uint32_t(kClockDivisors[kClk28]));
    clock_cnt_[kClk114] = int((uint32_t(clock_cnt_[kClk114]) + clocks) % uint32_t(kClockDivisors[kClk114]));
    ticks_ += clocks;
    poly4();
    poly5();
    poly9();
    sync_poly17();
    output_stale_ = true;
    return true;
}

int32_t Pokey::update() const {
    if (output_stale_) {
        output_ = compute_output();
        output_stale_ = false;
    }
    int32_t out = 0;
    for (int i = 0; i < kChannels; ++i) out += int32_t((output_ >> (4 * i)) & 0x0f);
    // At amplitude 1 the float product is the integer itself.
    out = amplitude_ == 1.0f ? out * kDefaultGain : int32_t(out * kDefaultGain * amplitude_);
    return std::min<int32_t>(out, 0x7fff);
}

}  // namespace dsp
