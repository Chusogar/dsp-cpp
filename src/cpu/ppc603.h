#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace dsp {

// PowerPC 603e / 603r (MPC603) interpreter: the full user and supervisor
// integer instruction set, the FPU (double and single precision, fused
// multiply-add, FPSCR, CR1 updates), condition register and branch unit,
// the SPRs (LR, CTR, XER, SRR0/1, SPRG0-3, DEC, TBL/TBU, HID0-2, PVR,
// BATs, DAR, DSISR, SDR1, segment registers) and the exceptions a game
// board needs: system reset vector, external interrupt, decrementer,
// system call, program (trap / illegal) and FP unavailable. Big-endian
// only. Address translation is not performed (effective = physical), as
// the Model 3 games map everything 1:1 through the BATs.
class Ppc603 {
public:
    class Bus {
    public:
        virtual ~Bus() = default;
        virtual uint8_t read8(uint32_t address) = 0;
        virtual uint16_t read16(uint32_t address) = 0;
        virtual uint32_t read32(uint32_t address) = 0;
        virtual uint64_t read64(uint32_t address) {
            return (uint64_t(read32(address)) << 32) | read32(address + 4);
        }
        virtual void write8(uint32_t address, uint8_t value) = 0;
        virtual void write16(uint32_t address, uint16_t value) = 0;
        virtual void write32(uint32_t address, uint32_t value) = 0;
        virtual void write64(uint32_t address, uint64_t value) {
            write32(address, uint32_t(value >> 32));
            write32(address + 4, uint32_t(value));
        }
    };

    static constexpr uint32_t kPvr603r = 0x00071202;
    static constexpr uint32_t kPvr603e = 0x00060103;

    explicit Ppc603(Bus& bus, uint32_t pvr = kPvr603r) : bus_(bus), pvr_(pvr) {}

    // Memory the CPU may touch directly (big-endian byte order, as on the
    // bus). `writable` regions take stores; others are read/fetch only.
    void add_fast_region(uint32_t start, uint32_t size, uint8_t* data, bool writable);
    void clear_fast_regions() { num_regions_ = 0; last_region_ = 0; regions_.fill(Region{}); }
    // Points fast region `index` (in add order) at new data (bank switching).
    void set_region_data(int index, uint8_t* data) {
        if (index >= 0 && index < num_regions_) regions_[size_t(index)].data = data;
    }

    void reset();
    // Runs at least `cycles` cycles (one per instruction), returns the count.
    int run(int cycles);
    // Stops run() after the current instruction.
    void abort_run() { stop_ = true; }

    void set_irq(bool asserted) { irq_line_ = asserted; }
    // Timebase/decrementer rate: CPU cycles per timer tick (bus/4).
    void set_timer_divider(int cycles) { timer_div_ = cycles > 0 ? cycles : 1; }

    uint32_t pc() const { return pc_; }
    void set_pc(uint32_t v) { pc_ = v; }
    uint32_t gpr(int n) const { return r_[size_t(n)]; }
    void set_gpr(int n, uint32_t v) { r_[size_t(n)] = v; }
    uint32_t msr() const { return msr_; }
    uint32_t lr() const { return lr_; }
    uint32_t ctr() const { return ctr_; }
    uint32_t cr() const { return cr_; }
    double fpr(int n) const { double d; std::memcpy(&d, &f_[size_t(n)], 8); return d; }
    uint64_t cycles() const { return total_cycles_; }

    // Optional hook for skipping idle loops: when the PC lands on
    // `address` the remaining cycles of run() are consumed.
    void set_idle_pc(uint32_t address) { idle_pc_ = address; }
    // Automatic detection of load/compare/branch spin loops (on by default).
    void set_spin_detection(bool on) { spin_detect_ = on; spin_target_ = 0xffffffff; }

private:
    struct Region {
        uint32_t start, end;
        uint8_t* data;
        bool writable;
    };

    // Memory access
    const Region* find(uint32_t a) const {
        const Region* last = &regions_[size_t(last_region_)];
        if (a >= last->start && a <= last->end && last->data) return last;
        for (int i = 0; i < num_regions_; ++i)
            if (a >= regions_[i].start && a <= regions_[i].end) {
                last_region_ = i;
                return &regions_[i];
            }
        return nullptr;
    }
    uint8_t rd8(uint32_t a);
    uint16_t rd16(uint32_t a);
    uint32_t rd32(uint32_t a);
    uint64_t rd64(uint32_t a);
    void wr8(uint32_t a, uint8_t v);
    void wr16(uint32_t a, uint16_t v);
    void wr32(uint32_t a, uint32_t v);
    void wr64(uint32_t a, uint64_t v);
    uint32_t fetch(uint32_t a);

    // Helpers
    void set_cr_field(int field, uint32_t v) {
        const int sh = (7 - field) * 4;
        cr_ = (cr_ & ~(0xfu << sh)) | ((v & 0xf) << sh);
    }
    void update_cr0(uint32_t result) {
        uint32_t v = (int32_t(result) < 0) ? 8 : (result ? 4 : 2);
        if (xer_so_) v |= 1;
        set_cr_field(0, v);
    }
    void update_cr1() { set_cr_field(1, fpscr_ >> 28); }
    uint32_t xer() const;
    void set_xer(uint32_t v);
    uint32_t read_spr(int spr);
    void write_spr(int spr, uint32_t v);
    uint32_t read_dec();
    void write_dec(uint32_t v);
    uint64_t read_tb();
    void write_tb(uint64_t v);
    void set_msr(uint32_t v) { msr_ = v; }
    void exception(uint32_t vector, uint32_t srr0, uint32_t srr1_bits = 0);
    void check_interrupts();
    bool fp_available();

    double fd(int n) const { double d; std::memcpy(&d, &f_[size_t(n)], 8); return d; }
    void set_fd(int n, double d) { std::memcpy(&f_[size_t(n)], &d, 8); }
    void set_fs(int n, double d);  // rounds to single precision
    void set_fprf(double d);

    void exec(uint32_t op);
    void exec19(uint32_t op);
    void exec31(uint32_t op);
    void exec59(uint32_t op);
    void exec63(uint32_t op);
    void illegal(uint32_t op);
    bool is_spin_loop(uint32_t start, uint32_t branch);

    Bus& bus_;
    uint32_t pvr_;
    std::array<Region, 8> regions_{};
    int num_regions_ = 0;
    mutable int last_region_ = 0;

    std::array<uint32_t, 32> r_{};
    std::array<uint64_t, 32> f_{};
    uint32_t pc_ = 0, cur_pc_ = 0;
    uint32_t cr_ = 0, lr_ = 0, ctr_ = 0, msr_ = 0;
    bool xer_so_ = false, xer_ov_ = false, xer_ca_ = false;
    uint32_t xer_bc_ = 0;
    uint32_t fpscr_ = 0;
    uint32_t srr0_ = 0, srr1_ = 0, dar_ = 0, dsisr_ = 0, sdr1_ = 0;
    std::array<uint32_t, 4> sprg_{};
    std::array<uint32_t, 16> sr_{};
    std::array<uint32_t, 16> bat_{};  // IBAT0U/L..IBAT3U/L, DBAT0U/L..DBAT3U/L
    uint32_t hid0_ = 0, hid1_ = 0, hid2_ = 0, ear_ = 0;
    std::array<uint32_t, 6> spr603_{};  // DMISS, DCMP, HASH1, HASH2, IMISS, ICMP, RPA (unused)

    // Timers: kept relative to total_cycles_.
    uint64_t total_cycles_ = 0;
    int timer_div_ = 10;
    uint64_t tb_base_ = 0, tb_cycle_ = 0;
    uint32_t dec_base_ = 0;
    uint64_t dec_cycle_ = 0;
    bool dec_pending_ = false;
    uint64_t dec_fire_cycle_ = ~0ull;

    bool irq_line_ = false;
    bool stop_ = false;
    bool reserve_ = false;
    uint32_t reserve_addr_ = 0;
    uint32_t idle_pc_ = 0xffffffff;
    int budget_ = 0;
    bool spin_detect_ = true;
    bool spin_ok_ = false;
    uint32_t spin_target_ = 0xffffffff, spin_branch_ = 0xffffffff;
};

}  // namespace dsp
