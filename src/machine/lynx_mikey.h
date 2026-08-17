#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace dsp {

// Atari Lynx Mikey: eight general timers (HBL/VBL/UART plus four audio
// channels), INTSET/INTRST, 16-colour 12-bit palette, LCD DMA control,
// parallel I/O for the cartridge shift register, UART stub and CPUSLEEP.
class LynxMikey {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kTimerCount = 8;
    static constexpr int kAudioCount = 4;

    using IrqCallback = std::function<void(bool)>;
    using WakeCallback = std::function<void()>;
    using SysctlCallback = std::function<void(uint8_t sysctl, uint8_t iodat)>;
    using ScanlineCallback = std::function<void(int vcounter)>;

    LynxMikey();

    void set_irq_callback(IrqCallback cb) { irq_cb_ = std::move(cb); }
    void set_wake_callback(WakeCallback cb) { wake_cb_ = std::move(cb); }
    void set_sysctl_callback(SysctlCallback cb) { sysctl_cb_ = std::move(cb); }
    void set_scanline_callback(ScanlineCallback cb) { scanline_cb_ = std::move(cb); }

    void reset();
    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t value);

    // Advance by 65C02 cycles (4 Mikey clocks each at 16 MHz).
    void tick(int cpu_cycles);

    bool irq_pending() const { return interrupt_ != 0; }
    uint8_t interrupt() const { return interrupt_; }
    uint16_t disp_addr() const { return disp_addr_; }
    uint16_t display_pointer() const { return uint16_t(data_[0x94] | (data_[0x95] << 8)); }
    bool video_dma() const { return (data_[0x92] & 0x01) != 0; }
    bool flip_screen() const { return (data_[0x92] & 0x02) != 0; }
    uint8_t iodir() const { return data_[0x8a]; }
    uint8_t iodat() const { return data_[0x8b]; }
    uint32_t pal_argb(int index) const;

    int16_t mix_sample() const;

private:
    struct Timer {
        uint8_t bakup = 0;
        uint8_t cntrl1 = 0;
        uint8_t cntrl2 = 0;
        uint8_t counter = 0;
        int accum = 0;

        bool int_en() const { return (cntrl1 & 0x80) != 0; }
        bool reload_en() const { return (cntrl1 & 0x10) != 0; }
        bool count_en() const { return (cntrl1 & 0x08) != 0; }
        bool linked() const { return (cntrl1 & 0x07) == 0x07; }
        int clock_sel() const { return cntrl1 & 0x07; }
        bool done() const { return (cntrl2 & 0x08) != 0; }
        void set_done(bool value) {
            if (value) {
                cntrl2 |= 0x08;
            } else {
                cntrl2 &= uint8_t(~0x08);
            }
        }
    };

    struct Audio {
        int8_t volume = 0;
        uint8_t feedback = 0;
        int8_t output = 0;
        uint16_t shifter = 1;
        uint8_t bakup = 0;
        uint8_t control = 0;
        uint8_t counter = 0;
        uint8_t other = 0;
        int accum = 0;
        bool linked() const { return (control & 0x07) == 0x07; }
        bool count_en() const { return (control & 0x08) != 0; }
        bool reload_en() const { return (control & 0x10) != 0; }
        bool done() const { return (other & 0x08) != 0; }
        int clock_sel() const { return control & 0x07; }
    };

    void update_irq();
    void signal_irq(int which);
    void count_down_linked(int which);
    void expire_timer(int which);
    uint8_t timer_read(int which, int offset) const;
    void timer_write(int which, int offset, uint8_t value);
    void clock_audio(int channel);
    uint8_t audio_read(int channel, int offset) const;
    void audio_write(int channel, int offset, uint8_t value);
    static int prescale(int clock_sel);

    std::array<uint8_t, 0x100> data_{};
    std::array<Timer, kTimerCount> timers_{};
    std::array<Audio, kAudioCount> audio_{};
    uint8_t interrupt_ = 0;
    uint16_t disp_addr_ = 0;
    bool vb_rest_ = false;

    IrqCallback irq_cb_;
    WakeCallback wake_cb_;
    SysctlCallback sysctl_cb_;
    ScanlineCallback scanline_cb_;
};

}  // namespace dsp
