#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "machine/iwm.h"
#include "machine/mac_rtc.h"
#include "machine/via6522.h"

namespace dsp {

// Macintosh Plus: 68000, 128K ROM, 1MB RAM, 512×342 1bpp, IWM 800K floppy.
class MacPlus : public Machine {
public:
    static constexpr uint32_t kCpuClock = 7833600;
    static constexpr int kHTotal = 704;
    static constexpr int kVTotal = 370;
    static constexpr int kHVisible = 512;
    static constexpr int kVVisible = 342;
    static constexpr int kVBlankLines = 28;
    static constexpr int kCyclesPerLine = kHTotal / 2;  // 352
    static constexpr int kWidth = kHVisible;
    static constexpr int kHeight = kVVisible;
    static constexpr uint32_t kRamSize = 0x100000;
    static constexpr uint32_t kRomSize = 0x20000;
    static constexpr int kSampleRate = 22254;
    static constexpr double kFps =
        double(kCpuClock) / double(kVTotal * kCyclesPerLine);

    MacPlus();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    bool load_media(const std::string& path, std::string* error) override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "Macintosh Plus"; }
    bool uses_keyboard() const override { return true; }
    bool uses_pointer() const override { return true; }

    uint32_t debug_pc() const { return cpu_.pc(); }
    uint32_t debug_sp() const { return cpu_.a[7].l; }
    uint8_t debug_im() const { return cpu_.cc.im; }
    uint8_t peek(uint32_t address) { return read_byte(address); }
    uint8_t peek_ram(uint32_t address) const { return ram_[address & (kRamSize - 1)]; }
    void poke(uint32_t address, uint8_t value) { write_byte(address, value); }
    bool overlay() const { return overlay_; }
    bool floppy_loaded() const { return iwm_.loaded(); }
    int floppy_track() const { return iwm_.track(); }
    uint8_t last_kbd_cmd() const { return kbd_cmd_; }
    uint8_t last_kbd_reply() const { return kbd_reply_; }
    Iwm& iwm() { return iwm_; }
    Via6522& via() { return via_; }

private:
    uint8_t read_byte(uint32_t address);
    void write_byte(uint32_t address, uint8_t value);
    uint16_t read_word(uint32_t address);
    void write_word(uint32_t address, uint16_t value);
    void on_cpu_cycles(int cycles);
    void update_irqs();
    void render();
    void via_pa_w(uint8_t data);
    void via_pb_w(uint8_t data);
    uint8_t via_pa_r();
    uint8_t via_pb_r();
    uint8_t scc_read(uint32_t address);
    void scc_write(uint32_t address, uint8_t value);
    uint8_t ram_at(uint32_t address) const { return ram_[address & (kRamSize - 1)]; }
    void ram_at(uint32_t address, uint8_t value) { ram_[address & (kRamSize - 1)] = value; }
    void clock_keyboard();
    static uint8_t keyboard_reply(uint8_t command);

    M68000 cpu_;
    Via6522 via_;
    Iwm iwm_;
    MacRtc rtc_;

    std::vector<uint8_t> ram_;
    std::vector<uint8_t> rom_;
    std::array<uint32_t, kWidth * kHeight> framebuffer_{};

    bool overlay_ = true;
    int screen_buffer_ = 1;
    bool main_sound_ = true;
    bool snd_enable_ = false;
    int snd_vol_ = 3;
    bool via_irq_ = false;
    bool scc_irq_ = false;
    bool mouse_button_ = false;
    uint8_t mouse_bit_[2] = {0, 0};
    uint8_t mouse_last_[2] = {0, 0};
    int last_pointer_x_ = 0;
    int last_pointer_y_ = 0;
    bool pointer_seen_ = false;
    bool rtc_ca2_ = false;

    uint8_t scc_ptr_[2] = {0, 0};
    uint8_t scc_wr1_[2] = {0, 0};
    bool scc_dcd_[2] = {false, false};

    int64_t via_acc_ = 0;
    int64_t rtc_acc_ = 0;
    int kbd_acc_ = 0;
    uint8_t kbd_cmd_ = 0;
    uint8_t kbd_reply_ = 0x7b;
    uint8_t kbd_shift_ = 0x7b;
    int kbd_bits_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
