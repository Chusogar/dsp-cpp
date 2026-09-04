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
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "Atari ST"; }
    bool uses_keyboard() const override { return true; }
    bool uses_pointer() const override { return true; }

    uint32_t debug_pc() const { return cpu_.pc(); }
    uint8_t peek(uint32_t address) const { return const_cast<AtariSt*>(this)->read_byte(address); }
    std::vector<uint8_t> ikbd_pending_bytes() const;
    bool floppy_loaded() const { return floppy_.loaded(); }
    int floppy_spt() const { return floppy_.spt(); }
    int floppy_tracks() const { return floppy_.tracks(); }

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
    void service_acia();

    M68000 cpu_;
    AY8910 psg_;
    Mc68901 mfp_;
    StFloppy floppy_;

    std::vector<uint8_t> ram_;
    std::vector<uint8_t> rom_;
    std::array<uint32_t, kWidth * kHeight> framebuffer_{};
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
    std::deque<uint8_t> ikbd_rx_;
    struct IkbdByte {
        uint8_t value = 0;
        int cycles = 0;
    };
    std::deque<IkbdByte> ikbd_pending_;
    uint8_t ikbd_cmd_ = 0;
    int ikbd_reset_step_ = 0;
    int last_pointer_x_ = 0;
    int last_pointer_y_ = 0;
    int pointer_frac_x_ = 0;
    int pointer_frac_y_ = 0;
    bool pointer_seen_ = false;
    bool last_pointer_b1_ = false;
    bool last_pointer_b2_ = false;
    uint32_t video_count_ = 0;

    int64_t mfp_acc_ = 0;
    int64_t audio_acc_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
