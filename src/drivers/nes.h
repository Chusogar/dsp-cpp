#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/irq_line.h"
#include "cpu/m6502.h"
#include "machine/nes_mapper.h"
#include "sound/nes_apu.h"
#include "video/nes_ppu.h"

namespace dsp {

// Nintendo Entertainment System (NTSC), ported from nes.pas, nes_ppu.pas,
// nes_mappers.pas and n2a03.pas. Same split as the Game Boy port: the
// console driver owns the memory map and frame loop, the PPU/APU/mapper
// live in video/sound/machine.
class Nes : public Machine {
public:
    static constexpr uint32_t kClock = NesApu::kClock;
    static constexpr int kScanlines = NesPpu::kScanlines;
    static constexpr double kFramesPerSecond = NesPpu::kFramesPerSecond;
    static constexpr int kMaxCartridge = 0x80000;  // 32 * 16 KiB PRG

    Nes();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return NesPpu::kScreenWidth; }
    int screen_height() const override { return NesPpu::kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return NesApu::kSampleRate; }

    const char* title() const override { return "NES"; }

    bool load_media(const std::string& path, std::string* error) override;
    bool load_ines(const std::vector<uint8_t>& data, std::string* error);

    uint8_t debug_read(uint16_t address) { return read_byte(address); }
    void debug_write(uint16_t address, uint8_t value) { write_byte(address, value); }
    uint16_t debug_pc() const { return cpu_.pc(); }
    int debug_mapper() const { return mapper_.mapper; }
    uint8_t debug_ppu_status() const { return ppu_.status; }

private:
    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    void on_cpu_cycles(int cycles);
    void run_cpu(double cycles);
    void apply_crc_patches(uint32_t crc, uint32_t chr_crc, int& mapper, int& submapper);

    M6502 cpu_;
    NesPpu ppu_;
    NesApu apu_;
    NesMapper mapper_;

    std::array<uint8_t, 0x10000> memory_{};
    std::array<uint32_t, NesPpu::kScreenWidth * NesPpu::kScreenHeight> framebuffer_{};

    uint8_t joy1_ = 0, joy2_ = 0;
    uint8_t joy1_read_ = 0, joy2_read_ = 0;
    bool val_4016_ = false;
    bool even_frame_ = true;
    bool mapper_irq_ = false;
    uint64_t cycle_count_ = 0;
    uint64_t apu_cycles_ = 0;
    uint64_t frame_irq_cycles_ = 0;
    uint64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
