#include "machine/slapstic.h"

namespace dsp {
namespace {

constexpr uint32_t kUnknown = 0xffff;

const Slapstic::Config kSlapstic101 = {
    3,
    {0x0080, 0x0090, 0x00a0, 0x00b0},
    {0x007f, kUnknown}, {0x1fff, 0x1dff}, {0x1ffc, 0x1b5c}, {0x1fcf, 0x0080},
    0,
    {0x1ff0, 0x1540}, {0x1ff3, 0x1540}, {0x1ff3, 0x1541}, {0x1ff3, 0x1542},
    {0x1ff3, 0x1543}, {0x1ff8, 0x1550},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}, {kUnknown, kUnknown},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}};

const Slapstic::Config kSlapstic103 = {
    3,
    {0x0040, 0x0050, 0x0060, 0x0070},
    {0x007f, 0x002d}, {0x3fff, 0x3d14}, {0x3ffc, 0x3d24}, {0x3fcf, 0x0040},
    0,
    {0x3ff0, 0x34c0}, {0x3ff3, 0x34c0}, {0x3ff3, 0x34c1}, {0x3ff3, 0x34c2},
    {0x3ff3, 0x34c3}, {0x3ff8, 0x34d0},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}, {kUnknown, kUnknown},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}};

const Slapstic::Config kSlapstic104 = {
    3,
    {0x0020, 0x0028, 0x0030, 0x0038},
    {0x007f, 0x0069}, {0x3fff, 0x3735}, {0x3ffc, 0x3764}, {0x3fe7, 0x0020},
    0,
    {0x3ff0, 0x3d90}, {0x3ff3, 0x3d90}, {0x3ff3, 0x3d91}, {0x3ff3, 0x3d92},
    {0x3ff3, 0x3d93}, {0x3ff8, 0x3da0},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}, {kUnknown, kUnknown},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}};

const Slapstic::Config kSlapstic105 = {
    3,
    {0x0010, 0x0014, 0x0018, 0x001c},
    {0x007f, 0x003d}, {0x3fff, 0x0092}, {0x3ffc, 0x00a4}, {0x3ff3, 0x0010},
    0,
    {0x3ff0, 0x35b0}, {0x3ff3, 0x35b0}, {0x3ff3, 0x35b1}, {0x3ff3, 0x35b2},
    {0x3ff3, 0x35b3}, {0x3ff8, 0x35c0},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}, {kUnknown, kUnknown},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}};

const Slapstic::Config kSlapstic106 = {
    3,
    {0x0008, 0x000a, 0x000c, 0x000e},
    {0x007f, 0x002b}, {0x3fff, 0x0052}, {0x3ffc, 0x0064}, {0x3ff9, 0x0008},
    0,
    {0x3ff0, 0x3da0}, {0x3ff3, 0x3da0}, {0x3ff3, 0x3da1}, {0x3ff3, 0x3da2},
    {0x3ff3, 0x3da3}, {0x3ff8, 0x3db0},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}, {kUnknown, kUnknown},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}};

const Slapstic::Config kSlapstic107 = {
    3,
    {0x0018, 0x001a, 0x001c, 0x001e},
    {0x007f, 0x006b}, {0x3fff, 0x3d52}, {0x3ffc, 0x3d64}, {0x3ff9, 0x0018},
    0,
    {0x3ff0, 0x00a0}, {0x3ff3, 0x00a0}, {0x3ff3, 0x00a1}, {0x3ff3, 0x00a2},
    {0x3ff3, 0x00a3}, {0x3ff8, 0x00b0},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}, {kUnknown, kUnknown},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}};

const Slapstic::Config kSlapstic108 = {
    3,
    {0x0028, 0x002a, 0x002c, 0x002e},
    {0x007f, 0x001f}, {0x3fff, 0x3772}, {0x3ffc, 0x3764}, {0x3ff9, 0x0028},
    0,
    {0x3ff0, 0x0060}, {0x3ff3, 0x0060}, {0x3ff3, 0x0061}, {0x3ff3, 0x0062},
    {0x3ff3, 0x0063}, {0x3ff8, 0x0070},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}, {kUnknown, kUnknown},
    {kUnknown, kUnknown}, {kUnknown, kUnknown}};

bool matches(uint32_t value, const Slapstic::MaskValue& mask_value) {
    return (value & mask_value.mask) == mask_value.value;
}

const Slapstic::Config& config_for(int number) {
    switch (number) {
        case 101: return kSlapstic101;
        case 103: return kSlapstic103;
        case 104: return kSlapstic104;
        case 105: return kSlapstic105;
        case 106: return kSlapstic106;
        case 108: return kSlapstic108;
        default: return kSlapstic107;
    }
}

}  // namespace

Slapstic::Slapstic(int number, M68000* cpu) : config_(config_for(number)), cpu_(cpu) { reset(); }

void Slapstic::set_type(int number) {
    config_ = config_for(number);
    reset();
}

void Slapstic::reset() {
    state_ = kDisabled;
    current_bank_ = uint8_t(config_.bankstart);
}

uint8_t Slapstic::alt2_kludge() {
    if (cpu_ == nullptr) return kAlternate2;
    // The first alternate address is usually consumed by an opcode fetch, so the
    // access is reconstructed from the instruction that is being executed.
    if (!matches(cpu_->pc() >> 1, config_.alt1)) return kEnabled;
    const uint16_t opcode = cpu_->peek_word(cpu_->ppc() & 0xffffff);
    if ((opcode & 0xf1f8) == 0x3090 || (opcode & 0xf1f8) == 0xb148) {
        const uint32_t regval = cpu_->a[(opcode >> 9) & 7].l >> 1;
        if (matches(regval, config_.alt3)) {
            alt_bank_ = uint8_t((regval >> config_.altshift) & 3);
            return kAlternate3;
        }
    }
    return kAlternate2;
}

uint8_t Slapstic::tweak(uint16_t offset) {
    const bool is_bank = offset == config_.bank[0] || offset == config_.bank[1] ||
                         offset == config_.bank[2] || offset == config_.bank[3];

    if (offset == 0x0000) {
        state_ = kEnabled;
        return current_bank_;
    }

    switch (state_) {
        case kDisabled:
            break;
        case kEnabled:
            if (matches(offset, config_.bit1)) {
                state_ = kBitwise1;
            } else if (matches(offset, config_.add1)) {
                state_ = kAdditive1;
            } else if (matches(offset, config_.alt1)) {
                state_ = kAlternate1;
            } else if (matches(offset, config_.alt2)) {
                state_ = alt2_kludge();
            } else if (is_bank) {
                state_ = kDisabled;
                for (uint8_t bank = 0; bank < 4; ++bank) {
                    if (offset == config_.bank[bank]) current_bank_ = bank;
                }
            }
            break;
        case kAlternate1:
            state_ = matches(offset, config_.alt2) ? kAlternate2 : kEnabled;
            break;
        case kAlternate2:
            if (matches(offset, config_.alt3)) {
                state_ = kAlternate3;
                alt_bank_ = uint8_t((offset >> config_.altshift) & 3);
            } else {
                state_ = kEnabled;
            }
            break;
        case kAlternate3:
            if (matches(offset, config_.alt4)) {
                state_ = kDisabled;
                current_bank_ = alt_bank_;
            }
            break;
        case kBitwise1:
            if (is_bank) {
                state_ = kBitwise2;
                bit_bank_ = current_bank_;
                bit_xor_ = 0;
            }
            break;
        case kBitwise2:
            if (matches(offset ^ bit_xor_, config_.bit2c0)) {
                bit_bank_ &= 0xfe;
                bit_xor_ ^= 3;
            } else if (matches(offset ^ bit_xor_, config_.bit2s0)) {
                bit_bank_ |= 1;
                bit_xor_ ^= 3;
            } else if (matches(offset ^ bit_xor_, config_.bit2c1)) {
                bit_bank_ &= 0xfd;
                bit_xor_ ^= 3;
            } else if (matches(offset ^ bit_xor_, config_.bit2s1)) {
                bit_bank_ |= 2;
                bit_xor_ ^= 3;
            } else if (matches(offset, config_.bit3)) {
                state_ = kBitwise3;
            }
            break;
        case kBitwise3:
            if (is_bank) {
                state_ = kDisabled;
                current_bank_ = bit_bank_;
            }
            break;
        case kAdditive1:
            if (matches(offset, config_.add2)) {
                state_ = kAdditive2;
                add_bank_ = current_bank_;
            } else {
                state_ = kEnabled;
            }
            break;
        case kAdditive2:
            if (matches(offset, config_.addplus1)) add_bank_ = uint8_t((add_bank_ + 1) & 3);
            if (matches(offset, config_.addplus2)) add_bank_ = uint8_t((add_bank_ + 2) & 3);
            if (matches(offset, config_.add3)) state_ = kAdditive3;
            break;
        default:  // kAdditive3
            if (is_bank) {
                state_ = kDisabled;
                current_bank_ = add_bank_;
            }
            break;
    }
    return current_bank_;
}

}  // namespace dsp
