#include "machine/beta128.h"

namespace dsp {

void Beta128::reset() {
    fdc_.reset();
    fdc_.set_disk(&disk_);
    active_ = false;
    control_ = 0x3c;
    fdc_.set_drive(0);
    fdc_.set_side(0);
}

bool Beta128::load_disk(const std::string& path, std::string* error) {
    if (!disk_.load_file(path, error)) return false;
    fdc_.set_disk(&disk_);
    return true;
}

uint8_t Beta128::state_r() const {
    if (!active_) return 0xff;
    uint8_t value = 0x3f;
    if (fdc_.drq()) value |= 0x40;
    if (fdc_.intrq()) value |= 0x80;
    return value;
}

void Beta128::param_w(uint8_t value) {
    if (!active_) return;
    control_ = value;
    fdc_.set_drive(value & 3);
    // Bit 4 selects the head with inverted polarity: 0 picks side 1.
    fdc_.set_side((value & 0x10) ? 0 : 1);
    if ((value & 0x04) == 0) fdc_.reset();
}

}  // namespace dsp
