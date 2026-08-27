#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Namco 06XX bus multiplexer between the main CPU and up to four Namco
// MB88xx I/O peripherals (51xx input/coin, 52xx sample player, 53xx I/O,
// 54xx sound trigger).
//
// Control register format ($7100 on this hardware family):
//   bits 0-3: bitmask of which slots are addressed by this access (more
//             than one bit can be set; real hardware wire-ANDs their reads)
//   bit 4:    transfer direction - 0 = the main CPU is about to WRITE to the
//             selected slot(s), 1 = it is about to READ from them
//
// NMI timer (matches leniad/dsp-emulator namcoio_06xx_5Xxx.pas):
//   When the low nibble is non-zero the chip runs a free-running timer of
//   fixed period 768 main-CPU cycles (~250 us @ 3.072 MHz) that pulses the
//   host's NMI line.  The timer does NOT modify the control register; the
//   Z80 NMI handler is responsible for writing $10 (or clearing the low
//   nibble) when the transfer is complete.  Bits 5-7 are ignored (unlike
//   MAME's variable-divisor model used on Pole Position).
class Namco06xx {
public:
    // Fixed NMI period used by Galaga / Bosconian / Dig Dug / Xevious
    // (Pascal: timers.init(0, 768, nmi_function, nil, false)).
    static constexpr int kNmiPeriodCycles = 768;

    struct Slot {
        std::function<void(bool)> chip_select;
        std::function<void(bool)> set_rw;   // true = CPU is reading
        std::function<void(uint8_t)> write;
        std::function<uint8_t()> read;
    };

    void set_slot(int index, Slot slot) {
        if (index >= 0 && index < 4) slots_[size_t(index)] = std::move(slot);
    }
    void set_nmi_callback(std::function<void()> cb) { nmi_callback_ = std::move(cb); }

    void reset() {
        control_ = 0;
        nmi_accumulator_ = 0;
        nmi_enabled_ = false;
        for (auto& sel : selected_) sel = false;
        for (auto& s : slots_)
            if (s.chip_select) s.chip_select(false);
    }

    void ctrl_write(uint8_t value) {
        control_ = value;
        const uint8_t mask = value & 0x0f;
        const bool read_mode = (value & 0x10) != 0;
        for (int i = 0; i < 4; i++) {
            const bool sel = (mask & (1 << i)) != 0;
            if (sel != selected_[size_t(i)]) {
                selected_[size_t(i)] = sel;
                if (slots_[size_t(i)].chip_select) slots_[size_t(i)].chip_select(sel);
            }
            if (sel && slots_[size_t(i)].set_rw) slots_[size_t(i)].set_rw(read_mode);
        }
        // Pascal: if (data and $f)=0 then disable timer else enable.
        if (mask == 0) {
            nmi_enabled_ = false;
            nmi_accumulator_ = 0;
        } else {
            nmi_enabled_ = true;
            // Restart the period on each ctrl write (same as timers.enabled=true
            // after a previous fire in the Pascal timer engine).
            nmi_accumulator_ = 0;
            // Read-request: notify selected chips when bit4 is set.
            if (read_mode) {
                for (int i = 0; i < 4; i++)
                    if ((mask & (1 << i)) != 0 && slots_[size_t(i)].set_rw)
                        slots_[size_t(i)].set_rw(true);
            }
        }
    }
    uint8_t ctrl_read() const { return control_; }

    void data_write(uint8_t /*offset*/, uint8_t value) {
        if ((control_ & 0x10) != 0) return;  // only writes while bit4 is clear
        const uint8_t mask = control_ & 0x0f;
        for (int i = 0; i < 4; i++)
            if ((mask & (1 << i)) != 0 && slots_[size_t(i)].write) slots_[size_t(i)].write(value);
    }
    uint8_t data_read(uint8_t /*offset*/) const {
        if ((control_ & 0x10) == 0) return 0;  // real hardware reads back 0 here
        uint8_t res = 0xff;
        const uint8_t mask = control_ & 0x0f;
        for (int i = 0; i < 4; i++)
            if ((mask & (1 << i)) != 0 && slots_[size_t(i)].read) res &= slots_[size_t(i)].read();
        return res;
    }

    // Call once per main-CPU cycle_handler tick (or accumulated cycles).
    void tick(int cycles, uint32_t /*cpu_clock*/) {
        if (!nmi_enabled_) return;
        nmi_accumulator_ += cycles;
        while (nmi_accumulator_ >= kNmiPeriodCycles) {
            nmi_accumulator_ -= kNmiPeriodCycles;
            if (nmi_callback_) nmi_callback_();
        }
    }

private:
    std::array<Slot, 4> slots_;
    std::array<bool, 4> selected_{};
    std::function<void()> nmi_callback_;
    uint8_t control_ = 0;
    int64_t nmi_accumulator_ = 0;
    bool nmi_enabled_ = false;
};

}  // namespace dsp
