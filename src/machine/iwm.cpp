#include "machine/iwm.h"

#include <algorithm>

namespace dsp {
namespace {

int rpm_for_track(int track) {
    if (track <= 15) return 394;
    if (track <= 31) return 429;
    if (track <= 47) return 472;
    if (track <= 63) return 525;
    return 590;
}

}  // namespace

void Iwm::reset() {
    phases_ = 0;
    control_ = 0;
    mode_ = 0;
    status_ = 0;
    data_ = 0;
    handshake_ = 0xbf;
    last_strobe_ = false;
    dir_out_ = true;
    drive_motor_ = false;
    stepping_ = false;
    track_ = 0;
    nibble_pos_ = 0;
    cycles_ = 0;
    tach_period_ = int64_t(7833600) * 60 / (394 * 120);
    select_track();
}

void Iwm::set_hdsel(bool side) {
    if (hdsel_ == side) return;
    hdsel_ = side;
    nibble_pos_ = 0;
}

void Iwm::select_track() {
    const int rpm = rpm_for_track(track_);
    tach_period_ = int64_t(7833600) * 60 / (int64_t(rpm) * 120);
    if (tach_period_ < 1) tach_period_ = 1;
    nibble_pos_ = 0;
}

void Iwm::tick(int cycles) {
    cycles_ += cycles;
    stepping_ = false;
}

bool Iwm::load_file(const std::string& path, std::string* error) {
    if (!disk_.load_file(path, error)) return false;
    select_track();
    return true;
}

uint8_t Iwm::next_nibble() {
    if (!drive_motor_ || !disk_.loaded()) return 0;
    const auto& nib = disk_.nibbles(track_, hdsel_ ? 1 : 0);
    if (nib.empty()) return 0;
    if (nibble_pos_ < 0 || nibble_pos_ >= int(nib.size())) nibble_pos_ = 0;
    const uint8_t value = nib[size_t(nibble_pos_++)];
    if (nibble_pos_ >= int(nib.size())) nibble_pos_ = 0;
    return value;
}

uint8_t Iwm::sense() const {
    const int reg = (phases_ & 7) | (hdsel_ ? 8 : 0);
    switch (reg) {
        case 0x0:  // step direction
            return dir_out_ ? 0 : 1;
        case 0x1:  // step in progress
            return stepping_ ? 0 : 1;
        case 0x2:  // motor on (0 = running)
            return drive_motor_ ? 0 : 1;
        case 0x3:  // eject / disk change
            return 1;
        case 0x4:
        case 0xc:
            return 0;
        case 0x5:  // superdrive
            return 0;
        case 0x6:  // double sided
            return disk_.sides() > 1 ? 1 : 0;
        case 0x7:  // drive present (0 = yes)
            return 0;
        case 0x8:  // no disk
            return disk_.loaded() ? 0 : 1;
        case 0x9:  // not write protected
            return 1;
        case 0xa:  // not track 0
            return track_ != 0 ? 1 : 0;
        case 0xb: {  // tachometer
            if (!disk_.loaded() || !drive_motor_) return 0;
            const int64_t phase = tach_period_ <= 0 ? 0 : (cycles_ / tach_period_);
            return int(phase & 1);
        }
        case 0xd:  // MFM mode
            return 0;
        case 0xe:  // ready (0 = ready)
            return (disk_.loaded() && drive_motor_) ? 0 : 1;
        case 0xf:  // new interface / 2M
            return 1;
        default:
            return 0;
    }
}

void Iwm::strobe_command() {
    const int reg = (phases_ & 7) | (hdsel_ ? 8 : 0);
    switch (reg) {
        case 0x0:
            dir_out_ = true;
            break;
        case 0x1:
            if (dir_out_) {
                if (track_ < 79) track_++;
            } else {
                if (track_ > 0) track_--;
            }
            stepping_ = true;
            select_track();
            break;
        case 0x2:
            drive_motor_ = true;
            break;
        case 0x4:
            dir_out_ = false;
            break;
        case 0x6:
            drive_motor_ = false;
            break;
        case 0x7:
            // Brief eject strobes during Plus boot must be ignored; a real
            // drive needs LSTRB held for ~750 ms.
            break;
        default:
            break;
    }
}

uint8_t Iwm::read(uint8_t offset) { return access(offset, 0, false); }

void Iwm::write(uint8_t offset, uint8_t data) { access(offset, data, true); }

uint8_t Iwm::access(uint8_t offset, uint8_t data, bool is_write) {
    offset &= 0x0f;
    if (offset < 8) {
        if (offset & 1)
            phases_ = uint8_t(phases_ | (1 << (offset >> 1)));
        else
            phases_ = uint8_t(phases_ & ~(1 << (offset >> 1)));
        const bool strobe = (phases_ & 0x08) != 0;
        if (strobe && !last_strobe_) strobe_command();
        last_strobe_ = strobe;
    } else {
        if (offset & 1)
            control_ = uint8_t(control_ | (1 << (offset >> 1)));
        else
            control_ = uint8_t(control_ & ~(1 << (offset >> 1)));
    }

    const bool enable = (control_ & 0x10) != 0;
    if (enable)
        status_ = uint8_t((status_ & 0xdf) | 0x20);
    else
        status_ = uint8_t(status_ & 0xdf);

    if (is_write && (offset & 1) && (control_ & 0xc0) == 0xc0) {
        if (enable) {
            data_ = data;
            handshake_ = 0xc0;  // ready for next byte, no underrun
        } else {
            mode_ = data;
            status_ = uint8_t((status_ & 0xe0) | (mode_ & 0x1f));
        }
    }

    switch (control_ & 0xc0) {
        case 0x00:
            if (enable) {
                data_ = next_nibble();
                return data_;
            }
            return 0xff;
        case 0x40:
            return uint8_t((status_ & 0x7f) | (sense() ? 0x80 : 0x00));
        case 0x80:
            return handshake_;
        case 0xc0:
        default:
            return 0xff;
    }
}

}  // namespace dsp
