#pragma once

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"
#include "sound/galaxian_sound.h"
#include "video/gfx.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dsp {

// Minimal i8255 mode-0 used by Scramble / Frogger family.
class Ppi8255 {
public:
    using PortRead = std::function<uint8_t()>;
    using PortWrite = std::function<void(uint8_t)>;

    void set_port_a(PortRead r, PortWrite w = {}) {
        read_a_ = std::move(r);
        write_a_ = std::move(w);
    }
    void set_port_b(PortRead r, PortWrite w = {}) {
        read_b_ = std::move(r);
        write_b_ = std::move(w);
    }
    void set_port_c(PortRead r, PortWrite w = {}) {
        read_c_ = std::move(r);
        write_c_ = std::move(w);
    }

    void reset() {
        ctrl_ = 0x9b;  // all inputs by default
        lat_a_ = lat_b_ = lat_c_ = 0xff;
    }

    uint8_t read(int index) {
        switch (index & 3) {
            case 0: return read_a_ ? read_a_() : lat_a_;
            case 1: return read_b_ ? read_b_() : lat_b_;
            case 2: return read_c_ ? read_c_() : lat_c_;
            default: return ctrl_;
        }
    }

    void write(int index, uint8_t value) {
        switch (index & 3) {
            case 0:
                lat_a_ = value;
                if (write_a_) write_a_(value);
                break;
            case 1:
                lat_b_ = value;
                if (write_b_) write_b_(value);
                break;
            case 2:
                lat_c_ = value;
                if (write_c_) write_c_(value);
                break;
            case 3:
                if (value & 0x80) ctrl_ = value;
                break;
        }
    }

private:
    uint8_t ctrl_ = 0x9b;
    uint8_t lat_a_ = 0xff, lat_b_ = 0xff, lat_c_ = 0xff;
    PortRead read_a_, read_b_, read_c_;
    PortWrite write_a_, write_b_, write_c_;
};

// Galaxian hardware family (Namco / Midway / Nichibutsu / Konami).
// Port of galaxian_hw.pas — Galaxian, Moon Cresta, Scramble, Frogger.
class Galaxian : public Machine {
public:
    enum class Game {
        Galaxian,
        MoonCresta,
        Scramble,
        Frogger,
    };

    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 256;
    static constexpr double kFramesPerSecond = 60.60606060;
    static constexpr int kScanlines = 256;
    static constexpr uint32_t kCpuClock = 3072000;
    // Master 14.318181 MHz; Z80 + AY clocked at /8 (MAME KONAMI_SOUND_CLOCK/8).
    static constexpr uint32_t kKonamiMasterClock = 14318181;
    static constexpr uint32_t kSoundClock = kKonamiMasterClock / 8;

    explicit Galaxian(Game game = Game::Galaxian);

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
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override;

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_galaxian(uint16_t address);
    void write_galaxian(uint16_t address, uint8_t value);
    uint8_t read_mooncrst(uint16_t address);
    void write_mooncrst(uint16_t address, uint8_t value);
    uint8_t read_scramble(uint16_t address);
    void write_scramble(uint16_t address, uint8_t value);
    uint8_t read_frogger(uint16_t address);
    void write_frogger(uint16_t address, uint8_t value);
    void on_cycles(int cycles);

    bool load_roms(const std::string& rom_path, std::string* error);
    void decode_graphics(const std::vector<uint8_t>& gfx_rom, int char_total, int sprite_total);
    void build_palette(const std::vector<uint8_t>& prom);
    static void decrypt_mooncrst(std::vector<uint8_t>& rom);
    void setup_scramble_ppi();
    void setup_frogger_ppi();

    int calc_nchar(int offset) const;
    void calc_sprite(int index, int& code, bool& flipx, bool& flipy) const;

    void update_video();
    void draw_tile(int offset);
    void draw_sprite(int index);
    void draw_tile_frogger(int offset);
    void draw_sprite_frogger(int index);
    void draw_bullets();
    void draw_stars();

    // Konami scramble sound board (2× AY + Z80)
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    uint8_t sound_in(uint16_t port);
    void sound_out(uint16_t port, uint8_t value);
    uint8_t konami_sound_timer_r() const;
    void run_sound(int main_cycles);

    Game game_;
    Z80 cpu_;
    Z80 sound_cpu_;
    AY8910 ay0_;
    AY8910 ay1_;
    Ppi8255 ppi0_;
    Ppi8255 ppi1_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint8_t, 0x10000> sound_memory_{};
    std::array<uint8_t, 0x400> videoram_{};
    std::array<uint8_t, 0x40> attributes_{};
    std::array<uint8_t, 0x20> sprites_{};
    std::array<uint8_t, 0x20> bullets_{};
    std::array<bool, 0x400> dirty_{};
    std::array<uint8_t, 5> gfx_bank_{};

    std::array<uint32_t, 64> palette_{};
    GfxSet chars_;
    GfxSet sprites_gfx_;

    std::array<uint32_t, 256 * 256> tilemap_{};
    std::array<uint32_t, 256 * 256> composite_{};
    std::vector<uint32_t> framebuffer_;

    bool nmi_enable_ = false;
    bool stars_enable_ = false;
    bool scramble_background_ = false;
    bool sound_present_ = false;
    uint32_t stars_scroll_ = 0;

    uint8_t in0_ = 0xff;
    uint8_t in1_ = 0xff;
    uint8_t in2_ = 0xff;
    uint8_t dsw_a_ = 0;
    uint8_t dsw_b_ = 0;
    uint8_t dsw_c_ = 0;

    uint8_t sound_latch_ = 0;
    uint8_t port_b_latch_ = 0;
    uint16_t scramble_prot_state_ = 0;
    uint8_t scramble_prot_ = 0;

    uint64_t sound_cycles_ = 0;
    bool sound_mute_ = false;
    GalaxianSound discrete_;

    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;

    bool uses_discrete_sound() const {
        return game_ == Game::Galaxian || game_ == Game::MoonCresta;
    }
    void write_discrete(uint16_t address, uint8_t value, uint16_t lfo_base, uint16_t sound_base,
                        uint16_t pitch_addr);
};

}  // namespace dsp
