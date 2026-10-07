// Sony PlayStation (PSX) machine driver.
// Hardware devices ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/r3000a.h"
#include "machine/psx_cdrom.h"
#include "machine/psx_dma.h"
#include "machine/psx_gpu.h"
#include "machine/psx_irq.h"
#include "machine/psx_joypad.h"
#include "machine/psx_mdec.h"
#include "machine/psx_spu.h"
#include "machine/psx_timers.h"

namespace dsp {

class Psx : public Machine {
public:
    static constexpr uint32_t kCpuClock = 33868800;
    static constexpr int kCyclesPerFrame = int(kCpuClock / 60);
    static constexpr int kSyncCycles = 100;
    static constexpr int kMipsUnderclock = 3;
    static constexpr int kMaxFbW = 640;
    static constexpr int kMaxFbH = 480;

    Psx();
    ~Psx() override = default;

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    bool load_media(const std::string& path, std::string* error) override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int, uint8_t) override {}

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return screen_w_; }
    int screen_height() const override { return screen_h_; }
    double frames_per_second() const override { return 60.0; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return PsxSpu::kSampleRate; }
    const char* title() const override { return "Sony PlayStation"; }

    uint32_t debug_pc() const { return cpu_.pc(); }
    uint32_t debug_gpr(int n) const { return cpu_.gpr(n); }
    uint32_t debug_cop0_sr() const { return cpu_.cop0_sr(); }
    uint8_t debug_ram8(uint32_t addr) const {
        return ram_[addr & 0x1FFFFFu];
    }
    uint32_t debug_ram32(uint32_t addr) const {
        const uint32_t a = addr & 0x1FFFFCu;
        return uint32_t(ram_[a]) | (uint32_t(ram_[a + 1]) << 8) |
               (uint32_t(ram_[a + 2]) << 16) | (uint32_t(ram_[a + 3]) << 24);
    }
    // Returns true if "PS-X EXE" magic is present in RAM (EXE loaded).
    bool debug_exe_loaded() const {
        static const char kMagic[] = "PS-X EXE";
        for (size_t i = 0; i + 8 <= ram_.size(); i++) {
            if (std::memcmp(ram_.data() + i, kMagic, 8) == 0) return true;
        }
        // Also check common load addresses without full scan of header leftovers.
        const uint32_t addrs[] = {0x80010000u, 0x8000F800u, 0x80030000u, 0x00010000u, 0x0000F800u};
        for (uint32_t a : addrs) {
            bool ok = true;
            for (int i = 0; i < 8; i++) {
                if (ram_[(a + uint32_t(i)) & 0x1FFFFFu] != uint8_t(kMagic[i])) {
                    ok = false;
                    break;
                }
            }
            if (ok) return true;
        }
        return false;
    }
    int debug_cd_mode() const { return cdrom_.debug_mode(); }
    int debug_cd_read_loc() const { return cdrom_.debug_read_loc(); }
    uint8_t debug_cd_stat() const { return cdrom_.debug_stat(); }
    uint32_t debug_irq_status() const { return irq_.istat(); }
    uint32_t debug_irq_mask() const { return irq_.imask(); }

private:
    static uint32_t physical_addr(uint32_t address);

    uint8_t read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);
    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);

    uint32_t io_read32(uint32_t addr);
    void io_write32(uint32_t addr, uint32_t value);
    uint16_t io_read16(uint32_t addr);
    void io_write16(uint32_t addr, uint16_t value);
    uint8_t io_read8(uint32_t addr);
    void io_write8(uint32_t addr, uint8_t value);

    void tick_devices(int cycles);
    void wire_dma();
    bool load_bios(const std::string& rom_path, std::string* error);

    R3000A cpu_;
    PsxIrqController irq_;
    PsxDma dma_;
    PsxTimers timers_;
    PsxGpu gpu_;
    PsxCdrom cdrom_;
    PsxJoypad joypad_;
    PsxSpu spu_;
    PsxMdec mdec_;

    std::vector<uint8_t> ram_;          // 2 MiB
    std::array<uint8_t, 1024> scratch_{};
    std::vector<uint8_t> bios_;         // 512 KiB
    std::array<uint8_t, 0x40> mem_ctrl1_{};
    std::array<uint8_t, 0x10> mem_ctrl2_{};
    std::array<uint8_t, 0x10> sio_{};
    uint32_t cache_control_ = 0;

    std::array<uint32_t, kMaxFbW * kMaxFbH> framebuffer_{};
    int screen_w_ = 320;
    int screen_h_ = 240;
    std::vector<int16_t> audio_pending_;
};

}  // namespace dsp
