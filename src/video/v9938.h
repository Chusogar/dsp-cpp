#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Yamaha V9938 (MSX2), enough of the chip for the BIOS, BASIC SCREEN 0-8 and
// MSX-DOS: 128 KiB VRAM, 16-colour palette, bitmap modes, sprites, line IRQ
// and the command engine (HMMV/HMMM/LMMV/LMMM/LINE/PSET/HMMC/LMMC).
class V9938 {
public:
    using InterruptHandler = std::function<void(bool asserted)>;

    static constexpr int kScreenWidth = 512;
    static constexpr int kScreenHeight = 212;
    static constexpr int kVramSize = 0x20000;  // 128 KiB

    explicit V9938(InterruptHandler on_interrupt);

    void reset();

    uint8_t vram_read();
    void vram_write(uint8_t value);
    uint8_t status_read();
    void register_write(uint8_t value);
    void palette_write(uint8_t value);
    void indirect_write(uint8_t value);

    void refresh_line(int line, int total_lines);

    const uint32_t* framebuffer() const { return framebuffer_.data(); }

    // Test hooks.
    uint8_t register_value(int index) const { return registers_[index & 63]; }
    bool command_executing() const { return command_ce_; }

private:
    enum Mode {
        kTxt1,
        kTxt2,
        kG1,
        kG2,
        kMc,
        kG3,
        kG4,
        kG5,
        kG6,
        kG7,
        kUnknown
    };

    Mode current_mode() const;
    int visible_height() const { return (registers_[9] & 0x80) ? 212 : 192; }
    bool display_enabled() const { return (registers_[1] & 0x40) != 0; }
    bool irq0_enabled() const { return (registers_[1] & 0x20) != 0; }
    bool irq1_enabled() const { return (registers_[0] & 0x10) != 0; }
    uint8_t backdrop() const { return uint8_t(registers_[7] & 0x0f); }

    void write_register(int index, uint8_t value);
    void update_interrupt_line();
    void increment_vram();
    uint8_t vram_get(uint32_t addr) const;
    void vram_set(uint32_t addr, uint8_t value);

    void render_scanline(int line);
    void render_text(int line, int columns);
    void render_g1(int line);
    void render_g2(int line, bool g3);
    void render_mc(int line);
    void render_bitmap(int line);
    void render_sprites(int line, int width);
    void plot(int x, int y, uint8_t color, int width);

    uint32_t palette_argb(uint8_t index) const;
    void set_default_palette();

    uint32_t pixel_address(int x, int y) const;
    int screen_width_px() const;
    int bits_per_pixel() const;
    uint8_t get_pixel(int x, int y) const;
    void put_pixel(int x, int y, uint8_t color);

    void start_command(uint8_t cmd);
    void finish_command();
    void exec_hmmv();
    void exec_hmmm();
    void exec_lmmv();
    void exec_lmmm();
    void exec_line();
    void exec_pset();
    void command_advance(int* x, int* y, int nx, int ny, int* count_x);

    uint8_t logical_op(uint8_t dst, uint8_t src, uint8_t op) const;

    InterruptHandler on_interrupt_;
    std::array<uint8_t, kVramSize> vram_{};
    std::array<uint8_t, 64> registers_{};
    std::array<uint16_t, 16> palette_{};  // 0b0RRR0BBB 00000GGG packed
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};
    std::array<uint8_t, 10> status_{};

    uint32_t address_ = 0;
    uint8_t read_buffer_ = 0;
    uint8_t latch_byte_ = 0;
    bool latch_pending_ = false;
    bool last_int_line_ = false;
    bool status_read_s0_ = true;
    uint8_t palette_byte_ = 0;
    bool palette_high_ = false;

    int scanline_ = 0;
    bool in_vblank_ = false;

    bool command_ce_ = false;
    bool command_tr_ = false;
    uint8_t command_ = 0;
    int cmd_sx_ = 0, cmd_sy_ = 0, cmd_dx_ = 0, cmd_dy_ = 0;
    int cmd_nx_ = 0, cmd_ny_ = 0;
    uint8_t cmd_clr_ = 0, cmd_arg_ = 0;
};

}  // namespace dsp
