#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6809.h"
#include "cpu/z80.h"
#include "sound/cem3394.h"

namespace dsp {

// Bally/Sente SAC-1 (Hat Trick, Chicken Shift, Goalie Ghost, …).
// Main M6809 @ 1.25 MHz + sound Z80 @ 4 MHz with soft m6850 UART and 6×CEM3394.
class Balsente : public Machine {
public:
    MachineType machine_type() const override { return MachineType::Arcade; }
    enum class Game {
        Sentetst,
        Cshift,
        Hattrick,
        Gghost,
        Otwalls,
        Snakepit,
        Triviag1,
        Snakjack,
        Stocker,
        Triviabb,
        Triviag2,
        Triviayp,
        Triviasp,
        Gimeabrk,
        Minigolf,
        Teamht,
        Grudge,
        Triviaes,
        Toggle,
        Nstocker,
        Sfootbal,
        Spiker,
        Stompin,
        Nametune,
        Rescraid
    };

    static constexpr int kScreenWidth = 256;
    static constexpr int kScreenHeight = 240;
    static constexpr double kFramesPerSecond = 60.0;
    static constexpr int kScanlines = 264;  // VTOTAL = 0x108
    static constexpr uint32_t kMasterClock = 20000000;
    static constexpr uint32_t kMainClock = kMasterClock / 16;   // 1_250_000
    static constexpr uint32_t kSoundClock = 4000000;
    static constexpr uint32_t kPixelClock = kMasterClock / 4;   // 5_000_000
    static constexpr int kHTotal = 0x140;
    static constexpr int kHbStart = 0x100;
    static constexpr int kVbEnd = 0x10;
    static constexpr int kVbStart = 0x100;
    static constexpr int kSampleRate = Cem3394::kSampleRate;

    static constexpr unsigned kPoly17Bits = 17;
    static constexpr size_t kPoly17Size = (1u << kPoly17Bits) - 1;
    static constexpr unsigned kPoly17Shl = 7;
    static constexpr unsigned kPoly17Shr = 10;
    static constexpr uint32_t kPoly17Add = 0x18000;

    static constexpr uint8_t kExpandAll = 0x00;
    static constexpr uint8_t kExpandNone = 0x3f;
    static constexpr uint8_t kSwapHalves = 0x80;

    explicit Balsente(Game game);

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

    const char* title() const override;

private:
    struct Counter8253 {
        bool timer_active = false;
        int32_t initial = 0;
        int32_t count = 0;
        uint8_t gate = 0;
        uint8_t out = 0;
        uint8_t mode = 0;
        uint8_t readbyte = 0;
        uint8_t writebyte = 0;
        // Remaining 2 MHz clocks until OUT fires (counters 1/2).
        int64_t remaining_2mhz = 0;
    };

    uint8_t main_read(uint16_t address);
    void main_write(uint16_t address, uint8_t value);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);

    bool load_roms(const std::string& rom_path, std::string* error);
    void expand_roms(uint8_t cd_rom_mask);
    void apply_game_init();
    void poly17_init();

    void videoram_w(uint16_t offset, uint8_t data);
    void palette_ram_w(uint16_t offset, uint8_t data);
    void update_palette_entry(int index);
    void palette_select_w(uint8_t data);
    void rombank_select_w(uint8_t data);
    void rombank2_select_w(uint8_t data);

    uint8_t random_num_r();
    uint8_t adc_data_r();
    void adc_select_w(uint8_t offset);

    uint8_t m6850_r(uint16_t offset);
    void m6850_w(uint16_t offset, uint8_t data);
    uint8_t m6850_sound_r(uint16_t offset);
    void m6850_sound_w(uint16_t offset, uint8_t data);
    void m6850_update_io();
    void m6850_queue_transmit(uint8_t data);
    void advance_uart(int main_cycles);

    uint8_t counter_8253_r(uint16_t offset);
    void counter_8253_w(uint16_t offset, uint8_t data);
    uint8_t counter_state_r();
    void counter_control_w(uint8_t data);
    void counter_set_gate(int which, int gate);
    void counter_set_out(int which, int out);
    void counter_start(int which);
    void counter_stop(int which);
    void counter_update_count(int which);
    void counter_callback(int which);
    void set_counter_0_ff(int newstate);
    void update_counter_0_timer();
    void advance_counters(int sound_cycles);

    void chip_select_w(uint8_t data);
    void dac_data_w(uint16_t offset, uint8_t data);
    void register_addr_w(uint8_t data);

    uint8_t spiker_expand_r();
    void spiker_expand_w(uint16_t offset, uint8_t data);

    void render_frame();
    void draw_one_sprite(const uint8_t* sprite);
    void generate_audio(int main_cycles);

    Game game_;
    M6809 main_{kMainClock};
    Z80 sound_{kSoundClock};
    std::array<Cem3394, 6> cem_;

    std::vector<uint8_t> main_rom_;
    std::vector<uint8_t> sprite_rom_;
    std::array<uint8_t, 0x2000> sound_rom_{};
    std::array<uint8_t, 0x10000> sound_ram_{};

    std::array<uint8_t, 0x800> spriteram_{};       // 0x0000-0x07FF
    std::array<uint8_t, 0x7800> videoram_{};       // 0x0800-0x7FFF
    std::array<uint8_t, 256 * 240> expanded_videoram_{};
    std::array<uint8_t, 0x1000> palette_ram_{};    // 1024 × 4
    std::array<uint32_t, 1024> palette_{};
    std::array<uint8_t, 0x200> novram_{};          // 0x9B00-0x9CFF

    // Bank pointer tables (expand_roms).
    std::array<const uint8_t*, 16> bankab_{};
    std::array<const uint8_t*, 16> bankcd_{};
    std::array<const uint8_t*, 2> bankef_{};
    int bank_ab_ = 0;
    int bank_cd_ = 0;
    int bank_ef_ = 0;
    int num_banks_ = 8;

    uint8_t palettebank_vis_ = 0;
    uint32_t sprite_mask_ = 0;

    uint8_t dsw_h_ = 0xff;
    uint8_t dsw_g_ = 0xff;
    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0x7f;  // bit7 VBLANK active-high, rest active-low
    bool vblank_ = false;

    uint8_t adc_value_ = 0x80;
    uint8_t adc_shift_ = 0;
    bool shooter_ = false;

    std::array<uint8_t, kPoly17Size + 1> rand17_{};
    uint64_t main_total_cycles_ = 0;

    // Soft m6850 (main ↔ sound).
    uint8_t m6850_status_ = 0x02;
    uint8_t m6850_control_ = 0;
    uint8_t m6850_input_ = 0;
    uint8_t m6850_output_ = 0;
    bool m6850_data_ready_ = false;
    bool m6850_tx_pending_ = false;
    uint8_t m6850_tx_byte_ = 0;
    int m6850_tx_delay_ = 0;  // main cycles until data_ready

    uint8_t m6850_sound_status_ = 0x02;
    uint8_t m6850_sound_control_ = 0;
    uint8_t m6850_sound_input_ = 0;
    uint8_t m6850_sound_output_ = 0;

    // 8253-5 + counter 0 FF.
    std::array<Counter8253, 3> counter_{};
    uint8_t counter_control_ = 0;
    uint8_t counter_0_ff_ = 0;
    bool counter_0_timer_active_ = false;
    double counter_0_period_cycles_ = 0;  // sound cycles per FF clock
    double counter_0_phase_ = 0;

    uint16_t dac_value_ = 0;
    uint8_t dac_register_ = 0;
    uint8_t chip_select_ = 0x3f;

    uint8_t spiker_expand_color_ = 0;
    uint8_t spiker_expand_bgcolor_ = 0;
    uint8_t spiker_expand_bits_ = 0;

    std::array<uint32_t, size_t(kScreenWidth) * kScreenHeight> framebuffer_{};
    std::vector<int16_t> audio_;
    double audio_error_ = 0;
};

}  // namespace dsp
