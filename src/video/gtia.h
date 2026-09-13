#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Atari CTIA/GTIA (C014805), the chip that turns ANTIC's playfield stream
// into colour, overlays the player/missile sprites, resolves priority and
// records collisions. It also reads the joystick triggers and the three
// console keys (START/SELECT/OPTION).
//
// ANTIC hands it one byte of "playfield colour index" per colour clock
// (0-3 for the four playfield registers, plus the special hi-res and
// background cases); GTIA merges in player/missile data and produces the
// final palette index for the screen.
class Gtia {
public:
    // Playfield source codes ANTIC passes to pixel(): which colour register
    // a given colour clock wants before players are considered.
    enum Pf : uint8_t {
        kPfBak = 0,   // background (COLBK)
        kPf0 = 1, kPf1 = 2, kPf2 = 3, kPf3 = 4,
        kPfHi2 = 5,   // hi-res (mode F) luminance-only pixel over COLPF2
    };

    // Console switches: bit0 START, bit1 SELECT, bit2 OPTION (active low).
    using ConsoleRead = std::function<uint8_t()>;
    // Joystick trigger n (0-3), active low in bit 0.
    using TriggerRead = std::function<uint8_t(int)>;

    Gtia();

    void reset();
    void set_console_handler(ConsoleRead h) { console_read_ = std::move(h); }
    void set_trigger_handler(TriggerRead h) { trigger_read_ = std::move(h); }

    uint8_t read(uint16_t offset);
    void write(uint16_t offset, uint8_t value);

    // Called once per scanline before pixels are produced, with the current
    // display line, so the player/missile shifters know which graphics byte
    // applies. `pm_data` supplies the four player and four missile bytes
    // ANTIC fetched (or that were written directly to GRAFP*/GRAFM).
    void begin_line(int line);

    // Resolves one colour clock. `pf` is the playfield source code and `hx`
    // the horizontal position in colour clocks (0-227); returns a palette
    // index (0-255, the standard Atari colour byte).
    uint8_t pixel(uint8_t pf, int hx);

    // GTIA's own 9/10/11 modes replace the playfield interpretation; ANTIC
    // needs to know which is active to feed the right data.
    int prior_mode() const { return (prior_ >> 6) & 3; }

    uint8_t colbk() const { return colbk_; }

    // Player/missile graphics registers ANTIC's DMA writes into.
    void set_player(int n, uint8_t v) { grafp_[n] = v; }
    void set_missile(uint8_t v) { grafm_ = v; }

private:
    void update_pm_spans();

    uint8_t colpm_[4]{}, colpf_[4]{}, colbk_ = 0;
    uint8_t grafp_[4]{}, grafm_ = 0;
    uint8_t hposp_[4]{}, hposm_[4]{};
    uint8_t sizep_[4]{}, sizem_ = 0;
    uint8_t prior_ = 0, vdelay_ = 0, gractl_ = 0;

    // Collision latches.
    uint8_t m2pf_[4]{}, p2pf_[4]{}, m2pl_[4]{}, p2pl_[4]{};

    // Per-colour-clock occupancy for this scanline, rebuilt by begin_line().
    std::array<uint8_t, 256> player_mask_{};   // bit n = player n covers this clock
    std::array<uint8_t, 256> missile_mask_{};  // bit n = missile n covers this clock

    ConsoleRead console_read_;
    TriggerRead trigger_read_;
};

}  // namespace dsp
