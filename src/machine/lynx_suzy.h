#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Atari Lynx Suzy (Mikey’s sprite/math companion). 16-bit multiply/divide
// with optional accumulate, the SCB sprite blitter (packed and RLE, stretch,
// tilt, flip, collision) and the cartridge/joystick ports at $FCB0-$FCB3.
class LynxSuzy {
public:
    using CartRead = std::function<uint8_t()>;
    using CartWrite = std::function<void(uint8_t)>;

    static constexpr int kScreenWidth = 160;
    static constexpr int kScreenHeight = 102;

    LynxSuzy();

    void set_ram(uint8_t* ram) { ram_ = ram; }
    void set_cart0(CartRead read, CartWrite write = nullptr) {
        cart0_read_ = std::move(read);
        cart0_write_ = std::move(write);
    }
    void set_cart1(CartRead read, CartWrite write = nullptr) {
        cart1_read_ = std::move(read);
        cart1_write_ = std::move(write);
    }

    void reset();
    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t value);

    // Sprite engine remaining cycles (CPU is halted via Mikey CPUSLEEP).
    void tick(int cycles);
    bool busy() const { return busy_; }

    void set_joystick(uint8_t joy) { joystick_ = joy; }
    void set_switches(uint8_t value) { switches_ = value; }
    uint8_t joystick() const { return joystick_; }

    uint16_t vidbas() const { return uint16_t(screen_); }

private:
    uint8_t ram_read(uint16_t address) const;
    void ram_write(uint16_t address, uint8_t value);
    void ram_write_nibble(uint16_t address, uint8_t nibble, bool high);
    uint8_t ram_read_nibble(uint16_t address, bool high) const;
    uint16_t ram_word(uint16_t address) const;

    void multiply();
    void divide();
    void blitter();
    void blit_lines();
    void blit_packed(int16_t y, int xdir, int bpp, uint8_t mask);
    void blit_rle(int16_t y, int xdir, int bpp, uint8_t mask);
    void plot(int16_t x, int16_t y, uint8_t color);

    uint8_t* ram_ = nullptr;
    std::array<uint8_t, 0x100> data_{};

    CartRead cart0_read_, cart1_read_;
    CartWrite cart0_write_, cart1_write_;

    bool signed_math_ = false;
    bool accumulate_ = false;
    bool accumulate_overflow_ = false;
    int sign_ab_ = 1;
    int sign_cd_ = 1;

    uint16_t screen_ = 0;
    uint16_t colbuf_ = 0;
    uint16_t colpos_ = 0;
    int16_t xoff_ = 0;
    int16_t yoff_ = 0;
    uint8_t mode_ = 0;
    uint8_t spr_coll_ = 0;
    uint8_t spritenr_ = 0;
    int16_t x_pos_ = 0;
    int16_t y_pos_ = 0;
    uint16_t width_ = 0x100;
    uint16_t height_ = 0x100;
    int16_t tilt_acc_ = 0;
    uint16_t height_acc_ = 0;
    uint16_t width_offset_ = 0x80;
    uint16_t height_offset_ = 0x80;
    int16_t stretch_ = 0;
    int16_t tilt_ = 0;
    std::array<uint8_t, 16> color_{};
    uint16_t bitmap_ = 0;
    bool use_rle_ = false;
    uint8_t line_color_ = 0;
    uint8_t spr_ctl0_ = 0;
    uint8_t spr_ctl1_ = 0;
    uint16_t scb_ = 0;
    uint16_t scb_next_ = 0;
    bool sprite_collide_ = false;
    bool everon_ = false;
    uint8_t fred_ = 0;
    int memory_accesses_ = 0;
    bool no_collide_ = false;
    bool vstretch_ = false;
    bool lefthanded_ = false;
    bool busy_ = false;
    int remaining_ = 0;

    uint8_t joystick_ = 0;
    uint8_t switches_ = 0;
};

}  // namespace dsp
