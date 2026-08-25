#include "sound/vlm5030.h"

namespace dsp {
namespace {

constexpr int kFrSize = 4;
constexpr int kIpSizeSlower = 240 / kFrSize;
constexpr int kIpSizeSlow = 200 / kFrSize;
constexpr int kIpSizeNormal = 160 / kFrSize;
constexpr int kIpSizeFast = 120 / kFrSize;
constexpr int kIpSizeFaster = 80 / kFrSize;

const uint8_t kSpeedTable[8] = {
    kIpSizeNormal, kIpSizeFast, kIpSizeFaster, kIpSizeFaster,
    kIpSizeNormal, kIpSizeSlower, kIpSizeSlow, kIpSizeSlow,
};

const uint8_t kEnergyTable[32] = {
    0,  1,  2,  3,  5,  6,  7,  9,  11, 13, 15, 17, 19, 22, 24, 27,
    31, 34, 38, 42, 47, 51, 57, 62, 68, 75, 82, 89, 98, 107, 116, 127,
};

const uint8_t kPitchTable[32] = {
    0,  21, 22, 23, 24, 25, 26, 27, 28, 29, 31, 33, 35, 37, 39, 41,
    43, 45, 49, 53, 57, 61, 65, 69, 73, 77, 85, 93, 101, 109, 117, 125,
};

const int k1Table[64] = {
    390,  403,  414,  425,  434,  443,  450,  457,  463,  469,  474,  478,  482,  485,  488,  491,
    494,  496,  498,  499,  501,  502,  503,  504,  505,  506,  507,  507,  508,  508,  509,  509,
    -390, -376, -360, -344, -325, -305, -284, -261, -237, -211, -183, -155, -125, -95,  -64,  -32,
    0,    32,   64,   95,   125,  155,  183,  211,  237,  261,  284,  305,  325,  344,  360,  376,
};

const int k2Table[32] = {
    0,    50,   100,  149,  196,  241,  284,  325,  362,  396,  426,  452,  473,  490,  502,  510,
    0,    -510, -502, -490, -473, -452, -426, -396, -362, -325, -284, -241, -196, -149, -100, -50,
};

const int k3Table[16] = {0, 64, 128, 192, 256, 320, 384, 448, -512, -448, -384, -320, -256, -192, -128, -64};

const int k5Table[8] = {0, 128, 256, 384, -512, -384, -256, -128};

}  // namespace

Vlm5030::Vlm5030(uint32_t clock, size_t rom_size, float amplitude)
    : clock_(clock ? clock : 1), amplitude_(amplitude), rom_(rom_size, 0),
      address_mask_(rom_size ? uint32_t(rom_size - 1) : 0) {
    reset();
    phase_ = PhIdle;
    setup_parameter(0);
}

void Vlm5030::set_rom(const std::vector<uint8_t>& rom) {
    rom_.assign(rom.begin(), rom.end());
    if (rom_.empty()) rom_.assign(1, 0);
    address_mask_ = uint32_t(rom_.size() - 1);
}

int Vlm5030::cycles_per_sample(uint32_t cpu_clock) const {
    // Pascal: sound_status.cpu_clock / (clock / 440)
    const uint32_t vlm_rate = clock_ / 440;
    if (vlm_rate == 0) return 440;
    return int((cpu_clock + vlm_rate / 2) / vlm_rate);
}

void Vlm5030::reset() {
    phase_ = PhReset;
    address_ = 0;
    vcu_addr_h_ = 0;
    pin_bsy_ = 0;
    old_energy_ = 0;
    old_pitch_ = 0;
    new_energy_ = 0;
    new_pitch_ = 0;
    current_energy_ = 0;
    current_pitch_ = 0;
    target_energy_ = 0;
    target_pitch_ = 0;
    old_k_.fill(0);
    new_k_.fill(0);
    current_k_.fill(0);
    target_k_.fill(0);
    interp_count_ = 0;
    sample_count_ = 0;
    pitch_count_ = 0;
    x_.fill(0);
    out_ = 0;
    setup_parameter(0);
}

uint8_t Vlm5030::rom_byte(uint32_t address) const {
    if (rom_.empty()) return 0;
    return rom_[address & address_mask_];
}

void Vlm5030::data_w(uint8_t data) { latch_data_ = data; }

void Vlm5030::setup_parameter(uint8_t param) {
    parameter_ = param;
    if (param & 2) {
        interp_step_ = 4;  // 9600 bps
    } else if (param & 1) {
        interp_step_ = 2;  // 4800 bps
    } else {
        interp_step_ = 1;  // 2400 bps
    }
    frame_size_ = kSpeedTable[(param >> 3) & 7];
    if (param & 0x80) {
        pitch_offset_ = -8;
    } else if (param & 0x40) {
        pitch_offset_ = 8;
    } else {
        pitch_offset_ = 0;
    }
}

void Vlm5030::set_rst(uint8_t pin) {
    if (pin_rst_ == pin) return;
    if (pin == 0) {
        pin_rst_ = 0;
        setup_parameter(latch_data_);
    } else {
        pin_rst_ = 1;
        if (pin_bsy_ != 0) reset();
    }
}

void Vlm5030::update_vcu(uint8_t pin) { pin_vcu_ = pin; }

void Vlm5030::set_st(uint8_t pin) {
    if (pin_st_ == pin) return;
    if (pin == 0) {
        pin_st_ = 0;
        if (pin_vcu_ != 0) {
            vcu_addr_h_ = uint16_t((uint16_t(latch_data_) << 8) + 1);
        } else {
            if (vcu_addr_h_ != 0) {
                address_ = uint16_t((vcu_addr_h_ & 0xff00) + latch_data_);
                vcu_addr_h_ = 0;
            } else {
                uint16_t table = uint16_t((latch_data_ & 0xfe) + ((latch_data_ & 1) << 8));
                address_ = uint16_t(((uint16_t(rom_byte(table)) << 8) | rom_byte(table + 1)) &
                                    address_mask_);
            }
            sample_count_ = uint8_t(frame_size_);
            interp_count_ = kFrSize;
            phase_ = PhRun;
        }
    } else {
        pin_st_ = 1;
        phase_ = PhSetup;
        sample_count_ = 1;
        pin_bsy_ = 1;
    }
}

uint16_t Vlm5030::get_bits(uint8_t sbit, uint8_t bits) const {
    uint32_t address = uint32_t(address_) + (sbit >> 3);
    uint16_t data = uint16_t(rom_byte(address) | (uint16_t(rom_byte(address + 1)) << 8));
    data = uint16_t(data >> (sbit & 7));
    data = uint16_t(data & (0xff >> (8 - bits)));
    return data;
}

int Vlm5030::parse_frame() {
    old_energy_ = new_energy_;
    old_pitch_ = new_pitch_;
    old_k_ = new_k_;

    uint8_t cmd = rom_byte(address_);
    if (cmd & 1) {
        new_energy_ = 0;
        new_pitch_ = 0;
        new_k_.fill(0);
        address_ = uint16_t((address_ + 1) & address_mask_);
        if (cmd & 2) return 0;
        int nums = ((cmd >> 2) + 1) * 2;
        return nums * kFrSize;
    }

    new_pitch_ = kPitchTable[get_bits(1, 5)];
    if (new_pitch_ > 0) new_pitch_ = uint8_t(int(new_pitch_) + pitch_offset_);
    new_energy_ = kEnergyTable[get_bits(6, 5)];
    new_k_[9] = k5Table[get_bits(11, 3)];
    new_k_[8] = k5Table[get_bits(14, 3)];
    new_k_[7] = k5Table[get_bits(17, 3)];
    new_k_[6] = k5Table[get_bits(20, 3)];
    new_k_[5] = k5Table[get_bits(23, 3)];
    new_k_[4] = k5Table[get_bits(26, 3)];
    new_k_[3] = k3Table[get_bits(29, 4)];
    new_k_[2] = k3Table[get_bits(33, 4)];
    new_k_[1] = k2Table[get_bits(37, 5)];
    new_k_[0] = k1Table[get_bits(42, 6)];
    address_ = uint16_t((address_ + 6) & address_mask_);
    return kFrSize;
}

void Vlm5030::update_stream() {
    if (phase_ == PhRun || phase_ == PhStop) {
        if (sample_count_ == 0) {
            if (phase_ == PhStop) {
                phase_ = PhEnd;
                sample_count_ = 1;
                goto phase_stop;
            }
            sample_count_ = uint8_t(frame_size_);
            if (interp_count_ == 0) {
                interp_count_ = parse_frame();
                if (interp_count_ == 0) {
                    interp_count_ = kFrSize;
                    sample_count_ = uint8_t(frame_size_);
                    phase_ = PhStop;
                }
                current_energy_ = old_energy_;
                current_pitch_ = old_pitch_;
                current_k_ = old_k_;
                if (current_energy_ == 0) {
                    target_energy_ = 0;
                    target_pitch_ = uint8_t(current_pitch_);
                    target_k_ = current_k_;
                } else {
                    target_energy_ = new_energy_;
                    target_pitch_ = new_pitch_;
                    target_k_ = new_k_;
                }
            }
            interp_count_ -= interp_step_;
            int interp_effect = kFrSize - (interp_count_ % kFrSize);
            current_energy_ = uint32_t(old_energy_ + (int(target_energy_) - int(old_energy_)) *
                                                         interp_effect / kFrSize);
            if (old_pitch_ > 1) {
                current_pitch_ = uint32_t(old_pitch_ + (int(target_pitch_) - int(old_pitch_)) *
                                                           interp_effect / kFrSize);
            }
            for (int i = 0; i < kNumK; i++) {
                current_k_[size_t(i)] =
                    old_k_[size_t(i)] +
                    (target_k_[size_t(i)] - old_k_[size_t(i)]) * interp_effect / kFrSize;
            }
        }

        int current_val = 0;
        if (old_energy_ == 0) {
            current_val = 0;
        } else if (old_pitch_ <= 1) {
            rng_ = rng_ * 1103515245u + 12345u;
            current_val = (rng_ & 0x100) ? int(current_energy_) : -int(current_energy_);
        } else {
            current_val = (pitch_count_ == 0) ? int(current_energy_) : 0;
        }

        int u[11];
        u[10] = current_val;
        for (int i = 9; i >= 0; i--) {
            u[i] = u[i + 1] - ((-current_k_[size_t(i)] * x_[size_t(i)]) / 512);
        }
        for (int i = 9; i >= 1; i--) {
            x_[size_t(i)] = x_[size_t(i - 1)] + ((-current_k_[size_t(i - 1)] * u[i - 1]) / 512);
        }
        x_[0] = u[0];
        out_ = u[0] * 64;
        if (out_ > 32768) out_ = 32768;
        else if (out_ < -32767) out_ = -32767;
        out_ = int32_t(out_ * amplitude_);

        sample_count_ = uint8_t(sample_count_ - 1);
        pitch_count_ = uint8_t(pitch_count_ + 1);
        if (pitch_count_ >= current_pitch_) pitch_count_ = 0;
        return;
    }

phase_stop:
    switch (phase_) {
        case PhSetup:
            if (sample_count_ <= 1) {
                sample_count_ = 0;
                phase_ = PhWait;
            } else {
                sample_count_ = uint8_t(sample_count_ - 1);
            }
            break;
        case PhEnd:
            if (sample_count_ <= 1) {
                sample_count_ = 0;
                pin_bsy_ = 0;
                phase_ = PhIdle;
            } else {
                sample_count_ = uint8_t(sample_count_ - 1);
            }
            break;
        default: break;
    }
    out_ = 0;
}

}  // namespace dsp
