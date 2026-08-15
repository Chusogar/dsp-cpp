#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/lr35902.h"
#include "machine/gb_mapper.h"
#include "sound/gb_apu.h"
#include "video/gb_ppu.h"

namespace dsp {

// Nintendo Game Boy / Game Boy Color, ported from gb.pas (+ lr35902.pas,
// gb_sound.pas, gb_mappers.pas, all folded into their own dsp-cpp-style chip
// classes: LR35902, GbApu, GbMapper, and the new GbPpu video chip).
//
// CGB is auto-detected from the cartridge header's $0143 byte, same as
// gb_change_model. Boot ROMs are optional (init() loads one if the given
// directory has dmg_boot.bin/cgb_boot.bin, matching common naming); without
// one the CPU/IO state is initialized directly to the documented post-boot
// values instead (reset_gb's `if not(rom_exist)` branch), which is what
// most users will actually hit since the boot ROM is copyrighted and not
// freely distributable, unlike every other console/computer ported so far.
class GameBoy : public Machine {
public:
    static constexpr uint32_t kClock = 4194304;
    static constexpr int kCyclesPerLine = 456;
    static constexpr int kScanlines = 154;
    static constexpr double kFramesPerSecond = double(kClock) / kCyclesPerLine / kScanlines;
    static constexpr int kMaxCartridge = 0x800000;  // 8 MiB, generous upper bound

    GameBoy();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return GbPpu::kScreenWidth; }
    int screen_height() const override { return GbPpu::kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return GbApu::kSampleRate; }

    const char* title() const override { return is_cgb_ ? "Game Boy Color" : "Game Boy"; }

    // Cartridge (.gb/.gbc, plain or zipped). Mirrors abrir_gb.
    bool load_media(const std::string& path, std::string* error) override;

    // Debugging aid, not part of the Machine interface.
    struct DebugState { uint16_t pc; uint16_t sp; bool halted; bool ime; uint8_t a; uint32_t irqs; int line; uint8_t lcdc; uint16_t hl; uint16_t de; uint16_t bc; };
    DebugState debug_state() const {
        return {cpu_.pc, cpu_.sp, cpu_.halted(), cpu_.ime, cpu_.a, cpu_.interrupts_serviced, line_, ppu_.lcdc(),
                uint16_t((cpu_.h<<8)|cpu_.l), uint16_t((cpu_.d<<8)|cpu_.e), uint16_t((cpu_.b<<8)|cpu_.c)};
    }
    uint8_t debug_read(uint16_t address) { return read_byte(address); }
    void debug_write(uint16_t address, uint8_t value) { write_byte(address, value); }
    void debug_set_fetch_hook(std::function<void(uint16_t)> hook) { cpu_.on_fetch = std::move(hook); }
    int debug_line() const { return line_; }
    int debug_speed() const { return cpu_.speed; }
    void debug_set_io_write_hook(std::function<void(uint8_t, uint8_t)> hook) { io_write_hook_ = std::move(hook); }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_io(uint8_t offset);
    void write_io(uint8_t offset, uint8_t value);
    void on_cpu_cycles(int cycles);
    void run_line_checkpoint_zero();
    void step_oam_dma(int cycles);
    void step_timer(int cycles);
    void step_div(int cycles);
    void hdma_block();
    void apply_post_boot_state();
    void save_cart_ram();
    void load_cart_ram();

    LR35902 cpu_;
    GbPpu ppu_;
    GbApu apu_;
    GbMapper mapper_;

    std::vector<uint8_t> dmg_boot_rom_;
    std::vector<uint8_t> cgb_boot_rom_;
    bool boot_rom_enabled_ = false;
    bool is_cgb_ = false;
    bool unlicensed_ = false;

    std::array<uint32_t, GbPpu::kScreenWidth * GbPpu::kScreenHeight> framebuffer_{};

    // Work RAM: 8 KiB on DMG (bank 0 fixed + bank 1 fixed), 32 KiB on CGB
    // (bank 0 fixed + banks 1-7 switchable via $ff70).
    std::array<std::array<uint8_t, 0x1000>, 8> wram_{};
    int wram_bank_ = 1;

    std::array<uint8_t, 0x100> io_ram_{};  // raw shadow of every I/O write, matches gb_0.io_ram[]

    // Timer.
    uint16_t div_counter_ = 0;  // internal 16-bit counter; DIV register is the high byte
    uint8_t tima_ = 0, tma_ = 0, tac_ = 0;
    int timer_cycles_ = 0;

    // LCD / STAT timing ("contador" in gb.pas resets every scanline; here
    // it's line_cycles_, advanced from on_cpu_cycles).
    uint8_t stat_ = 0;
    uint8_t ly_compare_ = 0;
    int line_ = 0;
    int line_cycles_ = 0;
    int sprites_time_ = 0;
    bool hdma_done_this_line_ = false;

    // OAM DMA ($ff46): the copy itself happens immediately (matches
    // gb.pas), oam_dma_remaining_ only tracks "still busy" bookkeeping.
    int oam_dma_remaining_ = 0;

    // CGB general purpose / HBlank VRAM DMA ($ff51-55).
    uint16_t dma_src_ = 0, dma_dst_ = 0;
    bool hdma_active_ = false;
    uint8_t hdma_size_ = 0xff;

    // Joypad.
    uint8_t joy_select_ = 0x30;
    uint8_t joy_val_ = 0xff;  // bits 0-3 direction (right,left,up,down), 4-7 buttons (a,b,select,start)
    uint8_t joystick_ = 0xff;

    uint64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
    std::function<void(uint8_t, uint8_t)> io_write_hook_;
};

}  // namespace dsp
