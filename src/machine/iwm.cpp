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
    drive_ = 0;
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

void Iwm::update_devsel() {
    // MAME iwm_device::control: while active, bit 5 picks drive 2 else 1.
    const int drive = (control_ & 0x10) ? ((control_ & 0x20) ? 2 : 1) : 0;
    if (drive == drive_) return;
    drive_ = drive;
    nibble_pos_ = 0;
}

uint8_t Iwm::next_nibble() {
    if (drive_ != 1 || !drive_motor_ || !disk_.loaded()) return 0;
    const auto& nib = disk_.nibbles(track_, hdsel_ ? 1 : 0);
    if (nib.empty()) return 0;
    if (nibble_pos_ < 0 || nibble_pos_ >= int(nib.size())) nibble_pos_ = 0;
    const uint8_t value = nib[size_t(nibble_pos_++)];
    if (nibble_pos_ >= int(nib.size())) nibble_pos_ = 0;
    return value;
}

uint8_t Iwm::sense() const {
    // MAME iwm status bit 7 is (!floppy || floppy->wpt_r()). No drive
    // selected leaves the sense line pulled up.
    if (drive_ == 0) return 1;

    const int reg = (phases_ & 7) | (hdsel_ ? 8 : 0);
    const bool internal = drive_ == 1;
    const bool has_disk = internal && disk_.loaded();
    const bool motor = internal && drive_motor_;

    // MAME mac_floppy_device::wpt_r / mfd51w_device (add_35). Drive 2 is
    // an empty MFD51W on the external connector: present, no disk, GCR.
    switch (reg) {
        case 0x0:  // Dir
            return dir_out_ ? 0 : 1;
        case 0x1:  // Step (MAME skips the delay → ready)
            return (internal && stepping_) ? 0 : 1;
        case 0x2:  // Motor (0 = running)
            return motor ? 0 : 1;
        case 0x3:  // Eject / disk change
            return 1;
        case 0x4:
        case 0xc:
            return 0;
        case 0x5:  // Superdrive? MFD51W has no MFM
            return 0;
        case 0x6:  // DoubleSide: MFD51W is always two-headed
            return 1;
        case 0x7:  // NoDrive (0 = present)
            return 0;
        case 0x8:  // NoDiskInPl
            return has_disk ? 0 : 1;
        case 0x9:  // NoWrProtect
            return 1;
        case 0xa:  // NotTrack0 — unused drive sits on cylinder 0
            return (internal && track_ != 0) ? 1 : 0;
        case 0xb: {  // Tachometer, 120 inversions/rotation
            if (!has_disk || !motor) return 0;
            const int64_t phase = tach_period_ <= 0 ? 0 : (cycles_ / tach_period_);
            return int(phase & 1);
        }
        case 0xd:  // MFMModeOn — IWM GCR drive stays GCR
            return 0;
        case 0xe:  // NoReady
            return (has_disk && motor) ? 0 : 1;
        case 0xf:  // new interface / 2M — MFD51W::is_2m() is true
            return 1;
        default:
            return 0;
    }
}

void Iwm::strobe_command() {
    // MAME mac_floppy_device::seek_phase_w on the IWM LSTRB rising edge.
    // Commands apply to the selected drive; only the internal unit has media.
    if (drive_ != 1) return;
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
            // MAME unload() on StartEject. A real Sony needs LSTRB held
            // ~750 ms; the Plus ROM strobes this during boot without a disk.
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
        update_devsel();
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
                handshake_ = 0xbf;
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
