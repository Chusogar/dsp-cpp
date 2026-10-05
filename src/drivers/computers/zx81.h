#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"

namespace dsp {

// Sinclair ZX81 / Timex TS1000 (PAL).
// Z80 @ 3.25 MHz, 8 KiB ROM, 16 KiB RAM, no sound hardware.
// Video is driven by the ROM display file via opcode fetches + NMI/INT timing;
// the framebuffer is also filled from D_FILE each frame as a reliable fallback.
class Zx81 : public Machine {
public:
    static constexpr uint32_t kClock = 3250000;
    static constexpr int kTstatesPerLine = 207;
    static constexpr int kLinesPerFrame = 311;
    static constexpr int kTstatesPerFrame = kTstatesPerLine * kLinesPerFrame;  // 64377
    static constexpr double kFps = double(kClock) / double(kTstatesPerFrame);
    static constexpr int kScreenWidth = 384;
    static constexpr int kScreenHeight = 311;
    static constexpr int kPaperX = 64;   // left border
    static constexpr int kPaperY = 56;   // top border
    static constexpr int kPaperW = 256;
    static constexpr int kPaperH = 192;
    static constexpr int kSampleRate = 44100;

    Zx81();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "Sinclair ZX81"; }
    bool uses_keyboard() const override { return true; }

    bool load_media(const std::string& path, std::string* error) override;

    uint16_t debug_pc() const { return cpu_.pc(); }
    uint8_t debug_read(uint16_t addr) { return mem_read(addr); }
    bool media_pending() const { return !pending_p_.empty(); }

private:
    // Frames to wait after boot before injecting a .p snapshot.
    static constexpr int kPInjectFrames = 100;

    uint8_t mem_read(uint16_t addr);
    void mem_write(uint16_t addr, uint8_t value);
    uint8_t opcode_read(uint16_t addr);
    uint8_t io_in(uint16_t port);
    void io_out(uint16_t port, uint8_t value);
    void on_cycles(int cycles);
    void apply_keyboard(const MachineInputs& in);
    void render_dfile();
    void plot_char_row(int x, int y, uint8_t ch, int row);
    bool load_roms(const std::string& path, std::string* error);
    bool queue_p(const std::vector<uint8_t>& data, std::string* error);
    void update_pending_p();
    void inject_p(const std::vector<uint8_t>& data);

    Z80 cpu_;

    std::array<uint8_t, 0x2000> rom_{};
    std::array<uint8_t, 0x4000> ram_{};
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};
    std::array<uint8_t, 8> keys_{};

    // ULA / video
    uint8_t prev_refresh_ = 0xff;
    uint16_t ula_char_ = 0xffff;
    bool nmi_generator_ = false;
    bool nmi_on_ = false;
    bool vsync_active_ = false;
    int line_t_ = 0;
    int scanline_ = 0;
    int frame_t_ = 0;
    uint64_t total_cycles_ = 0;

    std::vector<uint8_t> pending_p_;
    int boot_frames_ = 0;

    static constexpr uint32_t kBlack = 0xff000000u;
    static constexpr uint32_t kWhite = 0xffffffffu;
};

}  // namespace dsp
