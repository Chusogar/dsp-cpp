#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "sound/mos7360.h"

namespace dsp {

// Commodore Plus/4 / C16 (TED MOS 7360). Prefer Plus/4 64K; C16 uses 16K.
class Plus4 : public Machine {
public:
    enum class Model { Plus4_64K, C16_16K };
    enum class Region { Pal, Ntsc };

    explicit Plus4(Model model = Model::Plus4_64K, Region region = Region::Pal);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override { (void)bank; (void)value; }

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return ted_.vis_width(); }
    int screen_height() const override { return ted_.vis_height(); }
    double frames_per_second() const override { return frames_per_second_; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return Mos7360::kSampleRate; }

    const char* title() const override;
    bool uses_keyboard() const override { return true; }

    bool load_media(const std::string& path, std::string* error) override;

private:
    static constexpr int kPrgInjectFrames = 150;

    enum class RomView { Latch, ForceRam, ForceRom };

    uint8_t read_byte(uint16_t addr);
    void write_byte(uint16_t addr, uint8_t value);
    uint8_t read_memory(uint16_t offset, RomView rom_view);
    uint8_t ted_videoram_r(uint16_t offset, bool rom_force);
    uint8_t ted_k_r(uint8_t columns);
    void on_cycles(int cycles);
    void update_irq();
    bool load_roms(const std::string& path, std::string* error);
    bool queue_prg(const std::vector<uint8_t>& data, std::string* error);
    void update_pending_prg();
    void inject_prg(const std::vector<uint8_t>& data);

    Model model_;
    Region region_;
    double frames_per_second_ = 50.0;

    M6502 cpu_;
    Mos7360 ted_;

    std::vector<uint8_t> ram_;
    uint32_t ram_mask_ = 0xffff;
    std::array<uint8_t, 0x8000> kernal_basic_{};  // BASIC $0000 + KERNAL $4000
    std::array<uint8_t, 0x8000> function_{};      // LO $0000 + HI $4000 (Plus/4)
    bool have_function_ = false;

    // 7501 I/O port ($00/$01), same idea as 6510.
    uint8_t port_ddr_ = 0;
    uint8_t port_out_ = 0;

    // MOS 6529 keyboard column latch ($FD3x) and TED bank address ($FDDx).
    uint8_t kb_ = 0xff;
    uint8_t addr_latch_ = 0;

    // keyboard_[row] = active-low column bits for that matrix row (MAME ROW0..7).
    std::array<uint8_t, 8> keyboard_{};
    uint8_t joy1_ = 0xff;  // active-low UDLR + fire in bit5 style for ted_k_r
    uint8_t joy2_ = 0xff;

    bool ted_irq_ = false;

    std::vector<uint8_t> pending_prg_;
    int boot_frames_ = 0;

    int cpu_cycle_debt_ = 0;
    int64_t audio_acc_ = 0;
    std::vector<uint32_t> framebuffer_;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
