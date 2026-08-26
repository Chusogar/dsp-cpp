#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Namco 06XX bus multiplexer. The upper three control bits select the
// internal clock divider; bit 4 selects read/write and bits 0..3 select the
// attached 5xXX devices. The 06XX clock generates the host NMI and gates the
// chip-select lines. This timing is essential during Galaga POST.
class Namco06xx {
public:
    struct Slot {
        std::function<void(bool)> chip_select;
        std::function<void(bool)> set_rw;
        std::function<void(uint8_t)> write;
        std::function<uint8_t()> read;
    };

    void set_slot(int index, Slot slot) {
        if (index >= 0 && index < 4) slots_[size_t(index)] = std::move(slot);
    }
    void set_nmi_callback(std::function<void()> cb) { nmi_callback_ = std::move(cb); }

    void reset() {
        control_ = 0;
        clock_accumulator_ = 0;
        timer_state_ = false;
        read_stretch_ = false;
        for (auto& sel : selected_) sel = false;
        for (auto& s : slots_)
            if (s.chip_select) s.chip_select(false);
    }

    void ctrl_write(uint8_t value) {
        control_ = value;
        clock_accumulator_ = 0;
        timer_state_ = false;

        if ((control_ & 0xe0) == 0) {
            read_stretch_ = false;
            for (int i = 0; i < 4; i++) {
                selected_[size_t(i)] = false;
                if (slots_[size_t(i)].chip_select) slots_[size_t(i)].chip_select(false);
            }
            return;
        }

        // A read starts with one suppressed NMI, giving the selected MCU a
        // half-clock to place its value on the bus (MAME's read_stretch).
        read_stretch_ = (control_ & 0x10) != 0;
    }

    uint8_t ctrl_read() const { return control_; }

    void data_write(uint8_t /*offset*/, uint8_t value) {
        if ((control_ & 0x10) != 0) return;
        const uint8_t mask = control_ & 0x0f;
        for (int i = 0; i < 4; i++)
            if ((mask & (1 << i)) != 0 && slots_[size_t(i)].write)
                slots_[size_t(i)].write(value);
    }

    uint8_t data_read(uint8_t /*offset*/) const {
        if ((control_ & 0x10) == 0) return 0;
        uint8_t result = 0xff;
        const uint8_t mask = control_ & 0x0f;
        for (int i = 0; i < 4; i++)
            if ((mask & (1 << i)) != 0 && slots_[size_t(i)].read)
                result &= slots_[size_t(i)].read();
        return result;
    }

    // Galaga 06XX input clock is 18.432 MHz / 8 / 64 = 36 kHz.
    // Control bits 7..5 select a further divide-by-1..8. The device toggles
    // its internal clock each half-period; on the high phase it strobes the
    // selected MCU(s) and raises the host NMI, except for the first read
    // phase which is suppressed by read_stretch.
    void tick(int cycles, uint32_t cpu_clock) {
        if ((control_ & 0xe0) == 0 || cycles <= 0 || cpu_clock == 0) return;

        const uint32_t shifts = uint32_t((control_ >> 5) & 7);
        const uint32_t divisor = 1u << shifts;
        constexpr uint64_t input_clock = 36'000;

        // Accumulate half-periods using integer arithmetic so no floating
        // point timing drift is introduced.
        const uint64_t half_num = uint64_t(cpu_clock) * divisor;
        const uint64_t half_den = input_clock * 2u;
        clock_accumulator_ += uint64_t(cycles) * half_den;

        while (clock_accumulator_ >= half_num) {
            clock_accumulator_ -= half_num;
            timer_state_ = !timer_state_;

            if (timer_state_) {
                const bool read = (control_ & 0x10) != 0;
                const uint8_t mask = control_ & 0x0f;
                for (int i = 0; i < 4; i++) {
                    const bool selected = (mask & (1 << i)) != 0;
                    selected_[size_t(i)] = selected;
                    if (selected && slots_[size_t(i)].set_rw)
                        slots_[size_t(i)].set_rw(read);
                    if (slots_[size_t(i)].chip_select)
                        slots_[size_t(i)].chip_select(selected);
                }

                if (!read_stretch && nmi_callback_)
                    nmi_callback_();
                read_stretch_ = false;
            } else {
                for (int i = 0; i < 4; i++) {
                    if (selected_[size_t(i)] && slots_[size_t(i)].chip_select)
                        slots_[size_t(i)].chip_select(false);
                }
            }
        }
    }

private:
    std::array<Slot, 4> slots_;
    std::array<bool, 4> selected_{};
    std::function<void()> nmi_callback_;
    uint8_t control_ = 0;
    uint64_t clock_accumulator_ = 0;
    bool timer_state_ = false;
    bool read_stretch_ = false;
};

}  // namespace dsp
