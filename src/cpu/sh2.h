#pragma once

#include <array>
#include <functional>
#include <cstddef>
#include <cstdint>

namespace dsp {

// Hitachi SH-2 (SH7604) as used twice in the Sega 32X: the CPU core with
// delay slots, exceptions and IRL interrupts, plus the on-chip modules games
// touch — division unit, two-channel DMA controller (auto request and
// external DREQ), free-running timer, watchdog interval timer, the 4 KiB
// cache data array used as RAM, and the bus state controller registers.
//
// Addresses are 32-bit. Bits 31-29 select the area: 0 cached, 1 cache
// through, 2 associative purge, 3 address array, 6 data array (cache RAM),
// 7 on-chip registers ($FFFFFE00-$FFFFFFFF). External accesses (areas 0 and
// 1) reach the Bus with bits 28-0 of the address.
class Sh2 {
public:
    // External bus. Accesses are aligned to their size.
    class Bus {
    public:
        virtual ~Bus() = default;
        virtual uint8_t read8(uint32_t address) = 0;
        virtual uint16_t read16(uint32_t address) = 0;
        virtual uint32_t read32(uint32_t address) = 0;
        virtual void write8(uint32_t address, uint8_t value) = 0;
        virtual void write16(uint32_t address, uint16_t value) = 0;
        virtual void write32(uint32_t address, uint32_t value) = 0;
        // DMA channel `channel` has an external request pending (DREQ low).
        virtual bool dreq(int /*channel*/) { return false; }
    };

    explicit Sh2(Bus* bus) : bus_(bus) {}

    // Fast paths for plain memory: a 16 MiB external page (bits 28-24 of the
    // address) backed by `data` (big-endian bytes, size a power of two, the
    // page mirrors it). Reads and fetches from it skip the Bus.
    void map_memory(int page, uint8_t* data, uint32_t size, bool writable);

    void reset();  // power-on: PC = [0], SP = [4], VBR = 0, SR = $F0
    // Runs at least `cycles` cycles (one per instruction plus waits).
    int run(int cycles);

    // IRL interrupt request from outside: level 0 (none) to 15. The vector
    // is the auto-vector 64 + level / 2.
    void set_irl(int level) { irl_ = level; }
    // An external DMA request (DREQ) became active: run what it allows.
    void dma_request(int channel);

    bool sleeping() const { return sleeping_; }
    uint32_t pc() const { return pc_; }
    uint32_t r(int n) const { return r_[size_t(n) & 15]; }
    uint32_t pr() const { return pr_; }
    uint32_t gbr() const { return gbr_; }
    uint32_t vbr() const { return vbr_; }
    uint32_t sr() const { return sr_; }
    uint64_t total_cycles() const { return total_cycles_; }

    // Debug: called after any write that touches [address & 0x1fffffff].
    void set_write_watch(uint32_t address, std::function<void(uint32_t pc, uint32_t addr, uint32_t value)> fn) {
        watch_addr_ = address & 0x1ffffffc;
        watch_fn_ = std::move(fn);
    }
    // Debug: called on every exception / interrupt with its vector.
    void set_exception_hook(std::function<void(uint32_t vector, uint32_t pc)> fn) { exc_hook_ = std::move(fn); }
    // Debug hook called with the PC before each instruction.
    void set_instruction_hook(std::function<void(uint32_t)> hook) { hook_ = std::move(hook); }
    // Debug: on-chip register file and memory as the CPU sees it.
    uint32_t debug_read32(uint32_t address) { return read32(address); }

private:
    struct FastPage {
        uint8_t* data = nullptr;
        uint32_t mask = 0;
        bool writable = false;
    };

    uint8_t read8(uint32_t a);
    uint16_t read16(uint32_t a);
    uint32_t read32(uint32_t a);
    void write8(uint32_t a, uint8_t v);
    void write16(uint32_t a, uint16_t v);
    void write32(uint32_t a, uint32_t v);
    uint16_t fetch(uint32_t a);

    uint8_t onchip_read8(uint32_t a);
    uint16_t onchip_read16(uint32_t a);
    uint32_t onchip_read32(uint32_t a);
    void onchip_write8(uint32_t a, uint8_t v);
    void onchip_write16(uint32_t a, uint16_t v);
    void onchip_write32(uint32_t a, uint32_t v);

    void execute(uint16_t op);
    void delay_slot(uint32_t target);
    void exception(uint32_t vector);
    bool check_interrupts();
    void tick_peripherals(int cycles);
    void divu_32();
    void divu_64();
    void dma_check(int channel);
    void dma_run(int channel, bool external);
    int peripheral_level(int* vector);

    Bus* bus_;
    std::function<void(uint32_t)> hook_;
    std::function<void(uint32_t, uint32_t)> exc_hook_;
    uint32_t watch_addr_ = 0xffffffff;
    std::function<void(uint32_t, uint32_t, uint32_t)> watch_fn_;
    std::array<FastPage, 32> pages_{};

    std::array<uint32_t, 16> r_{};
    uint32_t pc_ = 0;
    uint32_t pr_ = 0;
    uint32_t sr_ = 0xf0;
    uint32_t gbr_ = 0;
    uint32_t vbr_ = 0;
    uint32_t mach_ = 0;
    uint32_t macl_ = 0;
    int cycles_ = 0;
    uint64_t total_cycles_ = 0;
    int irl_ = 0;
    bool sleeping_ = false;
    bool in_slot_ = false;

    // On-chip registers $FFFFFE00-$FFFFFFFF (raw storage).
    std::array<uint8_t, 512> regs_{};
    std::array<uint8_t, 4096> cache_ram_{};

    // Free-running timer.
    uint16_t frc_ = 0;
    uint16_t ocra_ = 0xffff;
    uint16_t ocrb_ = 0xffff;
    uint8_t ftcsr_ = 0;
    int frt_div_ = 0;
    // Watchdog.
    uint8_t wtcsr_ = 0x18;
    uint8_t wtcnt_ = 0;
    int wdt_div_ = 0;
    // Division unit.
    uint32_t dvsr_ = 0;
    uint32_t dvdnth_ = 0;
    uint32_t dvdntl_ = 0;
    uint32_t dvcr_ = 0;
    // DMA controller.
    struct DmaChannel {
        uint32_t sar = 0, dar = 0, tcr = 0, chcr = 0, vcr = 0;
    };
    std::array<DmaChannel, 2> dma_{};
    uint32_t dmaor_ = 0;
    bool dma_busy_ = false;
    // Highest on-chip interrupt request, recomputed when a module changes.
    bool periph_dirty_ = true;
    int periph_level_ = 0;
    int periph_vector_ = 0;
};

}  // namespace dsp
