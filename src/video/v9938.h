#pragma once

#include <array>
#include <cstdint>

namespace dsp {

// Yamaha V9938. Scanout, planar VRAM and the command engine follow CNGSOFT
// MSXEC (cpcec) algorithms; commands run instantly rather than cycle-accurately.
// Framebuffer is 544×240 including the PAL border.
class V9938 {
public:
    static constexpr int kPaperWidth = 512;
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
    int memtype() const;
    uint8_t vram_rd(uint32_t addr) const;
    void vram_wr(uint32_t addr, uint8_t value);
    uint32_t cpu_linear() const;
    uint32_t cpu_phys() const;
    void next_where();
    uint8_t ram_recv();
    void ram_send(uint8_t value);

    uint32_t ink(uint8_t index) const;
    uint32_t backdrop() const;
    int bit_bmp() const;
    int map_bm4() const;
    int map_bm8() const;

    void render_t1(int y, uint32_t* buf);
    void render_t2(int y, uint32_t* buf);
    void render_g1(int y, uint32_t* buf);
    void render_g2(int y, uint32_t* buf);
    void render_mc(int y, uint32_t* buf);
    void render_g4(int y, uint32_t* buf);
    void render_g5(int y, uint32_t* buf);
    void render_g6(int y, uint32_t* buf);
    void render_g7(int y, uint32_t* buf);
    void render_sprites_m1(int y, uint32_t* buf);
    void render_sprites_m2(int y, uint32_t* buf);

    int blit_update();
    uint8_t* blit_offs(int x, int y);
    uint8_t blit_test(int x, int y);
    void blit_logo(int x, int y, uint8_t color);
    unsigned blit_get_sx() const;
    unsigned blit_get_sy() const;
    unsigned blit_get_dx() const;
    unsigned blit_get_dy() const;
    unsigned blit_get_nx() const;
    unsigned blit_get_ny(int add) const;
    void blit_set_sy(unsigned y);
    void blit_set_dy(unsigned y);
    void blit_set_ny(unsigned y);
    void blit_launch();
    void blit_run();
    void blit_lmcm();
    void write_register(int index, uint8_t value);

    std::array<uint8_t, kVramSize> vram_{};
    std::array<uint8_t, kNumRegs> regs_{};
    std::array<uint8_t, kNumStatus> status_{};
    std::array<uint32_t, 16> palette_{};
    uint8_t pal_rgb_[16][3]{};
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};

    uint8_t latch_ = 0;
    bool latch_flag_ = false;
    uint8_t read_buf_ = 0;
    int vram_where_ = 0;
    uint8_t pal_latch_ = 0;
    bool pal_flag_ = false;
    int frame_counter_ = 0;
    bool irq_vblank_ = false;
    bool irq_hblank_ = false;

    int cmd_op_ = 0;
    bool cmd_busy_ = false;
    unsigned blit_nx_ = 0;
    unsigned blit_sx_ = 0;
    unsigned blit_dx_ = 0;
    int blit_nz_ = 0;
    int8_t blit_ax_ = 1;
    int8_t blit_ay_ = 1;
    int8_t blit_case_ = -1;
    int8_t blit_addx_ = 1;
    uint8_t blit_step_ = 1;
    uint8_t blit_bits_ = 0;
    uint8_t blit_mask_ = 15;
    int blit_xl_ = 255;
    int blit_yl_ = 1023;
    int blit_xh_ = ~255;
    int blit_yh_ = ~1023;
};

}  // namespace dsp
