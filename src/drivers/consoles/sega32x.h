#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "cpu/sh2.h"
#include "drivers/consoles/genesis.h"

namespace dsp {

// Sega 32X on a Genesis / Mega Drive: two SH-2s (master and slave, 23 MHz)
// with 256 KiB SDRAM, the 32X VDP (two 128 KiB frame buffers, packed pixel,
// direct colour and run length modes, 256-colour palette) mixed over the
// Genesis picture, PWM sound, the communication ports and the 68000-to-SH-2
// DREQ FIFO. The 68000 side is the Genesis driver with the 32X mapped in
// when ADEN is set: vector ROM at $000000, cartridge at $880000 / $900000
// (banked), frame buffer at $840000 and registers at $A15100.
//
// init() takes the BIOS set (MAME's 32x.zip: 32x_g_bios.bin, 32x_m_bios.bin,
// 32x_s_bios.bin); the cartridge (.32x, .bin) comes through load_media().
class Sega32X : public Genesis {
public:
    static constexpr uint32_t kSh2ClockNtsc = kMasterNtsc * 3 / 7;
    static constexpr uint32_t kSh2ClockPal = kMasterPal * 3 / 7;
    static constexpr int kSdramSize = 0x40000;
    static constexpr int kFbSize = 0x20000;

    explicit Sega32X(Region region = Region::Usa);

    bool init(const std::string& rom_path, std::string* error) override;
    bool load_media(const std::string& path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    const char* title() const override { return "Sega 32X"; }

    // Test / debug hooks.
    Sh2& master() { return master_; }
    Sh2& slave() { return slave_; }
    uint16_t comm(int index) const { return comm_[size_t(index) & 7]; }
    bool adapter_enabled() const { return aden_; }
    bool sh2_running() const { return sh2_running_; }
    uint16_t bitmap_mode() const { return bitmap_mode_; }
    const uint8_t* frame_buffer(int index) const { return dram_[size_t(index) & 1].data(); }
    uint16_t palette(int index) const { return palette_[size_t(index) & 255]; }
    uint16_t int_mask(int cpu) const { return int_mask_[size_t(cpu) & 1]; }
    uint16_t pending(int cpu) const { return pending_[size_t(cpu) & 1]; }

private:
    class Bus : public Sh2::Bus {
    public:
        Bus(Sega32X* owner, int cpu) : owner_(owner), cpu_(cpu) {}
        uint8_t read8(uint32_t a) override;
        uint16_t read16(uint32_t a) override;
        uint32_t read32(uint32_t a) override;
        void write8(uint32_t a, uint8_t v) override;
        void write16(uint32_t a, uint16_t v) override;
        void write32(uint32_t a, uint32_t v) override;
        bool dreq(int channel) override;

    private:
        Sega32X* owner_;
        int cpu_;
    };

    bool ext_read16(uint32_t address, uint16_t* value) override;
    bool ext_write16(uint32_t address, uint16_t value) override;
    bool ext_read8(uint32_t address, uint8_t* value) override;
    bool ext_write8(uint32_t address, uint8_t value) override;
    int32_t ext_audio_sample() override;

    // 32X address spaces shared by both sides.
    uint16_t sh2_read16(int cpu, uint32_t a);
    void sh2_write16(int cpu, uint32_t a, uint16_t v);
    uint8_t sh2_read8(int cpu, uint32_t a);
    void sh2_write8(int cpu, uint32_t a, uint8_t v);
    uint16_t sys_read(int cpu, uint32_t offset);            // SH-2 $4000-$403F
    void sys_write(int cpu, uint32_t offset, uint16_t v, uint16_t mask);
    uint16_t m68k_reg_read(uint32_t offset);                 // 68000 $A15100-$A1513F
    void m68k_reg_write(uint32_t offset, uint16_t v, uint16_t mask);
    uint16_t vdp_read(uint32_t offset);                      // $4100 / $A15180
    void vdp_write(uint32_t offset, uint16_t v, uint16_t mask);
    uint16_t pwm_read(uint32_t offset);
    void pwm_write(uint32_t offset, uint16_t v, uint16_t mask);
    uint16_t fb_read16(uint32_t offset) const;
    void fb_write16(uint32_t offset, uint16_t v, uint16_t mask, bool overwrite);

    void update_irls();
    void raise_irq(int bit);  // for both CPUs
    void sh2_reset_line(bool released);
    void fifo_push(uint16_t v);
    uint16_t fifo_pop();
    void pwm_advance(int sh2_cycles);
    void render_32x_line(int line, uint32_t* out, const uint8_t* backdrop);
    uint32_t sh2_clock() const { return pal_ ? kSh2ClockPal : kSh2ClockNtsc; }

    Bus master_bus_{this, 0};
    Bus slave_bus_{this, 1};
    Sh2 master_{&master_bus_};
    Sh2 slave_{&slave_bus_};

    std::vector<uint8_t> bios_g_;  // 68000 vector ROM, 256 bytes
    std::vector<uint8_t> bios_m_;  // master SH-2 boot ROM
    std::vector<uint8_t> bios_s_;  // slave SH-2 boot ROM
    std::vector<uint8_t> cart_;    // cartridge padded to a power of two (SH-2 page)
    bool bios_loaded_ = false;

    std::array<uint8_t, kSdramSize> sdram_{};
    std::array<std::array<uint8_t, kFbSize>, 2> dram_{};
    std::array<uint16_t, 256> palette_{};

    // Adapter / system registers.
    bool aden_ = false;
    bool sh2_running_ = false;  // RES released by the 68000
    bool fm_ = false;           // frame buffer / VDP owned by the SH-2s
    bool rv_ = false;           // ROM-to-VRAM DMA mode: cartridge back at $000000
    uint16_t int_ctl_ = 0;      // 68000 $A15102: INTS / INTM
    uint16_t bank_ = 0;
    std::array<uint16_t, 2> int_mask_{};  // per SH-2 $4000 low byte
    uint16_t hcount_ = 0;
    int hint_counter_ = 0;
    std::array<uint16_t, 2> pending_{};  // per SH-2: bits 0 PWM, 1 CMD, 2 H, 3 V, 4 VRES
    std::array<uint16_t, 8> comm_{};
    uint16_t sega_tv_ = 0;

    // 68000 -> SH-2 DREQ FIFO.
    uint16_t dreq_ctl_ = 0;  // bit 2 68S, bit 1 DMA
    uint32_t dreq_src_ = 0;
    uint32_t dreq_dst_ = 0;
    uint16_t dreq_len_ = 0;
    uint32_t dreq_left_ = 0;
    std::array<uint16_t, 8> fifo_{};
    int fifo_head_ = 0;
    int fifo_count_ = 0;

    // VDP.
    uint16_t bitmap_mode_ = 0;
    uint16_t shift_ = 0;
    uint16_t fill_len_ = 0;
    uint16_t fill_addr_ = 0;
    uint16_t fill_data_ = 0;
    int fs_ = 0;           // displayed buffer; the CPUs see the other one
    int fs_pending_ = -1;  // swap requested during display
    bool vblank_ = false;
    bool hblank_ = false;

    // PWM.
    uint16_t pwm_ctl_ = 0;
    uint16_t pwm_cycle_ = 0;
    std::array<std::array<uint16_t, 3>, 2> pwm_fifo_{};
    std::array<int, 2> pwm_count_{};
    std::array<int, 2> pwm_out_{};
    int pwm_acc_ = 0;
    int pwm_tm_ = 0;
    int64_t pwm_dc_ = 0;  // DC estimate (x1024) removed from the output

    int64_t sh2_debt_ = 0;
};

}  // namespace dsp
