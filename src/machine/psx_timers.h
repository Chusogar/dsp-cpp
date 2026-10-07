// PlayStation root counters (timers 0–2).
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <cstdint>

namespace dsp {

struct PsxGpuSync {
    int dot_div = 1;
    bool hblank = false;
    bool vblank = false;
};

class PsxTimers {
public:
    void reset();

    uint32_t load(uint32_t addr) const;
    void write(uint32_t addr, uint32_t value);

    void sync_gpu(const PsxGpuSync& sync);

    // Returns true if the timer raised an interrupt this tick.
    bool tick(int timer_number, int cycles);

private:
    struct Timer {
        explicit Timer(int number) : number(number) {}

        uint32_t load(uint32_t addr) const;
        void write(uint32_t addr, uint32_t value);
        void sync_gpu(const PsxGpuSync& sync);
        bool tick(int cycles_ticked);
        bool handle_irq();
        void set_mode(uint32_t value);
        uint32_t get_mode() const;

        int number = 0;
        uint32_t counter = 0;
        uint32_t target = 0;

        uint8_t sync_enable = 0;
        uint8_t sync_mode = 0;
        uint8_t reset_on_target = 0;
        uint8_t irq_on_target = 0;
        uint8_t irq_on_ffff = 0;
        uint8_t irq_repeat = 0;
        uint8_t irq_pulse = 0;
        uint8_t clock_source = 0;
        uint8_t interrupt_request = 1;
        mutable uint8_t reached_target = 0;
        mutable uint8_t reached_ffff = 0;

        bool vblank = false;
        bool hblank = false;
        int dot_div = 1;
        bool prev_hblank = false;
        bool prev_vblank = false;
        bool already_fired = false;
        int cycle_accum = 0;
    };

    Timer timers_[3]{Timer(0), Timer(1), Timer(2)};
};

}  // namespace dsp
