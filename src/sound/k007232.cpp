#include "sound/k007232.h"

#include <algorithm>
#include <cmath>

namespace dsp {

K007232::K007232(uint32_t clock, std::vector<uint8_t> rom, float amplify, VolumeCallback cb,
                 bool stereo)
    : clock_(clock),
      rom_(std::move(rom)),
      amplify_(amplify),
      callback_(std::move(cb)),
      stereo_(stereo) {
    // fncode table from k007232.pas: (32 << BASE_SHIFT) / (0x200 - i)
    for (int i = 0; i < 0x200; i++) {
        fncode_[size_t(i)] = uint32_t(std::lround((32 << kBaseShift) / double(0x200 - i)));
    }
    reset();
}

void K007232::reset() {
    wreg_.fill(0);
    address_.fill(0);
    start_.fill(0);
    step_.fill(0);
    bank_.fill(0);
    play_.fill(false);
    vol_[0][0] = 255;
    vol_[0][1] = 0;
    vol_[1][0] = 0;
    vol_[1][1] = 255;
    sample_accum_ = 0;
}

void K007232::set_volume(int channel, uint8_t vol_a, uint8_t vol_b) {
    if (channel < 0 || channel >= kChannels) return;
    vol_[size_t(channel)][0] = vol_a;
    vol_[size_t(channel)][1] = vol_b;
}

void K007232::set_bank(int bank_a, int bank_b) {
    bank_[0] = uint32_t(bank_a) << 17;
    bank_[1] = uint32_t(bank_b) << 17;
}

uint8_t K007232::read(uint8_t address) const {
    // Pascal: reading $5/$b triggers start (side effect) — non-const in Pascal.
    // We only return 0; start is triggered on write of those regs.
    return 0;
}

void K007232::write(uint8_t address, uint8_t value) {
    // Faithful to k007232.pas write()
    uint8_t r = address & 0x0f;
    wreg_[r] = value;

    if (r == 0x0c) {
        if (callback_) callback_(value);
        return;
    }
    if (r == 0x0d) return;  // loop flags

    int reg_port = 0;
    if (r >= 0x06) {
        reg_port = 1;
        r = uint8_t(r - 0x06);
    }

    switch (r) {
        case 0:
        case 1: {
            // address step
            const uint16_t idx =
                uint16_t(((wreg_[size_t(reg_port * 6 + 1)] << 8) & 0x100) | wreg_[size_t(reg_port * 6)]);
            step_[size_t(reg_port)] = fncode_[idx & 0x1ff];
            break;
        }
        case 2:
        case 3:
        case 4:
            // start address parts stored in wreg only
            break;
        case 5: {
            // start address key-on
            start_[size_t(reg_port)] =
                ((uint32_t(wreg_[size_t(reg_port * 6 + 4)] & 1) << 16) |
                 (uint32_t(wreg_[size_t(reg_port * 6 + 3)]) << 8) |
                 uint32_t(wreg_[size_t(reg_port * 6 + 2)]) | bank_[size_t(reg_port)]);
            if (start_[size_t(reg_port)] < rom_.size()) {
                play_[size_t(reg_port)] = true;
                address_[size_t(reg_port)] = 0;
            }
            break;
        }
        default:
            break;
    }
}

int32_t K007232::update() {
    // Faithful to k007232.pas internal_update (one sample step).
    int32_t out1 = 0, out2 = 0;
    for (int f = 0; f < kChannels; f++) {
        if (!play_[size_t(f)]) continue;

        uint32_t addr =
            start_[size_t(f)] + ((address_[size_t(f)] >> kBaseShift) & 0xfffff);
        const uint16_t volA = uint16_t(vol_[size_t(f)][0]) * 2;
        const uint16_t volB = uint16_t(vol_[size_t(f)][1]) * 2;

        uint32_t old_addr = addr;
        addr = start_[size_t(f)] + ((address_[size_t(f)] >> kBaseShift) & 0xfffff);
        while (old_addr <= addr) {
            if (old_addr >= rom_.size() || (rom_[old_addr] & 0x80) != 0) {
                // end of sample
                if ((wreg_[0x0d] & (1 << f)) != 0) {
                    // loop
                    start_[size_t(f)] =
                        ((uint32_t(wreg_[size_t(f * 6 + 4)] & 1) << 16) |
                         (uint32_t(wreg_[size_t(f * 6 + 3)]) << 8) |
                         uint32_t(wreg_[size_t(f * 6 + 2)]) | bank_[size_t(f)]);
                    addr = start_[size_t(f)];
                    address_[size_t(f)] = 0;
                    old_addr = addr;
                } else {
                    play_[size_t(f)] = false;
                }
                break;
            }
            old_addr++;
        }
        if (!play_[size_t(f)]) continue;

        address_[size_t(f)] += step_[size_t(f)];
        if (addr < rom_.size()) {
            const int32_t pcm = int32_t(rom_[addr] & 0x7f) - 0x40;
            out1 += pcm * int32_t(volA);
            out2 += pcm * int32_t(volB);
        }
    }

    // Also handle key-on via read of $5/$b (Pascal side effect) — not needed if games write $5.

    const int32_t mixed = stereo_ ? ((out1 + out2) / 2) : out1;
    return int32_t(float(mixed) * amplify_);
}

}  // namespace dsp
