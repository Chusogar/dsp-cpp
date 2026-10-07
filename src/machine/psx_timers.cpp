// PlayStation root counters.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#include "machine/psx_timers.h"

#include <algorithm>

namespace dsp {

void PsxTimers::reset() {
    timers_[0] = Timer(0);
    timers_[1] = Timer(1);
    timers_[2] = Timer(2);
}

uint32_t PsxTimers::load(uint32_t addr) const {
    const int n = int((addr & 0xF0) >> 4);
    if (n > 2) return 0xFFFFFFFFu;
    return timers_[n].load(addr);
}

void PsxTimers::write(uint32_t addr, uint32_t value) {
    const int n = int((addr & 0xF0) >> 4);
    if (n > 2) return;
    timers_[n].write(addr, value);
}

void PsxTimers::sync_gpu(const PsxGpuSync& sync) {
    timers_[0].sync_gpu(sync);
    timers_[1].sync_gpu(sync);
}

bool PsxTimers::tick(int timer_number, int cycles) {
    if (timer_number < 0 || timer_number > 2) return false;
    return timers_[timer_number].tick(cycles);
}

uint32_t PsxTimers::Timer::load(uint32_t addr) const {
    switch (addr & 0xF) {
        case 0x0: return counter;
        case 0x4: return get_mode();
        case 0x8: return target;
        default: return 0;
    }
}

void PsxTimers::Timer::write(uint32_t addr, uint32_t value) {
    switch (addr & 0xF) {
        case 0x0: counter = uint16_t(value); break;
        case 0x4: set_mode(value); break;
        case 0x8: target = value; break;
        default: break;
    }
}

void PsxTimers::Timer::sync_gpu(const PsxGpuSync& sync) {
    prev_hblank = hblank;
    prev_vblank = vblank;
    dot_div = sync.dot_div;
    hblank = sync.hblank;
    vblank = sync.vblank;
}

bool PsxTimers::Timer::tick(int cycles_ticked) {
    cycle_accum += cycles_ticked;
    switch (number) {
        case 0:
            if (sync_enable == 1) {
                switch (sync_mode) {
                    case 0: if (hblank) return false; break;
                    case 1: if (hblank) counter = 0; break;
                    case 2:
                        if (hblank) counter = 0;
                        if (!hblank) return false;
                        break;
                    case 3:
                        if (!prev_hblank && hblank) sync_enable = 0;
                        else return false;
                        break;
                }
            }
            if (clock_source == 0 || clock_source == 2) {
                counter += uint16_t(cycle_accum);
                cycle_accum = 0;
            } else {
                const uint16_t dots = uint16_t(cycle_accum * 11 / 7 / std::max(dot_div, 1));
                counter += dots;
                cycle_accum = 0;
            }
            return handle_irq();

        case 1:
            if (sync_enable == 1) {
                switch (sync_mode) {
                    case 0: if (vblank) return false; break;
                    case 1: if (vblank) counter = 0; break;
                    case 2:
                        if (vblank) counter = 0;
                        if (!vblank) return false;
                        break;
                    case 3:
                        if (!prev_vblank && vblank) sync_enable = 0;
                        else return false;
                        break;
                }
            }
            if (clock_source == 0 || clock_source == 2) {
                counter += uint16_t(cycle_accum);
                cycle_accum = 0;
            } else {
                if (!prev_hblank && hblank) counter += 1;
                cycle_accum = 0;
            }
            return handle_irq();

        case 2:
            if (sync_enable == 1 && (sync_mode == 0 || sync_mode == 3)) {
                return false;
            }
            if (clock_source == 0 || clock_source == 1) {
                counter += uint16_t(cycle_accum);
                cycle_accum = 0;
            } else {
                counter += uint16_t(cycle_accum / 8);
                cycle_accum %= 8;
            }
            return handle_irq();

        default:
            return false;
    }
}

bool PsxTimers::Timer::handle_irq() {
    bool irq = false;
    if (counter >= target) {
        reached_target = 1;
        if (reset_on_target == 1) counter = 0;
        if (irq_on_target == 1) irq = true;
    }
    if (counter >= 0xFFFF) {
        reached_ffff = 1;
        if (irq_on_ffff == 1) irq = true;
    }
    counter &= 0xFFFF;
    if (!irq) return false;

    if (irq_pulse == 0) {
        interrupt_request = 0;
    } else {
        interrupt_request = uint8_t((interrupt_request + 1) & 1);
    }
    bool trigger = interrupt_request == 0;
    if (irq_repeat == 0) {
        if (!already_fired && trigger) {
            already_fired = true;
        } else {
            return false;
        }
    }
    interrupt_request = 1;
    return trigger;
}

void PsxTimers::Timer::set_mode(uint32_t value) {
    sync_enable = uint8_t(value & 1);
    sync_mode = uint8_t((value >> 1) & 3);
    reset_on_target = uint8_t((value >> 3) & 1);
    irq_on_target = uint8_t((value >> 4) & 1);
    irq_on_ffff = uint8_t((value >> 5) & 1);
    irq_repeat = uint8_t((value >> 6) & 1);
    irq_pulse = uint8_t((value >> 7) & 1);
    clock_source = uint8_t((value >> 8) & 3);
    interrupt_request = 1;
    already_fired = false;
    counter = 0;
}

uint32_t PsxTimers::Timer::get_mode() const {
    uint32_t mode = 0;
    mode |= sync_enable;
    mode |= uint32_t(sync_mode) << 1;
    mode |= uint32_t(reset_on_target) << 3;
    mode |= uint32_t(irq_on_target) << 4;
    mode |= uint32_t(irq_on_ffff) << 5;
    mode |= uint32_t(irq_repeat) << 6;
    mode |= uint32_t(irq_pulse) << 7;
    mode |= uint32_t(clock_source) << 8;
    mode |= uint32_t(interrupt_request) << 10;
    mode |= uint32_t(reached_target) << 11;
    mode |= uint32_t(reached_ffff) << 12;
    reached_target = 0;
    reached_ffff = 0;
    return mode;
}

}  // namespace dsp
