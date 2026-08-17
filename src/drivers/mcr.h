#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "cpu/z80ctc.h"
#include "sound/ay8910.h"
#include "video/gfx.h"

namespace dsp {

// Midway MCR-II / MCR-III (Tapper, Tron, Satan's Hollow, Domino Man, Wacko,
// Discs of Tron, Timber), ported from mcr_hw.pas: dual Z80, CTC daisy IRQs,
// SSIO (2×AY-8910 + 14024 /SINT), 16×16 tiles and 32×32 sprites.
class Mcr : public Machine {
public:
    static constexpr int kScreenW = 512;
    static constexpr int kScreenH = 480;
    static constexpr uint32_t kMainClock = 5000000;
    static constexpr uint32_t kSoundClock = 2000000;
    static constexpr int kScanlines = 480;
    static constexpr int kCpuSync = 4;
    static constexpr double kFps = 30.0;
    static constexpr int kSampleRate = AY8910::kSampleRate;

    enum class Game { Tapper, Tron, Shollow, Domino, Wacko, Dotron, Timber };

    explicit Mcr(Game game = Game::Tapper);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenW; }
    int screen_height() const override { return kScreenH; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override;

    uint16_t debug_pc() const { return main_cpu_.pc(); }
    uint16_t debug_sp() const { return main_cpu_.sp; }
    uint16_t debug_sound_pc() const { return sound_cpu_.pc(); }
    uint8_t debug_im() const { return main_cpu_.im; }
    uint8_t debug_i() const { return main_cpu_.i; }
    bool debug_iff1() const { return main_cpu_.iff1; }
    bool debug_halted() const { return main_cpu_.halted; }
    uint8_t debug_read(uint16_t a) { return main_read(a); }
    int debug_ctc_irqs() const { return ctc_irqs_; }
    uint16_t debug_ix() const { return main_cpu_.ix; }
    uint16_t debug_iy() const { return main_cpu_.iy; }
    uint8_t debug_a() const { return main_cpu_.a; }
    uint8_t debug_f() const { return main_cpu_.f; }
    void debug_set_instruction_hook(Z80::InstructionHook hook) {
        main_cpu_.set_instruction_hook(std::move(hook));
    }

private:
    uint8_t tapper_read(uint16_t addr);
    void tapper_write(uint16_t addr, uint8_t value);
    uint8_t tron_read(uint16_t addr);
    void tron_write(uint16_t addr, uint8_t value);
    uint8_t main_read(uint16_t addr);
    void main_write(uint16_t addr, uint8_t value);
    uint8_t main_in(uint16_t port);
    void main_out(uint16_t port, uint8_t value);
    uint8_t sound_read(uint16_t addr);
    void sound_write(uint16_t addr, uint8_t value);
    void on_main_cycles(int cycles);
    void ssio_14024_tick();
    void update_video();
    void update_video_tapper();
    void update_video_tron();
    void set_color(int index, uint16_t value);
    void apply_inputs_tapper(const MachineInputs& in);
    void apply_inputs_dotron(const MachineInputs& in);
    void apply_inputs_tron(const MachineInputs& in);
    void apply_inputs_shollow(const MachineInputs& in);
    void apply_inputs_domino(const MachineInputs& in);
    void apply_inputs_wacko(const MachineInputs& in);
    bool load_roms(const std::string& path, std::string* error);
    void decode_chars(const std::vector<uint8_t>& rom, int count);
    void decode_sprites(const std::vector<uint8_t>& rom, int count);

    bool is_tron_map() const {
        return game_ == Game::Tron || game_ == Game::Shollow || game_ == Game::Domino ||
               game_ == Game::Wacko;
    }

    Game game_;
    Z80 main_cpu_;
    Z80 sound_cpu_;
    Z80Ctc ctc_;
    AY8910 ay0_;
    AY8910 ay1_;

    std::array<uint8_t, 0x10000> mem_{};
    std::array<uint8_t, 0x10000> sound_mem_{};
    std::array<uint8_t, 0x800> nvram_{};
    std::array<uint32_t, 256> palette_{};
    std::array<uint32_t, kScreenW * kScreenH> framebuffer_{};

    GfxSet chars_;
    GfxSet sprites_;

    uint8_t in0_ = 0xff, in1_ = 0xff, in2_ = 0xff, in3_ = 0xff, dsw_ = 0xc0;
    uint8_t analog_x_ = 0x80, analog_y_ = 0x80;
    uint8_t ssio_status_ = 0;
    std::array<uint8_t, 4> ssio_data_{};
    uint8_t ssio_14024_ = 0;
    int ssio_14024_acc_ = 0;
    int ctc_irqs_ = 0;

    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;
};

}  // namespace dsp
