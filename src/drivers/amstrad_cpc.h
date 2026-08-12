#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/i8255.h"
#include "machine/nec765.h"
#include "machine/spectrum_tape.h"
#include "sound/ay8910.h"

namespace dsp {

// Amstrad CPC 464/664/6128, ported from amstrad_cpc.pas.
//
// Hardware: Z80 @ 4 MHz, a Motorola 6845 CRTC wired the Amstrad way (video
// RAM always reads the base 64K regardless of ROM/RAM banking), the Gate
// Array (pen/palette selection, ROM enable, mode switch, 300 Hz interrupt
// generator), an Intel 8255 PPI (AY-3-8910 bus, cassette/vsync status,
// keyboard row select), a single AY-3-8910 whose spare I/O port scans the
// keyboard matrix, and a uPD765 (NEC765) floppy disk controller reading
// standard/Extended .dsk images (664/6128 only, needs `amsdos.rom`).
//
// Scope of this port: the Dandanator cartridge, localized ROM variants and
// the CPC Plus/512K RAM expansions are not emulated (a real 464 has no disk
// drive either). CDT/TZX cassette images load through the same block player
// as the ZX Spectrum driver, with the CPC's 4 MHz clock compensated to the
// tape's 3.5 MHz timings like the original engine does.
class AmstradCpc : public Machine {
public:
    enum class Model { CPC464, CPC664, CPC6128 };

    static constexpr int kScreenWidth = 400;
    static constexpr int kScreenHeight = 312;
    static constexpr uint32_t kCpuClock = 4000000;
    static constexpr uint32_t kAyClock = 1000000;
    static constexpr int kCyclesPerFrame = 79872;  // 312 lines * 64 chars * 4 T-states
    static constexpr int kSampleRate = AY8910::kSampleRate;

    explicit AmstradCpc(Model model);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return double(kCpuClock) / kCyclesPerFrame; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override;
    bool uses_keyboard() const override { return true; }
    bool load_media(const std::string& path, std::string* error) override;
	bool load_sna(const std::string& path, std::string* error);

private:
    // CRTC 6845 state, tcpc_crt of amstrad_cpc.pas.
    struct Crtc {
        std::array<uint8_t, 18> regs{};
        uint8_t reg = 0;
        uint16_t char_total = 64 * 8;
        uint16_t pixel_visible = 48 * 8;
        uint16_t borde = (kScreenWidth - 48 * 8) / 2;
        uint16_t pant_x = 0;
        uint16_t pant_addr = 0;
        bool was_hsync = false;
        bool was_vsync = false;
        bool line_is_visible = false;
        uint8_t character_counter = 0;
        uint8_t hsync_counter = 0;
        uint8_t vsync_counter = 0;
        bool next_line_no_visible = false;
        bool next_line_is_visible = false;
        bool state_hsync = false;
        bool is_in_adjustment_period = false;
        bool state_vsync = false;
        uint8_t state_row_address = 0;
        uint8_t adj_count = 0;
        uint16_t end_of_line_address = 0;
        uint16_t state_refresh_address = 0;
        uint16_t line_address = 0;
        uint8_t char_crt = 0;
    };

    // Gate array state, tcpc_ga of amstrad_cpc.pas.
    struct GateArray {
        uint8_t pen = 0;
        uint8_t video_mode = 0;
        uint8_t nvideo = 0;
        std::array<uint8_t, 17> pal{};
        uint8_t lines_count = 0;
        uint8_t lines_sync = 0;
        uint8_t rom_selected = 0;
        bool rom_low = true;
        bool rom_high = true;
        bool change_video = false;
        std::array<uint8_t, 4> marco{0, 1, 2, 3};
        uint8_t marco_latch = 0;
    };

    // PPI 8255 latches, tcpc_ppi of amstrad_cpc.pas.
    struct Ppi {
		uint8_t port_a_read_latch = 0xff;
		uint8_t port_a_write_latch = 0;

		bool tape_motor = false;

		uint8_t ay_control = 0;
		uint8_t keyb_line = 0;

		uint8_t port_c = 0xff;      // <-- NUEVO

		std::array<uint8_t,16> keyb_val{};
	};

    uint8_t read_byte(uint16_t address) const;
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    void on_cycles(int cycles);

    // Gate array / CRTC / memory banker.
    void write_ga(uint8_t value);
    void write_ram_banking(uint8_t value);
    void write_crtc(uint8_t port, uint8_t value);
    uint8_t read_crtc(uint8_t port) const;
    void build_palette();

    // Video, one CRTC character clock (4 T-states) at a time.
    void advance_video(int cycles);
    void do_end_of_line();
    void adjust_vertical_total();
    void draw_pixels();
    void fill_line(int line, uint32_t color);

    // PPI ports.
    uint8_t port_a_read();
    uint8_t port_b_read() const;
	uint8_t port_c_read() const;

    void port_a_write(uint8_t value);
    void port_c_write(uint8_t value);
    void update_ay();
    uint8_t keyboard_scan();

    uint8_t& ram(int page, uint16_t offset);

    Model model_;
    int ram_pages_ = 4;

    Z80 cpu_;
    AY8910 ay_;
    I8255 ppi_;
    SpectrumTape tape_;
    Nec765Fdc fdc_;

    std::vector<uint8_t> ram_;
    std::vector<uint8_t> lower_rom_;
    std::array<std::vector<uint8_t>, 16> upper_roms_;
    std::array<bool, 16> rom_enabled_{};

    Crtc crt_;
    GateArray ga_;
    Ppi ppi_state_;
    bool mod_address_ = false;
    bool irq_asserted_ = false;
    bool iff1_before_ = false;
    int cpc_line_ = 0;
    bool color_monitor_ = true;
    uint8_t bright_ = 0;
    // Off by default: the shared SDL front end hard-codes joystick 1/2 to the
    // same scancodes as ordinary letters (space, R, F, D, G, A, S...), and the
    // real CPC wires both joystick ports into keyboard matrix rows 9 and 6, so
    // leaving this on would corrupt normal typing. Enable with `--dip 1:1` for
    // joystick-only games.
    bool joystick_enabled_ = false;

    std::array<uint32_t, 32> palette_{};
    std::vector<uint32_t> framebuffer_;

    int64_t tape_accumulator_ = 0;  // scales the 4 MHz clock to the tape's 3.5 MHz timings
    int64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
