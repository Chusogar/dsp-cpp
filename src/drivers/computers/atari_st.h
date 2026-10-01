#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "machine/mc68901.h"
#include "machine/st_floppy.h"
#include "sound/ay8910.h"

namespace dsp {

// Atari 1040ST (PAL): 68000, TOS ROM, shifter, MFP, YM2149, WD1772 floppy.
class AtariSt : public Machine {
public:
    static constexpr uint32_t kCpuClock = 8010265;
    static constexpr int kSampleRate = AY8910::kSampleRate;
    static constexpr double kFps = 50.053;
    static constexpr int kLines = 313;
    static constexpr int kCyclesPerLine = 512;
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 400;
    static constexpr uint32_t kRamSize = 0x100000;
    static constexpr uint32_t kRomSize = 0x30000;

    AtariSt();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    bool load_media(const std::string& path, std::string* error) override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override {
        return vkb_visible_ ? display_.data() : framebuffer_.data();
    }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "Atari ST"; }
    bool uses_keyboard() const override { return true; }
    bool uses_pointer() const override { return true; }
    bool uses_relative_pointer() const override { return true; }

    uint32_t debug_pc() const { return cpu_.pc(); }
    // On-screen keyboard (Hataroid's, toggled with F11).
    static constexpr int kVkbTop = 187;          // first framebuffer row
    static constexpr int kVkbHeight = kHeight - kVkbTop;
    bool vkb_visible() const { return vkb_visible_; }
    void set_vkb_visible(bool visible);
    // Framebuffer position of the centre of the key with this ST scancode.
    bool vkb_key_centre(uint8_t scancode, int* x, int* y) const;
    M68000& debug_cpu() { return cpu_; }
    uint32_t debug_a(int r) const { return cpu_.a[size_t(r)].l; }
    uint8_t peek(uint32_t address) const { return const_cast<AtariSt*>(this)->read_byte(address); }
    void poke(uint32_t address, uint8_t value) { write_byte(address, value); }
    void poke_word(uint32_t address, uint16_t value) { write_word(address, value); }
    std::vector<uint8_t> ikbd_pending_bytes() const;
    bool floppy_loaded(int drive = 0) const { return floppy_.loaded(drive); }
    int floppy_spt(int drive = 0) const { return floppy_.spt(drive); }
    int floppy_tracks(int drive = 0) const { return floppy_.tracks(drive); }

private:
    uint8_t read_byte(uint32_t address);
    void write_byte(uint32_t address, uint8_t value);
    uint16_t read_word(uint32_t address);
    void write_word(uint32_t address, uint16_t value);
    void on_cpu_cycles(int cycles);
    void update_irqs();
    void render();
    void acia_write_control(uint8_t value);
    void acia_write_data(uint8_t value);
    uint8_t acia_status() const;
    uint8_t acia_read_data();
    void ikbd_push(uint8_t value);
    void ikbd_byte(uint8_t value);
    void ikbd_keys(const MachineInputs& inputs);
    void ikbd_mouse(const MachineInputs& inputs);
    void ikbd_mouse_packet(int dx, int dy, bool left, bool right);
    // Mouse motion in IKBD units, sent according to the current mouse mode.
    void ikbd_mouse_report(int dx, int dy, bool left, bool right);
    void ikbd_command(const std::vector<uint8_t>& cmd);
    void ikbd_reset_modes();
    void ikbd_joysticks(const MachineInputs& inputs);
    void ikbd_clock_tick();
    void service_acia();
    uint16_t blit_get_word(uint32_t even_addr) const;
    void blit_set_word(uint32_t even_addr, uint16_t value);
    uint16_t blit_mem_read(uint32_t address) const;
    void blit_mem_write(uint32_t address, uint16_t value);
    void run_blitter();

    void vkb_init();
    int vkb_hit(int x, int y) const;  // key table index or -1
    void vkb_input(const MachineInputs& inputs);
    void vkb_release_all();
    void vkb_compose();

    M68000 cpu_;
    AY8910 psg_;
    Mc68901 mfp_;
    StFloppy floppy_;

    std::vector<uint8_t> ram_;
    std::vector<uint8_t> rom_;
    std::array<uint32_t, kWidth * kHeight> framebuffer_{};
    std::array<uint32_t, kWidth * kHeight> display_{};

    // On-screen keyboard state.
    std::vector<uint32_t> vkb_image_;  // 640 x kVkbHeight, scaled once
    bool vkb_visible_ = false;
    bool vkb_toggle_down_ = false;
    bool vkb_button_down_ = false;
    int vkb_pressed_ = -1;  // key table index held by the mouse
    std::array<bool, 128> vkb_latched_{};  // sticky Shift / Ctrl / Alt
    bool vkb_pointer_on_ = false;
    int vkb_pointer_x_ = 0, vkb_pointer_y_ = 0;
    std::array<uint16_t, 16> palette_{};
    std::array<bool, size_t(Key::Count)> keys_down_{};

    bool rom_at_zero_ = true;
    uint8_t memcfg_ = 0;
    uint8_t video_hi_ = 0;
    uint8_t video_mid_ = 0;
    uint8_t video_lo_ = 0;
    uint8_t sync_mode_ = 0x02;
    uint8_t resolution_ = 0;
    uint8_t psg_port_a_ = 0xff;

    uint8_t acia_control_ = 0;
    uint8_t acia_rdr_ = 0;  // 6850 receive data register (last byte)
    std::deque<uint8_t> ikbd_rx_;
    struct IkbdByte {
        uint8_t value = 0;
        int cycles = 0;
    };
    std::deque<IkbdByte> ikbd_pending_;
    // IKBD (HD6301) command interpreter state.
    std::vector<uint8_t> ikbd_cmd_;  // command byte + parameters received
    int ikbd_cmd_need_ = 0;          // total bytes the command takes
    enum class MouseMode : uint8_t { Relative, Absolute, Keycode, Off };
    enum class JoyMode : uint8_t { Event, Interrogate, Monitor, Off };
    MouseMode mouse_mode_ = MouseMode::Relative;
    JoyMode joy_mode_ = JoyMode::Event;
    bool ikbd_paused_ = false;
    bool mouse_y_bottom_ = false;    // Y origin at the bottom (0x10)
    uint8_t mouse_button_action_ = 0;
    int abs_x_ = 0, abs_y_ = 0, abs_max_x_ = 319, abs_max_y_ = 199;
    uint8_t abs_buttons_ = 0;        // 0x0D button change bits
    bool abs_left_ = false, abs_right_ = false;
    uint8_t joy_state_[2] = {0, 0};  // bit 7 fire, bits 0-3 up/down/left/right
    int joy_monitor_rate_ = 0;       // 0x17 rate in 1/100 s
    int joy_monitor_count_ = 0;
    uint8_t clock_[6] = {0x89, 0x01, 0x01, 0x00, 0x00, 0x00};  // BCD yy mm dd hh mm ss
    int clock_frames_ = 0;
    int last_pointer_x_ = 0;
    int last_pointer_y_ = 0;
    int pointer_frac_x_ = 0;
    int pointer_frac_y_ = 0;
    bool pointer_seen_ = false;
    bool seed_valid_ = false;
    int seed_x_ = 0, seed_y_ = 0;
    bool last_pointer_b1_ = false;
    bool last_pointer_b2_ = false;
    uint32_t video_count_ = 0;

    // Mega ST / STE blitter at $FF8A00. TOS 1.04 Line-A uses it once the
    // probe succeeds (no bus error). Open-bus $FF left BUSY stuck on.
    std::array<uint16_t, 16> blit_halftone_{};
    int16_t blit_sxinc_ = 0;
    int16_t blit_syinc_ = 0;
    int16_t blit_dxinc_ = 0;
    int16_t blit_dyinc_ = 0;
    uint32_t blit_src_ = 0;
    uint32_t blit_dst_ = 0;
    uint16_t blit_emask_[3] = {0, 0, 0};
    uint16_t blit_xcount_ = 0;
    uint16_t blit_ycount_ = 0;
    uint8_t blit_hop_ = 0;
    uint8_t blit_op_ = 0;
    uint8_t blit_ctrl_ = 0;
    uint8_t blit_skew_ = 0;
    bool blit_defer_start_ = false;

    int64_t mfp_acc_ = 0;
    int64_t audio_acc_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
