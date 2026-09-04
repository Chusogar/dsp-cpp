#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "cpu/mcs48.h"
#include "machine/ql_mdv.h"
#include "machine/ql_win.h"
#include "machine/zx8302.h"
#include "video/zx8301.h"

namespace dsp {

// Sinclair QL (UK, JS SuperBASIC): 68008 + ZX8301/ZX8302 + 8749 IPC.
class SinclairQl : public Machine {
public:
    static constexpr uint32_t kCpuClock = 7500000;
    static constexpr uint32_t kIpcClock = 11000000;
    static constexpr double kFps = 50.08;
    static constexpr int kSampleRate = 44100;
    static constexpr int kWidth = Zx8301::kWidth;
    static constexpr int kHeight = Zx8301::kHeight;

    SinclairQl();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    bool load_media(const std::string& path, std::string* error) override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "Sinclair QL"; }
    bool uses_keyboard() const override { return true; }

    uint32_t debug_pc() const { return cpu_.pc(); }
    uint16_t debug_ipc_pc() const { return ipc_.pc(); }
    uint8_t debug_irq() const { return zx8302_.irq(); }
    uint8_t peek(uint32_t address) const { return const_cast<SinclairQl*>(this)->read_byte(address); }
    bool mdv1_loaded() const { return mdv1_.loaded(); }
    bool mdv2_loaded() const { return mdv2_.loaded(); }
    bool mdv1_selected() const { return mdv1_.selected(); }
    bool mdv2_selected() const { return mdv2_.selected(); }
    bool mdv1_motor() const { return mdv1_.motor(); }
    bool mdv2_motor() const { return mdv2_.motor(); }
    bool win_loaded() const { return win_.loaded(); }
    size_t win_file_count() const { return win_.files().size(); }

private:
    uint8_t read_byte(uint32_t address);
    void write_byte(uint32_t address, uint8_t value);
    uint16_t read_word(uint32_t address);
    void write_word(uint32_t address, uint16_t value);
    void on_cpu_cycles(int cycles);
    void run_ipc(int cycles);
    void update_cpu_irqs();
    void apply_keyboard(const MachineInputs& inputs);
    uint8_t keyboard_rows() const;
    void ipc_port_out(uint16_t port, uint8_t value);
    uint8_t ipc_port_in(uint16_t port) const;
    void update_mdv_gap();
    void tick_mdv_bits();
    void win_trap(uint16_t cmd);
    void win_open();
    void win_io();
    void set_d0(int err);
    uint32_t chan_pos();
    void set_chan_pos(uint32_t pos);
    const QlWinFile* chan_file();
    std::string chan_name();
    void copy_to_guest(uint32_t dest, const uint8_t* src, uint32_t n);

    M68000 cpu_;
    Mcs48 ipc_;
    Zx8301 video_;
    Zx8302 zx8302_;
    QlMicrodrive mdv1_;
    QlMicrodrive mdv2_;
    QlWin win_;

    std::array<uint8_t, 0x10000> rom_{};
    std::array<uint32_t, kWidth * kHeight> framebuffer_{};
    std::array<uint8_t, 8> keys_{};
    MachineInputs inputs_{};

    int comdata_to_ipc_ = 1;
    int ipc_ipl_ = 3;
    int zx8302_irq2_ = 0;
    int baudx4_ = 0;
    int speaker_ = 0;
    uint8_t keylatch_ = 0;
    int64_t ipc_cycle_acc_ = 0;
    int64_t baud_acc_ = 0;
    int64_t mdv_acc_ = 0;
    int mdv_stall_ = 0;
    int rtc_frames_ = 0;
    int flash_frames_ = 0;
    int64_t audio_acc_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
