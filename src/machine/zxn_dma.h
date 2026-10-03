#pragma once

#include <cstdint>
#include <functional>

namespace dsp {

// ZX Spectrum Next DMA (zxnDMA): a Z80-DMA register set (WR0-WR6, read
// mask/status sequence) that does memory/IO block transfers. Port $6B
// selects zxnDMA mode (transfers exactly `length` bytes), port $0B the
// Zilog compatible mode (length + 1). Transfers run in bursts that steal
// the bus from the CPU; with a port B prescaler (WR2 timing byte bit 5)
// a byte is moved every `prescaler` ticks of the 875 kHz reference, which
// is how the Next plays sampled sound through the DACs.
class ZxnDma {
public:
    using MemRead = std::function<uint8_t(uint16_t)>;
    using MemWrite = std::function<void(uint16_t, uint8_t)>;

    MemRead mem_read, io_read;
    MemWrite mem_write, io_write;
    // 28 MHz ticks per CPU T-state at the current CPU speed (8 at 3.5 MHz).
    int ticks_per_t = 8;

    void reset();
    void write(uint8_t value, bool z80_mode);
    uint8_t read();

    bool active() const { return enabled_; }
    // Runs the transfer for up to `budget` 28 MHz ticks (the time the CPU
    // would have run). Returns the ticks used by the DMA (the CPU is
    // stalled meanwhile); 0 when idle or waiting for the prescaler.
    int run(int budget);
    // Elapsed 28 MHz ticks, for the prescaler pacing.
    void advance(int ticks) { if (prescaler_wait_ > 0) prescaler_wait_ -= ticks; }

private:
    void command(uint8_t value);
    void load();
    void transfer_byte();
    void finish();

    // Programmed registers
    uint8_t wr0_ = 0, wr1_ = 0, wr2_ = 0, wr3_ = 0, wr4_ = 0, wr5_ = 0;
    uint16_t port_a_ = 0, port_b_ = 0, length_ = 0;
    uint8_t timing_a_ = 0, timing_b_ = 0, prescaler_ = 0;
    uint8_t read_mask_ = 0x7f;

    // Write sequencer: list of pending parameter bytes for the current WR.
    uint8_t follow_[8] = {};
    int follow_count_ = 0, follow_pos_ = 0;

    // Transfer state
    bool enabled_ = false;
    bool z80_mode_ = false;
    uint16_t addr_a_ = 0, addr_b_ = 0;
    uint16_t counter_ = 0;
    bool end_of_block_ = false;
    int prescaler_wait_ = 0;

    // Read sequence
    uint8_t read_seq_[7] = {};
    int read_count_ = 0, read_pos_ = 0;
    uint8_t status_ = 0x3a;
};

}  // namespace dsp
