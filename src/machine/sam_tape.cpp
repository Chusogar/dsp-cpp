#include "machine/sam_tape.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace dsp {
namespace {
// Standard ZX timings, in 3.5 MHz T-states.
constexpr uint32_t kPilot = 2168;
constexpr uint32_t kSync1 = 667;
constexpr uint32_t kSync2 = 735;
constexpr uint32_t kZero = 855;
constexpr uint32_t kOne = 1710;
constexpr int kPilotHeader = 8063;   // pulses for a header block
constexpr int kPilotData = 3223;     // pulses for a data block
constexpr uint32_t kTStatesPerMs = 3500;

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return !out.empty();
}
}  // namespace

void SamTape::eject() {
    pulses_.clear();
    index_ = 0;
    counter_ = 0;
    frac_ = 0;
    ear_ = false;
    playing_ = false;
}

void SamTape::rewind() {
    index_ = 0;
    counter_ = pulses_.empty() ? 0 : int64_t(to_sam(pulses_[0]));
    frac_ = 0;
    ear_ = false;
}

uint32_t SamTape::to_sam(uint32_t zx_tstates) {
    // The SAM runs at 6 MHz against the Spectrum's 3.5 MHz. Carry the
    // remainder between pulses so the error doesn't accumulate over a
    // whole tape.
    const uint64_t v = uint64_t(zx_tstates) * 6000000ull + frac_;
    frac_ = v % 3500000ull;
    return uint32_t(v / 3500000ull);
}

void SamTape::emit_pause(uint32_t pause_ms) {
    if (pause_ms == 0) return;
    pulses_.push_back(pause_ms * kTStatesPerMs);
}

void SamTape::emit_data_block(const uint8_t* data, size_t len, uint32_t pilot,
                              uint32_t sync1, uint32_t sync2, uint32_t zero,
                              uint32_t one, int pilot_pulses, int used_bits,
                              uint32_t pause_ms) {
    for (int i = 0; i < pilot_pulses; i++) pulses_.push_back(pilot);
    if (sync1) pulses_.push_back(sync1);
    if (sync2) pulses_.push_back(sync2);
    for (size_t i = 0; i < len; i++) {
        // The final byte of a block may carry fewer than eight valid bits.
        const int bits = (i + 1 == len) ? used_bits : 8;
        for (int b = 0; b < bits; b++) {
            const uint32_t w = (data[i] & (0x80 >> b)) ? one : zero;
            pulses_.push_back(w);
            pulses_.push_back(w);
        }
    }
    emit_pause(pause_ms);
}

bool SamTape::parse_tap(const std::vector<uint8_t>& data, std::string* error) {
    size_t pos = 0;
    while (pos + 2 <= data.size()) {
        const size_t len = size_t(data[pos]) | (size_t(data[pos + 1]) << 8);
        pos += 2;
        if (len == 0 || pos + len > data.size()) break;
        // The flag byte picks the pilot length: headers get the long tone.
        const int pilot = data[pos] < 128 ? kPilotHeader : kPilotData;
        emit_data_block(data.data() + pos, len, kPilot, kSync1, kSync2, kZero,
                        kOne, pilot, 8, 1000);
        pos += len;
    }
    if (pulses_.empty()) {
        if (error) *error = "no usable blocks in TAP file";
        return false;
    }
    return true;
}

bool SamTape::parse_tzx(const std::vector<uint8_t>& data, std::string* error) {
    size_t pos = 10;  // past "ZXTape!" signature, EOF marker and version
    auto u16 = [&](size_t o) { return uint32_t(data[o]) | (uint32_t(data[o + 1]) << 8); };
    auto u24 = [&](size_t o) {
        return uint32_t(data[o]) | (uint32_t(data[o + 1]) << 8) | (uint32_t(data[o + 2]) << 16);
    };
    while (pos < data.size()) {
        const uint8_t id = data[pos++];
        switch (id) {
            case 0x10: {  // standard speed data
                if (pos + 4 > data.size()) return true;
                const uint32_t pause = u16(pos);
                const size_t len = u16(pos + 2);
                pos += 4;
                if (pos + len > data.size()) return true;
                const int pilot = data[pos] < 128 ? kPilotHeader : kPilotData;
                emit_data_block(data.data() + pos, len, kPilot, kSync1, kSync2,
                                kZero, kOne, pilot, 8, pause);
                pos += len;
                break;
            }
            case 0x11: {  // turbo speed data: every timing is specified
                if (pos + 18 > data.size()) return true;
                const uint32_t pilot = u16(pos), sync1 = u16(pos + 2), sync2 = u16(pos + 4);
                const uint32_t zero = u16(pos + 6), one = u16(pos + 8);
                const int pilot_pulses = int(u16(pos + 10));
                const int used_bits = data[pos + 12];
                const uint32_t pause = u16(pos + 13);
                const size_t len = u24(pos + 15);
                pos += 18;
                if (pos + len > data.size()) return true;
                emit_data_block(data.data() + pos, len, pilot, sync1, sync2, zero,
                                one, pilot_pulses, used_bits ? used_bits : 8, pause);
                pos += len;
                break;
            }
            case 0x12: {  // pure tone
                if (pos + 4 > data.size()) return true;
                const uint32_t len = u16(pos);
                const uint32_t count = u16(pos + 2);
                pos += 4;
                for (uint32_t i = 0; i < count; i++) pulses_.push_back(len);
                break;
            }
            case 0x13: {  // sequence of individual pulses
                if (pos >= data.size()) return true;
                const int count = data[pos++];
                if (pos + size_t(count) * 2 > data.size()) return true;
                for (int i = 0; i < count; i++) pulses_.push_back(u16(pos + size_t(i) * 2));
                pos += size_t(count) * 2;
                break;
            }
            case 0x14: {  // pure data, no pilot or sync
                if (pos + 10 > data.size()) return true;
                const uint32_t zero = u16(pos), one = u16(pos + 2);
                const int used_bits = data[pos + 4];
                const uint32_t pause = u16(pos + 5);
                const size_t len = u24(pos + 7);
                pos += 10;
                if (pos + len > data.size()) return true;
                emit_data_block(data.data() + pos, len, 0, 0, 0, zero, one, 0,
                                used_bits ? used_bits : 8, pause);
                pos += len;
                break;
            }
            case 0x20: {  // pause / stop the tape
                if (pos + 2 > data.size()) return true;
                emit_pause(u16(pos));
                pos += 2;
                break;
            }
            case 0x21: { if (pos >= data.size()) return true; pos += 1 + data[pos]; break; }
            case 0x22: break;                                    // group end
            case 0x30: { if (pos >= data.size()) return true; pos += 1 + data[pos]; break; }
            case 0x31: { if (pos + 1 >= data.size()) return true; pos += 2 + data[pos + 1]; break; }
            case 0x32: { if (pos + 2 > data.size()) return true; pos += 2 + u16(pos); break; }
            case 0x33: { if (pos >= data.size()) return true; pos += 1 + size_t(data[pos]) * 3; break; }
            case 0x35: {                                          // custom info
                if (pos + 14 > data.size()) return true;
                const uint32_t len = uint32_t(data[pos + 10]) | (uint32_t(data[pos + 11]) << 8) |
                                     (uint32_t(data[pos + 12]) << 16) | (uint32_t(data[pos + 13]) << 24);
                pos += 14 + len;
                break;
            }
            case 0x5a: pos += 9; break;                           // glue block
            default:
                // An unknown block can't be skipped safely, so stop here and
                // keep whatever was decoded up to this point.
                if (error) *error = "unsupported TZX block 0x" + std::to_string(int(id));
                return !pulses_.empty();
        }
    }
    if (pulses_.empty()) {
        if (error) *error = "no usable blocks in TZX file";
        return false;
    }
    return true;
}

bool SamTape::load(const std::string& path, std::string* error) {
    std::vector<uint8_t> data;
    if (!read_file(path, data)) {
        if (error) *error = "cannot open tape image: " + path;
        return false;
    }
    eject();
    bool ok;
    if (data.size() > 10 && std::memcmp(data.data(), "ZXTape!\x1a", 8) == 0) {
        ok = parse_tzx(data, error);
    } else {
        ok = parse_tap(data, error);
    }
    if (!ok) {
        pulses_.clear();
        return false;
    }
    rewind();
    playing_ = true;   // start rolling so a LOAD finds signal straight away
    return true;
}

void SamTape::tick(int sam_tstates) {
    if (!playing_ || index_ >= pulses_.size()) return;
    counter_ -= sam_tstates;
    while (counter_ <= 0) {
        ear_ = !ear_;
        if (++index_ >= pulses_.size()) {
            playing_ = false;
            return;
        }
        counter_ += int64_t(to_sam(pulses_[index_]));
    }
}

}  // namespace dsp
