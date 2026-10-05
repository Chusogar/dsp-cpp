#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// MOS 7360/8360 TED (Plus/4 / C16) — video + sound. Ported/simplified from MAME mos7360.
class Mos7360 {
public:
    static constexpr int kSampleRate = 44100;

    static constexpr int kNtscLines = 261;
    static constexpr int kPalLines = 312;

    // Visible framebuffer (MAME plus4 screen size).
    static constexpr int kVisW = 336;
    static constexpr int kVisH = 216;

    static constexpr uint32_t kNtscClock = 14318181u / 4u;  // ≈ 3579545 TED clock
    static constexpr uint32_t kPalClock = 17734470u / 5u;   // ≈ 3546894 TED clock

    enum class Variant { Ntsc, Pal };

    // Memory fetch for video. `rom_force` mirrors TED's temporary ROM enable
    // while reading chargen from ROM (INROM).
    using MemRead = std::function<uint8_t(uint16_t addr, bool rom_force)>;
    using KeyRead = std::function<uint8_t(uint8_t columns)>;
    using IrqCallback = std::function<void(bool asserted)>;

    explicit Mos7360(Variant variant = Variant::Pal);

    void set_mem_read(MemRead r) { mem_ = std::move(r); }
    void set_key_read(KeyRead r) { key_ = std::move(r); }
    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }

    void reset();

    // Full-address TED access ($FFxx). Updates *cs0/*cs1 like MAME (0 = select).
    uint8_t read(uint16_t addr, int* cs0, int* cs1);
    void write(uint16_t addr, uint8_t data, int* cs0, int* cs1);

    // Advance one raster line (0 .. lines-1), tick timers, fire raster IRQ.
    void update_line(int line);

    // Copy the visible bitmap into `dst` (vis_w * vis_h ARGB).
    void blit_visible(uint32_t* dst) const;

    // One mono sample at kSampleRate.
    int16_t update();

    bool rom_enabled() const { return rom_ != 0; }
    uint8_t bus_r() const { return last_data_; }
    int raster_line() const { return rasterline_; }
    int lines() const { return total_lines_; }
    int cycles_per_line() const { return cycles_per_line_; }
    int vis_width() const { return kVisW; }
    int vis_height() const { return kVisH; }
    Variant variant() const { return variant_; }
    uint32_t clock() const { return clock_; }
    // CPU phi2 ≈ TED clock / 2 in single-clock / display mode.
    uint32_t cpu_clock() const { return clock_ / 2u; }

    // 128 TED colors (16 hues × 8 luminances). Index is color & 0x7f.
    static const uint32_t kPalette[128];

private:
    void set_interrupt(int mask);
    void clear_interrupt(int mask);
    uint8_t read_ram(uint16_t offset);
    uint8_t read_rom(uint16_t offset);
    void draw_character(int ybegin, int yend, int ch, int yoff, int xoff, const uint16_t* color);
    void draw_character_multi(int ybegin, int yend, int ch, int yoff, int xoff);
    void draw_bitmap(int ybegin, int yend, int ch, int yoff, int xoff);
    void draw_bitmap_multi(int ybegin, int yend, int ch, int yoff, int xoff);
    void draw_cursor(int ybegin, int yend, int yoff, int xoff, int color);
    void drawlines(int first, int last);
    void soundport_w(int offset, uint8_t data);
    void sound_start();
    void put_pix(int y, int x, uint32_t argb);
    void tick_timers(int cycles);
    void start_timer(int id);
    void stop_timer(int id);
    int timer_period(int id) const;
    int cs0_r(uint16_t offset) const;
    int cs1_r(uint16_t offset) const;
    void recalc_display();

    Variant variant_;
    uint32_t clock_;
    MemRead mem_;
    KeyRead key_;
    IrqCallback irq_cb_;

    std::array<uint8_t, 0x20> reg_{};
    std::vector<uint32_t> bitmap_;

    int rom_ = 1;
    int frame_count_ = 0;
    int total_lines_ = 0;
    int cycles_per_line_ = 0;
    int cursor1_ = 0;

    int chargenaddr_ = 0;
    int bitmapaddr_ = 0;
    int videoaddr_ = 0;

    int x_begin_ = 0, x_end_ = 0;
    int y_begin_ = 0, y_end_ = 0;

    uint16_t c16_bitmap_[2]{};
    uint16_t bitmapmulti_[4]{};
    uint16_t mono_[2]{};
    uint16_t monoinversed_[2]{};
    uint16_t multi_[4]{};
    uint16_t ecmcolor_[2]{};
    uint16_t colors_[5]{};

    int rasterline_ = 0;
    int lastline_ = 0;
    int raster_col_ = 0;

    uint8_t last_data_ = 0;

    // Timers (countdown in TED/CPU-ish clocks; period from latched regs).
    std::array<int, 3> timer_count_{};
    std::array<bool, 3> timer_active_{};

    // Sound
    int tone1pos_ = 0, tone2pos_ = 0;
    int tone1samples_ = 1, tone2samples_ = 1;
    int noisesize_ = 0;
    int noisepos_ = 0;
    int noisesamples_ = 1;
    std::vector<uint8_t> noise_;
};

}  // namespace dsp
