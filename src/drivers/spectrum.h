#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/tape_tzx.h"

namespace dsp {

// ZX Spectrum 48K (and 16K variant), ported from spectrum_48k.pas + spectrum_misc.pas.
// Independent of any pre-existing Spectrum driver in dsp-cpp.
// Chips: Z80 @ 3.5 MHz, ULA (border/beeper/keyboard/EAR), TapeTzx.
class Spectrum48k : public Machine {
public:
    static constexpr int kScreenWidth = 352;   // 48 + 256 + 48
    static constexpr int kScreenHeight = 296;  // 48 + 192 + 56
    static constexpr uint32_t kClock = 3500000;
    static constexpr int kTstatesPerLine = 224;
    static constexpr int kLinesPerFrame = 312;
    static constexpr int kTstatesPerFrame = kTstatesPerLine * kLinesPerFrame;  // 69888
    static constexpr double kFps = double(kClock) / double(kTstatesPerFrame);
    static constexpr int kSampleRate = 44100;

    enum class Model { Spec48k, Spec16k };

    explicit Spectrum48k(Model model = Model::Spec48k);

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
    const char* title() const override;
    bool uses_keyboard() const override { return true; }
    bool load_media(const std::string& path, std::string* error) override;

    bool load_tape(const std::string& path, std::string* error);
    void tape_play();
    void tape_stop();
    bool tape_playing() const { return tape_.is_playing(); }

    bool load_sna(const std::string& path, std::string* error);

	uint8_t mem_read(uint16_t addr);
    void mem_write(uint16_t addr, uint8_t value);
    uint8_t io_in(uint16_t port);
    void io_out(uint16_t port, uint8_t value);
    void on_cycles(int cycles);


private:
    
    void build_contention();
    void render_line(int line);
    void border_fill_to(int t_end);
    void border_on_out(uint8_t new_colour_index);
    uint8_t border_index() const;
    void apply_keyboard(const MachineInputs& in);

    Model model_;
    Z80 cpu_;
    TapeTzx tape_;

    std::array<uint8_t, 0x10000> mem_{};
    std::array<uint8_t, 0x4000> rom_{};
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};
    std::array<uint32_t, 16> palette_{};

    // ULA state
    uint8_t border_ = 7;
    uint8_t border_pos_ = 0;  // T-state within line when colour last changed
    // Per-T border colour index (ULA classic 0-15 or ULA+ 16-23)
    std::array<std::array<uint8_t, kTstatesPerLine>, kLinesPerFrame> border_buf_{};
    uint8_t speaker_ = 0;   // bit 4 of port $FE → 0x00 / 0x10
    uint8_t ear_ = 0;       // tape level 0x00 / 0x40
    std::array<uint8_t, 8> keys_{};
    uint8_t joy_ = 0x00;    // Kempston active-high bits
    bool flash_ = false;
    int flash_count_ = 0;

    // ULA+ (64-entry GRB palette, ports $BF3B / $FF3B)
    struct UlaPlus {
        bool enabled = true;   // hardware present
        bool active = false;   // software enable
        uint8_t mode = 0;      // 0 = palette data, 1 = mode/control
        uint8_t last_reg = 0;  // 0..63
        std::array<uint8_t, 64> pal{};  // raw GRB bytes
    } ulaplus_;
    // Runtime ARGB colours: [0..15] classic, [16..79] ULA+
    std::array<uint32_t, 80> palette_ext_{};
    void ulaplus_set_entry(uint8_t index, uint8_t value);
    uint32_t ulaplus_decode(uint8_t value) const;
    uint32_t border_colour() const;

    // Timing
    int line_ = 0;
    int t_in_line_ = 0;
    int frame_t_ = 0;
    std::array<uint8_t, 71000> contention_{};

    // Audio (beeper)
    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;
    int16_t beeper_level_ = 0;

    static constexpr uint16_t kScrTable[192] = {
        0,    256, 512, 768, 1024, 1280, 1536, 1792, 32,   288, 544, 800, 1056, 1312, 1568, 1824,
        64,   320, 576, 832, 1088, 1344, 1600, 1856, 96,   352, 608, 864, 1120, 1376, 1632, 1888,
        128,  384, 640, 896, 1152, 1408, 1664, 1920, 160,  416, 672, 928, 1184, 1440, 1696, 1952,
        192,  448, 704, 960, 1216, 1472, 1728, 1984, 224,  480, 736, 992, 1248, 1504, 1760, 2016,
        2048, 2304, 2560, 2816, 3072, 3328, 3584, 3840, 2080, 2336, 2592, 2848, 3104, 3360, 3616, 3872,
        2112, 2368, 2624, 2880, 3136, 3392, 3648, 3904, 2144, 2400, 2656, 2912, 3168, 3424, 3680, 3936,
        2176, 2432, 2688, 2944, 3200, 3456, 3712, 3968, 2208, 2464, 2720, 2976, 3232, 3488, 3744, 4000,
        2240, 2496, 2752, 3008, 3264, 3520, 3776, 4032, 2272, 2528, 2784, 3040, 3296, 3552, 3808, 4064,
        4096, 4352, 4608, 4864, 5120, 5376, 5632, 5888, 4128, 4384, 4640, 4896, 5152, 5408, 5664, 5920,
        4160, 4416, 4672, 4928, 5184, 5440, 5696, 5952, 4192, 4448, 4704, 4960, 5216, 5472, 5728, 5984,
        4224, 4480, 4736, 4992, 5248, 5504, 5760, 6016, 4256, 4512, 4768, 5024, 5280, 5536, 5792, 6048,
        4288, 4544, 4800, 5056, 5312, 5568, 5824, 6080, 4320, 4576, 4832, 5088, 5344, 5600, 5856, 6112};
};

}  // namespace dsp
