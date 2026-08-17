#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Yamaha V9938, ported from zxtiny `zxm/msx2.c`. 128 KiB VRAM, MSX2 screen
// modes 0–8, sprites, palette, and the command engine (instant, not
// cycle-accurate). Framebuffer is 288×240 including the PAL border.
class V9938 {
public:
    static constexpr int kPaperWidth = 256;
    static constexpr int kPaperHeight = 212;
    static constexpr int kBorderH = 16;
    static constexpr int kBorderV = 14;
    static constexpr int kScreenWidth = kPaperWidth + 2 * kBorderH;
    static constexpr int kScreenHeight = kPaperHeight + 2 * kBorderV;
    static constexpr int kVramSize = 128 * 1024;
    static constexpr int kNumRegs = 48;
    static constexpr int kNumStatus = 10;

    V9938();

    void reset();
    uint8_t port_read(int port);
    void port_write(int port, uint8_t value);
    // LMMC/HMMC CPU→VRAM bytes go through R#44 (and port $98) while a command is live.
    void command_write_byte(uint8_t data);
    bool command_busy() const { return cmd_busy_; }
    int command_op() const { return cmd_op_; }

    void render_line(int line);
    void begin_frame();
    void check_line_irq(int line);
    bool irq_pending() const { return irq_vblank_ || irq_hblank_; }

    const uint32_t* framebuffer() const { return framebuffer_.data(); }
    uint8_t reg(int index) const { return regs_[std::size_t(index)]; }
    int screen_mode() const;

private:
    int active_lines() const;
    uint8_t vram_rd(uint32_t addr) const;
    void vram_wr(uint32_t addr, uint8_t value);
    void render_t1(int line, uint32_t* buf);
    void render_g1(int line, uint32_t* buf);
    void render_g2(int line, uint32_t* buf);
    void render_mc(int line, uint32_t* buf);
    void render_g4(int line, uint32_t* buf);
    void render_g5(int line, uint32_t* buf);
    void render_g6(int line, uint32_t* buf);
    void render_g7(int line, uint32_t* buf);
    void render_sprites_m1(int line, uint32_t* buf);
    void render_sprites_m2(int line, uint32_t* buf);
    uint8_t log_op(int op, uint8_t src, uint8_t dst) const;
    uint32_t bitmap_addr(int x, int y) const;
    int display_y_offset() const;
    int pixels_per_byte() const;
    uint8_t get_pixel(int x, int y) const;
    void set_pixel(int x, int y, uint8_t clr);
    void exec_command();
    void write_register(int index, uint8_t value);
    void start_cpu_transfer(int cmd, int dx, int dy, int nx, int ny, int arg, uint8_t first);
    uint8_t command_read_byte();
    int line_x_mask() const;

    std::array<uint8_t, kVramSize> vram_{};
    std::array<uint8_t, kNumRegs> regs_{};
    std::array<uint8_t, kNumStatus> status_{};
    std::array<uint32_t, 16> palette_{};
    uint8_t pal_rgb_[16][3]{};
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};

    uint8_t latch_ = 0;
    bool latch_flag_ = false;
    uint8_t read_buf_ = 0;
    uint32_t vram_addr_ = 0;
    bool vram_write_ = false;
    uint8_t pal_latch_ = 0;
    bool pal_flag_ = false;
    int scanline_ = 0;
    int frame_counter_ = 0;
    bool irq_vblank_ = false;
    bool irq_hblank_ = false;
    int cmd_sx_ = 0, cmd_sy_ = 0, cmd_dx_ = 0, cmd_dy_ = 0;
    int cmd_nx_ = 0, cmd_ny_ = 0, cmd_clr_ = 0, cmd_arg_ = 0, cmd_op_ = 0;
    int cmd_px_ = 0, cmd_py_ = 0;
    bool cmd_busy_ = false;
};

}  // namespace dsp
