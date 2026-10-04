#pragma once

#include <array>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/machine.h"
#include "cpu/ppc603.h"
#include "machine/eeprom93c46.h"
#include "machine/model3_sound.h"
#include "machine/sega_315_5881.h"
#include "video/model3_tilegen.h"
#include "video/real3d.h"

namespace dsp {

// Sega Model 3 Step 2.1 main board: PowerPC 603r, MPC106 PCI bridge, Real3D
// Pro-1000 3D board, tile generator, 315-5649 I/O (inputs, ADC, EEPROM),
// RTC 72421, backup RAM, IRQ controller, CROM banking and the 315-5881
// security board. Games: Star Wars Trilogy Arcade (swtrilgy, Rev. A).
//
// Sound: 68000 sound board with two SCSPs plus the DSB2 MPEG music board
// (see Model3Sound), mixed down to mono for the front end.
class Model3 : public Machine, private Ppc603::Bus {
public:
    static constexpr int kScreenWidth = 496;
    static constexpr int kScreenHeight = 384;
    static constexpr double kFramesPerSecond = 57.524160;

    Model3();
    ~Model3() override;

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override { (void)bank; (void)value; }

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return 44100; }

    const char* title() const override { return "Star Wars Trilogy Arcade (Sega Model 3)"; }

    // Threaded rendering: the 3D rasterizer of frame N runs on a second
    // thread while frame N+1 is emulated (the picture is one frame late).
    void set_threaded(bool on);

    // Emulated PowerPC clock (default 166 MHz; can be lowered for speed).
    void set_cpu_clock(uint32_t hz);

    // Debug / test access.
    Ppc603& cpu() { return cpu_; }
    Real3D& real3d() { return gpu_; }
    Model3TileGen& tilegen() { return tilegen_; }
    Model3Sound& sound() { return sound_; }
    uint8_t irq_enable() const { return irq_enable_; }
    uint32_t irq_state() const { return irq_state_; }
    uint8_t crom_bank() const { return crom_bank_reg_; }
    uint32_t read32_debug(uint32_t a) { return read32(a); }
    uint64_t frame_count() const { return frames_; }

private:
    // Ppc603::Bus
    uint8_t read8(uint32_t a) override;
    uint16_t read16(uint32_t a) override;
    uint32_t read32(uint32_t a) override;
    void write8(uint32_t a, uint8_t v) override;
    void write16(uint32_t a, uint16_t v) override;
    void write32(uint32_t a, uint32_t v) override;

    bool load_roms(const std::string& rom_path, std::string* error);
    void set_crom_bank(uint8_t value);
    void irq_assert(uint32_t bits);
    void irq_clear(uint32_t bits);
    void update_irq();

    uint8_t read_inputs(unsigned reg);
    void write_inputs(unsigned reg, uint8_t value);
    uint8_t read_system(unsigned reg) const;
    void write_system(unsigned reg, uint8_t value);
    uint8_t read_rtc(unsigned reg) const;
    uint32_t read_security(unsigned reg);
    void write_security(unsigned reg, uint32_t value);
    uint32_t pci_config_data_read();
    void compose();
    void finish_render();
    void mix_layers(std::vector<uint32_t>& dst);

    Ppc603 cpu_;
    Real3D gpu_;
    Model3TileGen tilegen_;
    Eeprom93C46 eeprom_{16};
    Sega3155881 crypt_;
    Model3Sound sound_;
    std::vector<int16_t> stereo_;

    std::vector<uint8_t> ram_;          // 8 MB, big-endian byte order
    std::vector<uint8_t> crom_;         // fixed 8 MB
    std::vector<uint8_t> crom_banked_;  // 3 x 16 MB
    std::vector<uint8_t> vrom_;         // 64 MB, raw interleave (LE words)
    std::vector<uint8_t> backup_;       // 128 KB, big-endian byte order
    std::vector<uint8_t> security_ram_; // 128 KB, big-endian byte order
    std::vector<uint8_t> blank_bank_;

    uint8_t crom_bank_reg_ = 0xff;
    uint8_t irq_enable_ = 0;
    uint32_t irq_state_ = 0;
    uint8_t midi_ctrl_ = 0;
    uint8_t input_bank_ = 0;
    uint8_t adc_channel_ = 0;
    bool security_first_read_ = true;
    mutable std::time_t rtc_time_ = 0;
    mutable std::tm rtc_tm_{};

    // MPC106
    std::array<uint8_t, 256> mpc_regs_{};
    uint32_t pci_bus_ = 0, pci_device_ = 0, pci_function_ = 0, pci_reg_ = 0;

    // Inputs
    MachineInputs inputs_{};
    int joy_x_ = 0x80, joy_y_ = 0x80;

    uint32_t cpu_hz_ = 166000000;
    uint64_t frames_ = 0;
    std::vector<uint32_t> framebuffer_;
    std::vector<uint32_t> frame3d_;
    std::vector<uint32_t> fb_work_;
    std::vector<uint32_t> bottom_snap_, top_snap_;
    std::thread worker_;
    bool threaded_ = true;
    bool have_pending_ = false;
    std::vector<int16_t> audio_;
    double audio_frac_ = 0;
};

}  // namespace dsp
