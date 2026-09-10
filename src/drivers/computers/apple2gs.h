#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/w65c816.h"
#include "sound/es5503.h"

namespace dsp {

// Apple IIGS (ROM03), built around the WDC 65C816 and Apple's "Mega II"
// custom chip, which reproduces the original Apple II's memory map/banking
// and video generation in a single chip so 8-bit software keeps working
// unmodified alongside the 816's 16-bit native-mode software and its 24-bit,
// bank-switched address space.
//
// This is a fresh, from-scratch driver (no Apple IIGS support exists in
// leniad/dsp-emulator, the Pascal project this codebase otherwise migrates
// from); memory-map and timing facts below are cross-checked against MAME's
// src/mame/apple/apple2gs.cpp, then reimplemented independently.
class Apple2GS : public Machine {
public:
    static constexpr uint32_t kMasterClock = 28636363;
    static constexpr uint32_t kFastClock = kMasterClock / 10;   // ~2.864 MHz
    static constexpr uint32_t kSlowClock = 1023000;             // Apple II-compatible
    static constexpr int kScreenWidth = 640;
    static constexpr int kScreenHeight = 400;
    static constexpr double kFramesPerSecond = 59.923;

    Apple2GS() : cpu_(kFastClock), doc_(kMasterClock / 8) {}

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    bool load_media(const std::string& path, std::string* error) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return 44100; }

    const char* title() const override { return "Apple IIGS"; }

private:
    uint8_t mem_read(uint32_t address);
    void mem_write(uint32_t address, uint8_t value);
    uint8_t io_read(uint16_t offset);   // $Cxxx in banks 00/01/E0/E1
    void io_write(uint16_t offset, uint8_t value);
    void on_cpu_cycles(int cycles);
    void update_video();
    void update_video_shr();

    W65C816 cpu_;

    // Motherboard RAM: banks $00-$0F (1MB, the ROM03 standard configuration).
    static constexpr size_t kRamBanks = 16;
    std::array<std::array<uint8_t, 0x10000>, kRamBanks> ram_{};
    // Banks $E0/$E1: the Mega II's dedicated "shadow" banks, always present
    // and always reflecting the Apple II-compatible view of text/hires/etc.
    std::array<std::array<uint8_t, 0x10000>, 2> shadow_ram_{};
    // ROM banks $FC-$FF (256KB: 341-0728 covers FC-FD, 341-0748's two
    // halves -- swapped, per an inverted PCB address line -- cover FE-FF).
    std::array<uint8_t, 0x40000> rom_{};
    // Slot/Mega II gfx ROM (character generator + related fixed data).
    std::array<uint8_t, 0x4000> mega2_rom_{};
    std::array<uint8_t, 0x1000> chr_rom_{};

    // --- Mega II soft-switch state (the parts needed for early boot) ---
    bool altzp_ = false;       // language-card-style aux/main zero-page+stack select
    bool ramrd_ = false, ramwrt_ = false;  // main/aux RAM read/write select ($0000-$FFFF via banks 00/01)
    bool intcxrom_ = false;    // internal ROM vs slot ROM in $C100-$CFFF
    bool slotc3rom_ = false;   // slot vs internal ROM specifically for slot 3 ($C300-$C3FF)
    bool lcram_ = false;       // language-card RAM readable (else ROM readable) at $D000-$FFFF
    bool lcram2_ = true;       // language-card bank 2 (else bank 1) selected for $D000-$DFFF
    bool lc_write_enable_ = false;
    bool lc_prewrite_ = false;
    bool vbl_ = false;
    // Real Apple IIGS clock/CMOS-RAM chip protocol: an 8-bit command
    // (bit7 = 1 for read / 0 for write, bits6-0 = register address 0-127)
    // is clocked MSB-first through $C034's bit7 on each write, one bit per
    // write; once 8 bits are assembled the command is decoded, then a
    // further 8 $C034 writes (for a write command) or reads via $C033
    // (for a read command) transfer the data byte the same way. Backs a
    // 256-byte battery-RAM array (addresses 0-127 map to the low half;
    // some Apple II clock chips mirror the same store into 128-255, which
    // software sometimes relies on, so both halves are kept in sync here)
    // seeded with values matching this machine's actual configuration.
    uint8_t clock_ram_[256] = {};
    Es5503 doc_;
    uint16_t doc_addr_ = 0;
    uint8_t doc_ctrl_ = 0;  // bit6: 0=register access, 1=RAM access; bit5: auto-increment
    uint64_t cycles_since_sample_ = 0;
    int clock_bit_count_ = 0;
    uint8_t clock_shift_reg_ = 0;
    bool clock_cmd_ready_ = false;
    bool clock_is_read_ = false;
    uint8_t clock_addr_ = 0;
    uint8_t rtc_data_ = 0, rtc_control_ = 0;
    uint8_t adb_response_ = 0;
    bool adb_response_ready_ = false;
    uint8_t vgcint_ = 0;
    uint8_t inten_ = 0;  // bit3: VBL IRQ enable (standard IIGS convention)
    uint8_t slotromsel_ = 0;  // bit N (1-7): slot N uses internal ROM if set
    uint8_t shadow_reg_ = 0x08;  // which regions mirror to $E0/$E1 (IIgs-specific)
    uint8_t speed_reg_ = 0x40;   // bit7: fast/slow; bit6 always 1
    uint8_t state_reg_ = 0;      // bank 0/1 select bits etc.
    uint8_t newvideo_ = 0x01;    // Super Hi-Res enable + linear/paletted mode
    uint8_t border_color_ = 0;
    uint8_t text_color_ = 0xf0;
    bool col80_ = false, store80_ = false, page2_ = false, hires_ = false, text_mode_ = true, mixed_ = false;
    bool altcharset_ = false;
    bool an3_ = true;
    int scanline_ = 0;
    uint64_t total_cycles_ = 0;
    // Background ADB-microcontroller-completion simulation. Real hardware
    // runs the ADB microcontroller as a genuinely separate CPU: it updates
    // shared status bytes in the $E1xxxx "system global page" on its own
    // schedule, independent of whether the 65816 currently has interrupts
    // masked (confirmed via a real trace: the boot-time poll loop that
    // waits on one of these bytes runs with SEI active, so a maskable-IRQ
    // based approach can't drive it). Rather than a full second CPU core,
    // this tracks "someone just wrote 0 to a monitored completion-status
    // byte" and, after a short simulated delay measured in elapsed CPU
    // cycles (tracked independent of interrupt state), writes back a
    // "done" marker -- reproducing the externally-observable effect of the
    // real microcontroller's asynchronous completion without emulating its
    // internal instruction stream.
    struct PendingCompletion { uint32_t address; uint64_t due_cycle; };
    std::vector<PendingCompletion> pending_completions_;

    // --- Disk / SmartPort (slot 5, 3.5" block device) ---
    // High-level emulation: a tiny, protocol-correct firmware stub is
    // placed at the slot 5 ROM window with the standard SmartPort
    // identification bytes real disk-controller firmware carries (so the
    // ROM's own device scan recognizes a SmartPort-capable controller is
    // present), and the stub's dispatcher body is just a JSR to a fixed
    // "trap" address that on_cpu_cycles intercepts directly, reading the
    // ProDOS device driver call's parameter block from memory, performing
    // the block read/write against disk_ in C++, and returning
    // success/failure the same way the real firmware would (carry flag +
    // error code in A) -- avoiding the need to emulate real GCR/IWM disk
    // timing for a raw ProDOS-order (.po) image, which is already exactly
    // the sequence of 512-byte blocks the driver call expects.
    static constexpr uint32_t kSmartPortTrapPc = 0x00c50a;
    std::vector<uint8_t> disk_;
    bool disk_loaded_ = false;
    void smartport_dispatch();

    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};
    std::vector<int16_t> audio_;

    uint8_t keyboard_latch_ = 0;
    uint8_t button_state_ = 0;

    int main_cycles_per_frame_ = 0;
};

}  // namespace dsp
