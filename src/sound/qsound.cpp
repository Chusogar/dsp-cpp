#include "sound/qsound.h"

#include <algorithm>
#include <cmath>

namespace dsp {

QSound::QSound(uint32_t sample_rom_size) {
    rom_.assign(sample_rom_size, 0);
    rom_mask_ = sample_rom_size != 0 ? sample_rom_size - 1 : 0;
    for (int index = 0; index <= 32; index++) {
        pan_table_[size_t(index)] = int(std::round((256.0 / std::sqrt(32.0)) * std::sqrt(index)));
    }
    reset();
}

void QSound::set_rom(std::vector<uint8_t> rom) {
    rom_ = std::move(rom);
    rom_mask_ = rom_.empty() ? 0 : uint32_t(rom_.size() - 1);
}

void QSound::reset() {
    channels_ = {};
    data_ = 0;
    out_left_ = 0;
    out_right_ = 0;
}

void QSound::set_command(uint8_t data, uint16_t value) {
    int channel = 99;
    int reg = 99;
    if (data < 0x80) {
        channel = data >> 3;
        reg = data & 7;
    } else if (data < 0x90) {
        channel = data - 0x80;
        reg = 8;
    } else if (data >= 0xba && data < 0xca) {
        channel = data - 0xba;
        reg = 9;
    }
    if (channel < 0 || channel >= kChannels) return;
    Channel& current = channels_[size_t(channel)];
    switch (reg) {
        case 0: {
            int bank_channel = (channel + 1) & 0x0f;
            channels_[size_t(bank_channel)].bank = int(value & 0x7f) << 16;
            break;
        }
        case 1: current.address = value; break;
        case 2:
            current.pitch = int(value) * 16;
            if (value == 0) current.key = 0;
            break;
        case 4: current.loop = value; break;
        case 5: current.end_addr = value; break;
        case 6:
            if (value == 0) {
                current.key = 0;
            } else if (current.key == 0) {
                current.key = 1;
                current.offset = 0;
                current.lastdt = 0;
            }
            current.vol = value;
            break;
        case 8: {
            int pandata = (int(value) - 0x10) & 0x3f;
            if (pandata > 32) pandata = 32;
            current.rvol = pan_table_[size_t(pandata)];
            current.lvol = pan_table_[size_t(32 - pandata)];
            current.pan = value;
            break;
        }
        default: break;
    }
}

void QSound::write(uint8_t offset, uint8_t data) {
    switch (offset & 3) {
        case 0: data_ = uint16_t((data_ & 0x00ff) | (uint16_t(data) << 8)); break;
        case 1: data_ = uint16_t((data_ & 0xff00) | data); break;
        case 2: set_command(data, data_); break;
        default: break;
    }
}

void QSound::clock() {
    out_left_ = 0;
    out_right_ = 0;
    for (Channel& current : channels_) {
        if (current.key == 0) continue;
        int rvol = (current.rvol * current.vol) / 256;
        int lvol = (current.lvol * current.vol) / 256;
        int count = current.offset >> 16;
        current.offset &= 0xffff;
        if (count != 0) {
            current.address += count;
            if (current.address >= current.end_addr) {
                // MAME / hardware: loop == 0 stops the voice. Pascal qsound.pas
                // has this test inverted.
                if (current.loop == 0) {
                    current.key = 0;
                    continue;
                }
                current.address = (current.end_addr - current.loop) & 0xffff;
            }
            uint32_t offset = uint32_t(current.bank + current.address) & rom_mask_;
            current.lastdt = int8_t(offset < rom_.size() ? rom_[offset] : 0);
        }
        out_left_ += (current.lastdt * lvol) / 32;
        out_right_ += (current.lastdt * rvol) / 32;
        current.offset += current.pitch;
    }
    out_left_ = std::clamp(out_left_, -32767, 32767);
    out_right_ = std::clamp(out_right_, -32767, 32767);
}

}  // namespace dsp
