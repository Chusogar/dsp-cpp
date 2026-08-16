#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/tms7000.h"
#include "sound/tms5220.h"
#include "video/tms3556.h"

namespace dsp {

// Exelvision EXL-100 (1984) and EXELTEL (1986).
//
// There is no Pascal driver in dsp-emulator; this machine is new in dsp-cpp
// and follows MAME `src/mame/ti/exelv.cpp` (Raphael Nabet / Robbbert):
// custom TMS7020/TMS7040 (LVDP instead of SWAP R), TMS7041/7042 I/O CPU,
// TMS3556 VDP with 32 KiB VRAM, TMS5220C speech on the sub CPU, infrared
// keyboard, cartridge at $0200-$7FFF and 2 KiB CPU RAM at $C000.
class Exelv : public Machine {
public:
    enum class Model { Exl100, Exeltel };

    static constexpr int kScreenWidth = Tms3556::kTotalWidth;
    static constexpr int kScreenHeight = Tms3556::kTotalHeight;
    static constexpr uint32_t kExl100Crystal = 4915200;
    static constexpr uint32_t kExeltelCrystal = 9830400;
    static constexpr int kScanlines = Tms3556::kScanlines;
    static constexpr double kFramesPerSecond = 50.0;
    static constexpr int kSampleRate = Tms5220::kSampleRate;

    explicit Exelv(Model model);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return vdp_.framebuffer(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override;
    bool uses_keyboard() const override { return true; }
    bool load_media(const std::string& path, std::string* error) override;

    bool bios_loaded() const { return bios_loaded_; }
    uint16_t debug_pc() const { return maincpu_.pc(); }
    uint8_t debug_a() const { return maincpu_.a(); }
    Tms3556& vdp() { return vdp_; }
    Tms7000& maincpu() { return maincpu_; }

    // Test helper: install dummy internal ROMs (IDLE + reset vector).
    void install_dummy_bios();

private:
    uint8_t read_main(uint16_t address);
    void write_main(uint16_t address, uint8_t value);
    uint8_t read_sub(uint16_t address);
    void write_sub(uint16_t address, uint8_t value);

    uint8_t mailbox_wx319_r();
    void mailbox_wx318_w(uint8_t data);
    uint8_t tms7020_porta_r();
    void tms7020_portb_w(uint8_t data);
    uint8_t tms7041_porta_r();
    void tms7041_portb_w(uint8_t data);
    uint8_t tms7041_portc_r();
    void tms7041_portc_w(uint8_t data);
    uint8_t tms7041_portd_r();
    void tms7041_portd_w(uint8_t data);
    uint8_t cart_r(uint16_t offset) const;

    void on_main_cycles(int cycles);
    void tick_keyboard(int cpu_cycles);
    uint8_t scan_key_channel() const;
    bool load_bios(const std::string& rom_path, std::string* error);
    bool load_cart_bytes(std::vector<uint8_t> data, std::string* error);

    Model model_;
    Tms7000 maincpu_;
    Tms7000 subcpu_;
    Tms3556 vdp_;
    Tms5220 speech_;

    std::array<uint8_t, 0x800> ram_{};
    std::vector<uint8_t> cart_;
    std::vector<uint8_t> system_rom_;

    uint8_t tms7020_portb_ = 0;
    uint8_t tms7041_portb_ = 0;
    uint8_t tms7041_portc_ = 0;
    uint8_t tms7041_portd_ = 0;
    uint8_t wx318_ = 0;
    uint8_t wx319_ = 0;
    bool speech_irq_ = false;
    bool sub_present_ = false;
    bool bios_loaded_ = false;
    bool hle_io_sent_ = false;
    int hle_io_delay_ = 0;

    MachineInputs inputs_{};
    uint8_t k_channels_[3] = {0xff, 0xff, 0x3e};
    uint8_t k_ch_byte_ = 0;
    uint8_t k_ch_bit_ = 0;
    bool k_bit_bit_ = false;
    bool k_bit_num_ = false;
    int k_timer_us_ = 0;
    bool k_started_ = false;
    int64_t k_boot_cycles_ = 0;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
    int cass_bit_ = 1;
};

}  // namespace dsp
