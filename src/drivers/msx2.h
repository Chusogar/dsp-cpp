#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "sound/ay8910.h"
#include "video/v9938.h"

namespace dsp {

// MSX2 (1985), ported from zxtiny `zxm/msx2.c`. Panasonic-style layout:
// expanded slot 0 (main BIOS + sub ROM), cartridge in slot 1, expanded slot 3
// (256 KB mapper RAM + optional disk ROM). V9938, AY-3-8910, RP-5C01 RTC,
// and MSX-DOS disk I/O via Disk-ROM BIOS traps (DSKIO/DSKCHG/GETDPB) on a
// raw 512-byte-sector `.dsk` image.
class Msx2 : public Machine {
public:
    static constexpr uint32_t kMainClock = 3579545;
    static constexpr int kCyclesPerLine = 228;
    static constexpr int kScanlines = 313;
    static constexpr double kFramesPerSecond =
        double(kMainClock) / kCyclesPerLine / kScanlines;
    static constexpr int kRamPages = 16;
    static constexpr int kMaxCartridge = 1024 * 1024;

    Msx2();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return vdp_.framebuffer(); }
    int screen_width() const override { return V9938::kScreenWidth; }
    int screen_height() const override { return V9938::kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return AY8910::kSampleRate; }

    const char* title() const override { return "MSX2"; }
    bool uses_keyboard() const override { return true; }
    bool load_media(const std::string& path, std::string* error) override;

    uint16_t debug_pc() const { return z80_.pc(); }
    bool debug_has_diskrom() const { return has_diskrom_; }
    bool debug_disk_loaded() const { return !dsk_.empty(); }
    uint8_t debug_slot() const { return primary_sel_; }
    uint8_t debug_mapper(int page) const { return mapper_reg_[size_t(page)]; }
    uint8_t debug_vdp_reg(int index) const { return vdp_.reg(index); }
    int debug_vdp_mode() const { return vdp_.screen_mode(); }

private:
    enum class Mapper { None, Ascii8, Ascii16, Konami, KonamiScc };

    uint8_t read_byte(uint16_t address);
    void write_byte(uint16_t address, uint8_t value);
    uint8_t read_port(uint16_t port);
    void write_port(uint16_t port, uint8_t value);
    void on_main_cycles(int cycles);
    void on_instruction(uint16_t pc);
    void update_pages();
    uint8_t megarom_read(uint16_t address) const;
    void megarom_write(uint16_t address, uint8_t value);
    uint8_t psg_port_a() const;
    uint8_t rtc_read() const;
    void bios_ret();
    bool trap_bios(uint16_t pc);
    void trap_dskio();
    void trap_getdpb();
    bool load_bios(const std::string& path, std::string* error);
    bool load_cartridge(const std::string& path, std::string* error);
    bool load_cas(const std::string& path, std::string* error);
    bool load_dsk(const std::string& path, std::string* error);
    static Mapper detect_mapper(const uint8_t* data, uint32_t size);

    Z80 z80_;
    V9938 vdp_;
    AY8910 ay8910_;

    std::array<uint8_t, kRamPages * 0x4000> ram_{};
    std::array<uint8_t, 0x8000> bios_{};
    std::array<uint8_t, 0x4000> subrom_{};
    std::array<uint8_t, 0x4000> diskrom_{};
    bool has_diskrom_ = false;

    std::vector<uint8_t> cart_;
    Mapper cart_mapper_ = Mapper::None;
    std::array<uint8_t, 4> cart_bank_{};

    uint8_t primary_sel_ = 0;
    std::array<uint8_t, 4> secondary_sel_{};
    std::array<bool, 4> expanded_{};
    std::array<uint8_t, 4> mapper_reg_{3, 2, 1, 0};
    std::array<uint8_t*, 4> rd_{};
    std::array<uint8_t*, 4> wr_{};

    uint8_t ppi_c_ = 0;
    std::array<uint8_t, 11> keyboard_{};
    uint8_t joy1_ = 0x3f;
    uint8_t rtc_addr_ = 0;
    uint8_t rtc_mode_ = 0;
    uint8_t rtc_ram_[4][13]{};

    std::vector<uint8_t> cas_;
    uint32_t cas_pos_ = 0;
    std::vector<uint8_t> dsk_;

    uint64_t audio_accumulator_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
