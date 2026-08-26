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
//   bit 4: transfer direction - 0 = the main CPU is about to WRITE to the
//          selected slot(s), 1 = it is about to READ from them
// The 06XX clock divider generates the host NMI at approximately 4 kHz on
// the Galaga CPU board. With a 3.072 MHz Z80 this is one NMI every 768 CPU
// cycles. The old implementation accidentally generated one NMI every 4000
// cycles (768 Hz), which can stall Galaga's 51XX communication during boot.
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
        nmi_accumulator_ = 0;
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
    }
    uint8_t ctrl_read() const { return control_; }

    void data_write(uint8_t /*offset*/, uint8_t value) {
        if ((control_ & 0x10) != 0) return;
        const uint8_t mask = control_ & 0x0f;
        for (int i = 0; i < 4; i++)
            if ((mask & (1 << i)) != 0 && slots_[size_t(i)].write) slots_[size_t(i)].write(value);
    }
    uint8_t data_read(uint8_t /*offset*/) const {
        if ((control_ & 0x10) == 0) return 0;
        uint8_t res = 0xff;
        const uint8_t mask = control_ & 0x0f;
        for (int i = 0; i < 4; i++)
            if ((mask & (1 << i)) != 0 && slots_[size_t(i)].read) res &= slots_[size_t(i)].read();
        return res;
    }

    // Galaga: 3.072 MHz / 768 = 4 kHz NMI clock.
    // Count actual Z80 cycles so variable-sized instruction batches do not
    // introduce timing drift.
    void tick(int cycles, uint32_t /*cpu_clock*/) {
        if ((control_ & 0x0f) == 0) return;
        constexpr int64_t kNmiDivider = 768;
        nmi_accumulator_ += cycles;
        while (nmi_accumulator_ >= kNmiDivider) {
            nmi_accumulator_ -= kNmiDivider;
            if (nmi_callback_) nmi_callback_();
        }
    }

private:
    std::array<Slot, 4> slots_;
    std::array<bool, 4> selected_{};
    std::function<void()> nmi_callback_;
    uint8_t control_ = 0;
    int64_t nmi_accumulator_ = 0;
};

}  // namespace dsp
