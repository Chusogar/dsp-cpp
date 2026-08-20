#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cpu/irq_line.h"
#include "cpu/m6502.h"
#include "machine/d64_image.h"
#include "machine/gcr.h"
#include "machine/g64_image.h"
#include "machine/via6522.h"

namespace dsp {

// Commodore 1541 disk drive.
// MOS 6502 @ 1 MHz, VIA1 ($1800), VIA2 ($1C00), 2 KiB RAM,
// 16 KiB DOS ROM, GCR bit-stream disk and IEC serial bus.
//
// IEC lines are open-collector, active low.
// true = released/high through pull-up.
// false = pulled low.
class C1541 {
public:
    static constexpr uint32_t kClock = 1000000;

    C1541();

    void reset();

    // Run cycles PHI2 ticks. Call from the C64 host once per host batch.
    void run(int cycles);

    bool load_rom(const std::string& path, std::string* error = nullptr);
    bool load_rom(const uint8_t* data, size_t size);

    bool load_d64(const std::string& path, std::string* error = nullptr);
    bool load_d64(const uint8_t* data, size_t size, std::string* error = nullptr);

    bool load_g64(const std::string& path, std::string* error = nullptr);
    bool load_g64(const uint8_t* data, size_t size, std::string* error = nullptr);

    void set_write_protect(bool on) { write_protect_ = on; }
    void set_weak_bits(bool on) { weak_bits_ = on; }

    // IEC bus, active-low levels.
    // Host drives these outputs before run().
    void set_host_atn(bool high);
    void set_host_clk(bool high);
    void set_host_data(bool high);

    // Combined bus state after drive outputs, for the host CIA2 inputs.
    bool bus_atn() const;
    bool bus_clk() const;
    bool bus_data() const;

    bool rom_loaded() const { return rom_loaded_; }
    bool disk_loaded() const { return disk_.open(); }

    D64Image& disk() { return disk_; }

    // Front panel state.
    bool motor_on() const { return motor_on_; }
    bool led_on() const { return led_on_; }
    int half_track() const { return half_track_; }

private:
    uint8_t read_mem(uint16_t addr);
    void write_mem(uint16_t addr, uint8_t value);

    void update_iec();
    void update_via1_inputs();
    void update_via2_inputs();

    void update_iec_outputs();
    void on_via2_pb(uint8_t value);
    void tick_disk(int cycles);
    void rebuild_track_gcr();
    void step_head(int delta);

private:
    M6502 cpu_;
    Via6522 via1_;
    Via6522 via2_;

    std::array<uint8_t, 0x800> ram_{};
    std::array<uint8_t, 0x4000> rom_{};

    bool rom_loaded_ = false;

    D64Image disk_;
    G64Image g64_;

    bool use_g64_ = false;
    bool write_protect_ = false;
    bool weak_bits_ = true;

    uint32_t weak_rng_ = 0xACE1u;
    int zero_run_ = 0;

    // Mechanics.
    int half_track_ = 18 * 2;
    int stepper_prev_ = 0;

    // Cycles owed to the drive CPU: M6502::run() completes instructions and
    // can overshoot, and dropping that overshoot makes the drive run fast.
    int cycle_debt_ = 0;

    bool motor_on_ = false;
    bool led_on_ = false;

    // Rotating GCR bit stream for current track.
    std::vector<bool> track_bits_;
    int bit_pos_ = 0;
    int bit_timer_ = 0;
    int cycles_per_bit_ = 13;

    uint8_t shift_reg_ = 0;
    int shift_count_ = 0;

    // Consecutive one bits seen by the head; a sync mark is ten or more, which
    // spans several calls to tick_disk() and so cannot be a local count.
    int ones_ = 0;

    bool sync_ = false;
    bool byte_ready_ = false;
    uint8_t last_byte_ = 0;

    // VIA2 CA2: set overflow enable.
    bool soe_ = false;

    // IEC open-collector outputs from drive.
    // true = released/high.
    // false = pulled low.
    bool drv_data_ = true;
    bool drv_clk_ = true;

    bool host_atn_ = true;
    bool host_clk_ = true;
    bool host_data_ = true;

    bool via1_irq_ = false;
    bool via2_irq_ = false;
};

} // namespace dsp