#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "cpu/z8002.h"
#include "video/gfx.h"

namespace dsp {

// Namco Pole Position / Pole Position II: Z80 + dual Z8002, road + sprites.
class PolePos : public Machine {
public:
    enum class Game { PolePosition, PolePosition2 };

    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 224;
    static constexpr int kRawHeight = 256;
    static constexpr int kVisTop = 16;
    static constexpr int kScanlines = 264;
    static constexpr uint32_t kMasterClock = 24576000;
    static constexpr uint32_t kCpuClock = kMasterClock / 8;  // 3.072 MHz
    static constexpr double kFramesPerSecond = double(kMasterClock / 4) / (384.0 * 264.0);
    static constexpr int kSampleRate = 44100;
    static constexpr int kCyclesPerLine = 192;

    explicit PolePos(Game game);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override {
        return game_ == Game::PolePosition2 ? "Pole Position II" : "Pole Position";
    }

    uint16_t debug_z80_pc() const { return z80_.pc(); }
    uint32_t debug_sub1_pc() const { return sub1_.pc(); }
    uint32_t debug_sub2_pc() const { return sub2_.pc(); }
    uint8_t debug_ls259() const { return ls259_; }
    uint8_t debug_chacl() const { return chacl_; }
    uint8_t debug_sub_irq_mask() const { return sub_irq_mask_; }
    bool debug_sub1_reset() const { return sub1_reset_; }
    bool debug_sub2_reset() const { return sub2_reset_; }
    uint16_t debug_view_hscroll() const { return view_hscroll_; }
    uint16_t debug_road_vscroll() const { return road_vscroll_; }
    uint16_t debug_alpha_word(int index) const {
        return uint16_t((uint16_t(alpha_ram_[size_t(index * 2)]) << 8) | alpha_ram_[size_t(index * 2 + 1)]);
    }
    uint16_t debug_view_word(int index) const {
        return uint16_t((uint16_t(view_ram_[size_t(index * 2)]) << 8) | view_ram_[size_t(index * 2 + 1)]);
    }
    uint8_t debug_char_pixel(int code, int x, int y) const {
        const uint8_t* pix = chars_.element(code);
        return pix[y * 8 + x];
    }

private:
    uint8_t z80_read(uint16_t address);
    void z80_write(uint16_t address, uint8_t value);
    uint8_t z80_in(uint16_t port);
    void z80_out(uint16_t port, uint8_t value);
    uint8_t z8002_read(int which, uint16_t address);
    void z8002_write(int which, uint16_t address, uint8_t value);
    void on_z80_cycles(int cycles);

    uint8_t ready_r() const;
    void ls259_w(int bit, bool value);
    uint8_t analog_r() const;
    uint16_t ic25_r(uint16_t offset);

    uint8_t namco06_data_r();
    void namco06_data_w(uint8_t data);
    uint8_t namco06_ctrl_r() const { return n06_control_; }
    void namco06_ctrl_w(uint8_t data);
    void namco06_tick();

    uint8_t namco51_read();
    void namco51_write(uint8_t data);
    void namco51_vblank();
    uint8_t namco53_read();

    uint8_t in0() const;

    void decode_graphics();
    void build_palette();
    void update_video();
    void draw_background();
    void draw_road();
    void draw_sprites();
    void draw_text();
    void zoom_sprite(bool big, uint32_t code, uint32_t color, bool flipx, int sx, int sy,
                     int sizex, int sizey);

    uint32_t pen_rgb(int pen) const;

    Game game_;
    Z80 z80_;
    Z8002 sub1_;
    Z8002 sub2_;

    std::vector<uint8_t> z80_rom_;
    std::vector<uint8_t> sub1_rom_;
    std::vector<uint8_t> sub2_rom_;
    std::array<uint8_t, 0x800> nvram_{};
    std::array<uint8_t, 0x400> sound_ram_{};
    std::array<uint8_t, 0x1000> sprite_ram_{};
    std::array<uint8_t, 0x800> road_ram_{};
    std::array<uint8_t, 0x800> alpha_ram_{};
    std::array<uint8_t, 0x1000> view_ram_{};
    std::vector<uint8_t> char_rom_;
    std::vector<uint8_t> tile_rom_;
    std::vector<uint8_t> sprite_rom_;
    std::vector<uint8_t> bigsprite_rom_;
    std::vector<uint8_t> road_rom_;
    std::vector<uint8_t> scalelut_;
    std::vector<uint8_t> proms_;
    std::vector<uint8_t> namco_wavetable_;

    GfxSet chars_;
    GfxSet tiles_;
    GfxSet sprites_;
    GfxSet bigsprites_;

    std::array<uint32_t, 128> rgb_{};
    std::array<uint16_t, 0xf00> pens_{};
    std::array<uint16_t, 256> vpos_mod_{};
    std::array<uint32_t, kScreenWidth * kRawHeight> bitmap_{};
    std::vector<uint32_t> framebuffer_;

    uint16_t view_hscroll_ = 0;
    uint16_t road_vscroll_ = 0;
    uint8_t ls259_ = 0;
    uint8_t chacl_ = 0;
    uint8_t sub_irq_mask_ = 0;
    bool sub1_reset_ = true;
    bool sub2_reset_ = true;
    bool irq_enable_ = false;
    bool adc_channel_ = false;
    bool adc_ready_ = true;
    uint8_t adc_value_ = 0;
    int scanline_ = 0;

    uint8_t dswa_ = 0xff;
    uint8_t dswb_ = 0x6b;  // rank B/C, unknown off, demo sounds on
    uint8_t in0_ = 0xff;
    uint8_t accel_ = 0;
    uint8_t brake_ = 0;
    uint8_t steer_ = 0x80;
    bool gear_hi_ = false;
    bool gear_button_prev_ = false;

    // Namco 06xx
    uint8_t n06_control_ = 0;
    bool n06_timer_state_ = false;
    bool n06_read_stretch_ = false;
    int n06_cycle_acc_ = 0;
    int n06_period_cycles_ = 0;

    // Namco 51xx (high-level I/O, Pole Position nibble buffer)
    int n51_mode_ = 1;  // 0 idle/credit, 1 init/switch, 2 active
    int n51_out_index_ = 0;
    int n51_coinage_left_ = 0;
    std::array<uint8_t, 8> n51_out_{};
    std::array<uint8_t, 4> n51_coinage_{1, 1, 1, 1};
    uint8_t n51_cred_lo_ = 0;
    uint8_t n51_cred_hi_ = 0;
    uint8_t n51_coin1_partial_ = 0;
    uint8_t n51_coin2_partial_ = 0;
    uint8_t n51_in0_prev_ = 0xff;

    // Namco 53xx (high-level I/O)
    uint8_t steer_last_ = 0x80;
    uint8_t steer_delta_ = 0;
    int16_t steer_accum_ = 0;

    // Pole Position II IC25 multiply
    int16_t ic25_result_ = 0;
    int8_t ic25_signed_ = 0;
    uint8_t ic25_unsigned_ = 0;
    uint16_t ic25_word_ = 0;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
