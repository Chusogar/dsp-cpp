#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Motorola MC6845 / HD6845 CRTC, modelled after the Amstrad CPC usage in
// amstrad_cpc.pas (character-clock stepping). Suitable for CPC, BBC, etc.
//
// Advance one character clock with tick(). On CPC the CRTC runs at 1 MHz
// (CPU clock / 4). Drivers typically call tick() once per 4 T-states.
class Mc6845 {
public:
    // de = display enable for this character cell
    // hsync / vsync = current levels (active high as in the chip)
    // ma = 14-bit memory address
    // ra = row address (0..31)
    using ScanCallback = std::function<void(bool de, bool hsync, bool vsync, uint16_t ma, uint8_t ra)>;
    // Fired on the falling edge of HSYNC (CPC Gate Array counts these).
    using HsyncEndCallback = std::function<void()>;
    using VsyncCallback = std::function<void(bool active)>;

    Mc6845();

    void reset();

    // Register interface (ports 0=address, 1=data write, 2/3=status/data read).
    void select(uint8_t reg) { reg_ = reg & 0x1f; }
    void write(uint8_t value);
    uint8_t read_status() const { return 0x80; }  // light-pen not implemented
    uint8_t read() const;

    // One character clock.
    void tick();

    void set_scan_callback(ScanCallback cb) { scan_cb_ = std::move(cb); }
    void set_hsync_end_callback(HsyncEndCallback cb) { hsync_end_cb_ = std::move(cb); }
    void set_vsync_callback(VsyncCallback cb) { vsync_cb_ = std::move(cb); }

    // Accessors used by video renderers.
    uint8_t reg(int index) const { return regs_[index & 31]; }
    uint8_t selected() const { return reg_; }
    bool hsync() const { return state_hsync_; }
    bool vsync() const { return state_vsync_; }
    bool display_enable() const { return line_is_visible_ && !state_hsync_ && !state_vsync_; }
    uint16_t ma() const { return state_refresh_address_; }
    uint8_t ra() const { return state_row_address_; }
    uint16_t line_address() const { return line_address_; }
    uint8_t character_counter() const { return character_counter_; }

    // Derived geometry helpers (pixels at 16 MHz / 2 bpp style for CPC).
    int h_total_chars() const { return int(regs_[0]) + 1; }
    int h_displayed_chars() const { return int(regs_[1]); }
    int v_displayed_rows() const { return int(regs_[6]); }
    int max_scanline() const { return int(regs_[9]); }

private:
    void write_reg(uint8_t index, uint8_t value);
    void end_of_line();
    void adjust();

    static constexpr uint8_t kMasks[16] = {
        0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
        0x03, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    };

    std::array<uint8_t, 32> regs_{};
    uint8_t reg_ = 0;

    uint8_t character_counter_ = 0;
    uint8_t hsync_counter_ = 0;
    uint8_t vsync_counter_ = 0;
    uint8_t state_row_address_ = 0;
    uint8_t adj_count_ = 0;
    uint8_t char_crt_ = 0;  // vertical character row counter (R4)

    uint16_t line_address_ = 0;
    uint16_t end_of_line_address_ = 0;
    uint16_t state_refresh_address_ = 0;

    bool state_hsync_ = false;
    bool state_vsync_ = false;
    bool was_hsync_ = false;
    bool was_vsync_ = false;
    bool is_in_adjustment_period_ = false;
    bool line_is_visible_ = false;
    bool next_line_is_visible_ = false;
    bool next_line_no_visible_ = false;

    ScanCallback scan_cb_;
    HsyncEndCallback hsync_end_cb_;
    VsyncCallback vsync_cb_;
};

}  // namespace dsp
