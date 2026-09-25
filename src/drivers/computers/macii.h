#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "core/machine.h"
#include "machine/iwm.h"
#include "machine/ncr5380_hdd.h"
#include "machine/via6522.h"
#include "sound/asc.h"

namespace dsp {

// Macintosh II (ROM rev. A 97851DB6 or rev. B 9779D2C4, no 68851: the Apple
// HMMU does the 24-bit translation), 8 MB RAM, with an Apple Macintosh
// Display Card 8*24 (JMFB, declaration ROM 341-0868) in NuBus slot 9 driving
// a 640x480 "Hi-Res" monitor.
//
// CPU: Musashi 68020 (no FPU, like a Mac II with the 68881 socket empty).
// Hardware follows MAME's macii / nubus_48gc drivers: two VIAs (VIA2 PB3
// selects 24/32-bit mode, VIA2 T1 on PB7 is the 60.15 Hz tick into VIA1 CA1),
// NCR 5380 SCSI (pseudo-DMA windows), Apple Sound Chip, IWM, Z8530 SCC,
// 343-0042-B RTC with 256 bytes of extended PRAM, and the ADB transceiver on
// the VIA1 shift register, modelled at the protocol level (as Mini vMac does).
//
// Disks: a SCSI hard disk image (ID 0/6). A bare HFS volume (starts with
// the "LK" boot blocks) gets a driver descriptor map and a small SCSI driver
// in front of it so the ROM can boot it; images with a partition map boot
// with their own driver. Writes go back to the image file.
class MacII : public Machine {
public:
    static constexpr uint32_t kCpuClock = 15667200;
    static constexpr uint32_t kRamSize = 8u << 20;
    static constexpr int kWidth = 640, kHeight = 480;
    static constexpr int kLines = 525;
    static constexpr int kCyclesPerFrame = 235008;  // 66.67 Hz
    static constexpr int kCyclesPerLine = kCyclesPerFrame / kLines;

    MacII();
    ~MacII() override;

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    bool load_media(const std::string& path, std::string* error) override;
    void run_frame() override;
    void set_inputs(const MachineInputs&) override;
    void set_dip_switch(int, uint8_t) override {}
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return double(kCpuClock) / kCyclesPerFrame; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return Asc::kSampleRate; }
    const char* title() const override { return "Macintosh II"; }
    bool uses_keyboard() const override { return true; }
    bool uses_pointer() const override { return true; }

    // Debug helpers.
    uint32_t debug_pc() const;
    uint8_t peek(uint32_t logical);
    uint32_t peek32(uint32_t logical);
    void set_trace(int instructions) { trace_left_ = instructions; }
    void post_key(int adb_code, bool down) { key_events_.push_back(uint8_t(adb_code | (down ? 0 : 0x80))); }
    void debug_mouse(int dx, int dy, bool button);
    uint64_t total_cycles() const { return total_cycles_; }
    const uint8_t* debug_pram() const { return pram_.data(); }

    // Bus interface used by the CPU glue (public for the C callbacks).
    uint32_t cpu_read(uint32_t address, int size);
    void cpu_write(uint32_t address, uint32_t value, int size);
    uint32_t cpu_peek(uint32_t address, int size);
    void instruction_hook(uint32_t pc);

private:
    uint32_t translate(uint32_t a) const;
    uint8_t* ram_ptr(uint32_t pa);
    uint32_t phys_read(uint32_t pa, int size, bool side_effects);
    void phys_write(uint32_t pa, uint32_t value, int size);
    uint8_t io_read8(uint32_t off);
    void io_write8(uint32_t off, uint8_t v);
    uint32_t card_read(uint32_t off, int size);
    void card_write(uint32_t off, uint32_t value, int size);
    uint32_t card_reg_read(uint32_t reg);
    void card_reg_write(uint32_t reg, uint32_t data);
    void bus_error(uint32_t address, bool write);

    void update_irqs();
    void set_slot_irq(bool on);
    void via1_pa_w(uint8_t v);
    void via1_pb_w(uint8_t v);
    uint8_t via1_pb_r();
    void via2_pa_w(uint8_t v);
    void via2_pb_w(uint8_t v);
    void tick_devices(int cycles);
    void render();
    void make_context_current();
    void seed_pram(uint8_t video_mode);

    // ---- ADB transceiver (protocol level) ----
    void adb_state_changed();
    void adb_new_state();
    void adb_update();
    void adb_talk();
    void adb_end_listen();
    bool adb_any_event() const;
    bool adb_next_key(uint8_t* ev);
    void adb_shift_in(uint8_t v);  // byte to the Mac
    uint8_t adb_shift_out();       // byte from the Mac
    int adb_state_ = 3;
    int64_t adb_due_ = -1;
    bool adb_int_ = true;  // VIA1 PB3, active low
    bool adb_listen_ = false, adb_talk_buf_ = false;
    uint8_t adb_index_ = 0, adb_size_ = 0, adb_cmd_ = 0;
    uint8_t adb_buf_[8] = {};
    uint8_t adb_kbd_addr_ = 2, adb_mouse_addr_ = 3, adb_rand_ = 1;
    std::deque<uint8_t> key_events_;
    int mouse_dx_ = 0, mouse_dy_ = 0;
    bool mouse_button_ = false, mouse_button_sent_ = false;
    bool adb_mouse_polled_ = false;
    void move_pointer(int x, int y);

    // ---- RTC 343-0042-B ----
    void rtc_ce(bool level);
    void rtc_clk(bool level);
    void rtc_execute(uint8_t byte);
    bool rtc_enb_ = true, rtc_clk_ = false, rtc_latch_ = false, rtc_out_ = false;
    int rtc_state_ = 0, rtc_bits_ = 0;
    bool rtc_dir_out_ = false;
    uint8_t rtc_byte_ = 0, rtc_cmd_ = 0, rtc_xaddr_ = 0;
    bool rtc_wp_ = false;
    uint32_t rtc_seconds_ = 0;
    std::array<uint8_t, 256> pram_{};
    int64_t second_cycles_ = 0;
    bool rtc_1hz_ = false;

    // ---- MDC 8*24 video card (JMFB) ----
    std::vector<uint8_t> vram_, decl_rom_;
    uint16_t jm_control_ = 2, jm_preload_ = 248;
    uint32_t jm_base_ = 0, jm_stride_ = 20;
    uint16_t crtc_[64] = {};
    bool vbl_disable_ = true;
    bool slot_irq_ = false;
    uint8_t clut_addr_ = 0, clut_cnt_ = 0, clut_rgb_[3] = {};
    uint8_t ramdac_mode_ = 0;
    std::array<uint32_t, 256> clut_{};
    int line_ = 0;

    // ---- chipset ----
    Via6522 via1_, via2_;
    Iwm iwm_;
    Ncr5380Hdd scsi_;
    Asc asc_;
    uint8_t scc_ptr_[2] = {0, 0}, scc_wr2_ = 0;
    std::vector<uint8_t> ram_, rom_;
    bool overlay_ = true;
    uint8_t glue_ = 0;
    bool hmmu_24bit_ = false;
    uint8_t nubus_irq_ = 0x3f;
    bool via1_irq_ = false, via2_irq_ = false;
    int via_rem_ = 0;
    int64_t asc_acc_ = 0;
    std::vector<int16_t> audio_;
    std::array<uint32_t, kWidth * kHeight> framebuffer_{};

    // ---- CPU ----
    std::vector<uint8_t> cpu_context_;
    bool in_execute_ = false;
    uint64_t total_cycles_ = 0;
    uint64_t last_tick_cycle_ = 0;
    int trace_left_ = 0;
    bool initialized_ = false;
    std::array<bool, size_t(Key::Count)> prev_keys_{};
    int last_px_ = -1, last_py_ = -1;
};

}  // namespace dsp
