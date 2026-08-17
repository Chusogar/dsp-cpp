#pragma once

#include <cstdint>

#include "machine/trdos_disk.h"
#include "machine/wd1793.h"

namespace dsp {

// Beta 128 disk interface: WD1793 plus the system latch at port $FF.
class Beta128 {
public:
    void reset();
    void enable() { active_ = true; }
    void disable() { active_ = false; }
    bool active() const { return active_; }

    bool load_disk(const std::string& path, std::string* error);
    bool disk_present() const { return disk_.present(); }
    void eject() { disk_.eject(); }

    uint8_t status_r() { return active_ ? fdc_.status_r() : 0xff; }
    uint8_t track_r() { return active_ ? fdc_.track_r() : 0xff; }
    uint8_t sector_r() { return active_ ? fdc_.sector_r() : 0xff; }
    uint8_t data_r() { return active_ ? fdc_.data_r() : 0xff; }
    uint8_t state_r() const;

    void command_w(uint8_t value) {
        if (active_) fdc_.command_w(value);
    }
    void track_w(uint8_t value) {
        if (active_) fdc_.track_w(value);
    }
    void sector_w(uint8_t value) {
        if (active_) fdc_.sector_w(value);
    }
    void data_w(uint8_t value) {
        if (active_) fdc_.data_w(value);
    }
    void param_w(uint8_t value);

private:
    Wd1793 fdc_;
    TrdosDisk disk_;
    bool active_ = false;
    uint8_t control_ = 0x3c;
};

}  // namespace dsp
