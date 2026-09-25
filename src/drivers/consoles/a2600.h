#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m6502.h"
#include "machine/mos6532.h"
#include "video/tia.h"

namespace dsp {

// Atari 2600 / VCS (1977). No BIOS: a cartridge maps into the 4 KiB window at
// $1000-$1FFF. The 6507 is a 6502 with a 13-bit address bus and no IRQ/NMI
// pins. Video and the two audio channels live in the TIA; 128 bytes of RAM,
// the console switches and the RIOT timer live in the MOS 6532.
class A2600 : public Machine {
public:
    static constexpr uint32_t kMasterClock = 3579545;
    static constexpr uint32_t kCpuClock = kMasterClock / 3;
    static constexpr int kCyclesPerLine = Tia::kCpuCyclesPerLine;
    static constexpr int kScanlines = Tia::kScanlines;
    static constexpr int kScreenWidth = Tia::kScreenWidth;
    static constexpr int kScreenHeight = 192;
    static constexpr double kFramesPerSecond =
        double(kCpuClock) / kCyclesPerLine / kScanlines;
    static constexpr size_t kMaxCartridge = 64 * 1024 + 256;

    A2600();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return Tia::kSampleRate; }

    const char* title() const override { return "Atari 2600"; }

    bool load_media(const std::string& path, std::string* error) override;

    uint16_t debug_pc() const { return cpu_.pc(); }
    int debug_bank() const { return bank_; }
    const char* mapper_name() const;

private:
    // Flat 2K/4K; Atari F8/F6/F4 (+Superchip RAM); Parker Bros E0;
    // M Network E7; Activision FE; Tigervision 3F; Atari F0 (Megaboy).
    enum class Mapper { Flat, F8, F6, F4, F0, E0, E7, FE, T3F };

    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    void on_cpu_cycles(int cycles);
    // Advance the TIA (and the frame/line bookkeeping) by colour clocks.
    void advance(int clocks);
    void end_line();
    void detect_mapper(const std::vector<uint8_t>& data, size_t size);

    uint8_t read_cartridge(uint16_t offset);
    void write_cartridge(uint16_t offset, uint8_t value);
    void touch_hotspot(uint16_t offset);
    bool install_cartridge(const std::vector<uint8_t>& data, std::string* error);
    static bool read_plain_or_zip(const std::string& path, std::vector<uint8_t>& data,
                                  std::string* error);
    static bool load_from_directory(const std::string& directory, std::vector<uint8_t>& data,
                                    std::string* error);

    uint8_t read_swcha() const;
    uint8_t read_swchb() const;

    M6502 cpu_;
    Tia tia_;
    Mos6532 riot_;

    std::vector<uint8_t> rom_;
    size_t rom_mask_ = 0;
    Mapper mapper_ = Mapper::Flat;
    int bank_ = 0;
    int bank_count_ = 1;
    bool superchip_ = false;
    std::array<uint8_t, 128> superchip_ram_{};
    // E0: three switchable 1K slices, the fourth fixed to the last 1K.
    std::array<int, 4> e0_slice_{};
    // E7: 2K ROM bank (or 1K RAM when bank 7) at $1000, 256-byte RAM page.
    int e7_ram_page_ = 0;
    std::array<uint8_t, 2048> e7_ram_{};
    // FE: bank chosen by the high byte following a stack access at $01FE.
    bool fe_pending_ = false;
    bool fe_jsr_ = false;

    // TIA register writes wait for the end of the CPU instruction: the
    // 6502 core reports an instruction's cycles only after it has executed.
    struct PendingWrite { uint8_t reg, value; };
    std::array<PendingWrite, 4> pending_{};
    int pending_count_ = 0;

    MachineInputs inputs_{};
    uint8_t dips_ = 0x08;  // colour, amateur difficulty

    int line_in_frame_ = 0;
    int first_visible_ = -1;
    int last_row_ = -1;
    bool prev_vsync_ = false;
    bool frame_done_ = false;
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};
    std::vector<int16_t> audio_;
};

}  // namespace dsp
