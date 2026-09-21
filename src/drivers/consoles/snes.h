#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/spc700.h"
#include "cpu/w65c816.h"
#include "video/snes_ppu.h"

namespace dsp {

// Nintendo Super Famicom / SNES.
//
// A 65C816 with 128 KB of work RAM, the S-PPU pair, and eight DMA channels
// that the software leans on heavily: almost everything that reaches VRAM
// gets there by DMA rather than by stores. The sound side is a second
// processor (SPC700 + DSP) behind four mailbox ports; this driver answers
// those ports with the boot handshake the IPL performs so that software gets
// past its upload loop, but does not yet run SPC700 code.
class Snes : public Machine {
public:
    static constexpr uint32_t kCpuClock = 3580000;
    static constexpr int kFps = 60;
    static constexpr int kLinesTotal = 262;
    static constexpr int kVisibleLines = SnesPpu::kHeight;
    static constexpr int kCyclesPerLine = 1364 / 6;   // master clock / 6
    static constexpr int kWidth = SnesPpu::kWidth;
    static constexpr int kHeight = SnesPpu::kHeight;
    static constexpr int kSampleRate = 32000;

    Snes();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int, uint8_t) override {}
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override { out.clear(); }
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "Super Nintendo"; }
    bool load_media(const std::string& path, std::string* error) override;

private:
    uint8_t cpu_read(uint32_t addr);
    void cpu_write(uint32_t addr, uint8_t value);
    uint8_t read_io(uint16_t addr);
    void write_io(uint16_t addr, uint8_t value);
    uint32_t map_rom(uint32_t addr) const;
    void run_dma(uint8_t channels);
    void run_hdma_init();
    void run_hdma_line();
    uint8_t apu_read(int port);
    void apu_write(int port, uint8_t value);
    uint8_t aram_read(uint16_t addr);
    void aram_write(uint16_t addr, uint8_t value);
    void tick_apu_timers(int cycles);
    // Runs the sound CPU for the share of its own clock that matches the
    // cycles the main CPU has just executed.
    void run_apu(int main_cycles);

    struct DmaChannel {
        uint8_t control = 0;     // direction, step, transfer pattern
        uint8_t dest = 0;        // $21xx register
        uint32_t src = 0;        // bank:address
        uint16_t count = 0;      // bytes, 0 means 65536
        // HDMA state
        uint16_t table = 0;
        uint8_t line_count = 0;
        bool repeat = false;
        bool done = false;
        bool pending = false;
        uint16_t indirect = 0;
        uint8_t indirect_bank = 0;   // $43x7
    };

    W65C816 cpu_;
    SnesPpu ppu_;

    std::vector<uint8_t> rom_;
    bool hirom_ = false;
    std::vector<uint8_t> sram_;
    std::array<uint8_t, 0x20000> wram_{};   // 128 KB at banks $7E-$7F
    uint32_t wram_addr_ = 0;                // $2181-$2183 port

    std::array<DmaChannel, 8> dma_{};
    uint8_t nmitimen_ = 0;
    uint8_t rdnmi_ = 0;
    uint8_t hdmaen_ = 0;
    uint8_t memsel_ = 0;
    uint8_t wrio_ = 0xff;   // $4201, echoed back by $4213
    uint16_t htime_ = 0x1ff, vtime_ = 0x1ff;
    bool irq_pending_ = false;    // $4211 TIMEUP, cleared when read
    bool nmi_pending_ = false;
    bool in_vblank_ = false;
    int line_ = 0;

    // Controller shift registers.
    uint16_t pad1_ = 0;
    uint16_t pad1_shift_ = 0;
    uint8_t joy_latch_ = 0;

    // Sound side: the SPC700 with its own RAM, the boot ROM that covers the
    // top 64 bytes until software switches it out, and the three timers.
    Spc700 apu_;
    std::array<uint8_t, 0x10000> aram_{};
    std::array<uint8_t, 64> ipl_{};
    bool ipl_visible_ = true;
    std::array<uint8_t, 4> apu_out_{};   // SPC700 -> main CPU  ($F4-$F7 write)
    std::array<uint8_t, 4> apu_in_{};    // main CPU -> SPC700  ($2140-$2143 write)
    uint8_t apu_control_ = 0;
    std::array<uint8_t, 3> timer_target_{};
    std::array<uint8_t, 3> timer_stage_{};
    std::array<uint8_t, 3> timer_out_{};
    std::array<int, 3> timer_div_{};
    std::array<uint8_t, 128> dsp_{};
    uint8_t dsp_addr_ = 0;
    int apu_cycles_ = 0;

    std::array<uint32_t, size_t(kWidth) * kHeight> framebuffer_{};
    uint8_t open_bus_ = 0;
};

}  // namespace dsp
