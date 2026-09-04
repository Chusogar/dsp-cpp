#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/m68000.h"
#include "machine/amiga_adf.h"
#include "machine/amiga_chipset.h"
#include "machine/cia8520.h"

namespace dsp {

// Commodore Amiga 500 (PAL OCS): 68000, Kickstart, Agnus/Denise/Paula, 8520 CIAs,
// 512 KiB chip RAM, ADF floppy (AmigaDOS MFM).
class Amiga500 : public Machine {
public:
    static constexpr uint32_t kCpuClock = 7093790;
    static constexpr int kSampleRate = 44100;
    static constexpr double kFps = 50.0;
    static constexpr int kLines = 313;
    static constexpr int kCyclesPerLine = 454;
    static constexpr uint32_t kChipSize = 0x80000;

    Amiga500();

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    bool load_media(const std::string& path, std::string* error) override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return AmigaChipset::kWidth; }
    int screen_height() const override { return AmigaChipset::kHeight; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "Commodore Amiga 500"; }
    bool uses_keyboard() const override { return true; }

    uint32_t debug_pc() const { return cpu_.pc(); }
    uint8_t peek(uint32_t address) const { return const_cast<Amiga500*>(this)->read_byte(address); }
    uint16_t color00() const { return chipset_.color00(); }
    uint16_t dmacon() const { return chipset_.dmacon(); }
    uint16_t intena() const { return chipset_.intena(); }
    bool overlay() const;
    Cia8520& ciaa() { return ciaa_; }
    Cia8520& ciab() { return ciab_; }
    uint16_t intreq() const { return chipset_.intreq(); }
    uint16_t bplcon0() const { return chipset_.bplcon0(); }
    uint32_t cop1lc() const { return chipset_.cop1lc(); }
    uint32_t cop2lc() const { return chipset_.cop2lc(); }
    uint32_t bplpt0() const { return chipset_.bplpt0(); }
    uint16_t color_reg(int i) const { return chipset_.color(i); }
    uint32_t sprpt0() const { return chipset_.sprpt0(); }
    uint16_t sprpos0() const { return chipset_.sprpos0(); }
    uint16_t sprctl0() const { return chipset_.sprctl0(); }
    bool floppy_loaded() const { return floppy_.loaded(); }
    int floppy_tracks() const { return floppy_.tracks(); }
    int floppy_spt() const { return floppy_.spt(); }

private:
    uint8_t read_byte(uint32_t address);
    void write_byte(uint32_t address, uint8_t value);
    uint16_t read_word(uint32_t address);
    void write_word(uint32_t address, uint16_t value);
    uint16_t chip_word(uint32_t address) const;
    void poke_chip_word(uint32_t address, uint16_t value);
    void on_cpu_cycles(int cycles);
    void update_ipl();
    void cia_b_floppy(uint8_t prb);
    uint8_t cia_a_pra_in() const;

    M68000 cpu_;
    Cia8520 ciaa_;
    Cia8520 ciab_;
    AmigaChipset chipset_;
    AmigaAdf floppy_;

    std::vector<uint8_t> chip_;
    std::vector<uint8_t> rom_;
    std::array<uint32_t, AmigaChipset::kWidth * AmigaChipset::kHeight> framebuffer_{};

    int cyl_ = 0;
    int side_ = 0;
    bool motor_ = false;
    bool selected_ = false;
    bool disk_changed_ = true;
    uint8_t prev_prb_ = 0xFF;
    int cia_acc_ = 0;
    int index_div_ = 0;
    int64_t audio_acc_ = 0;
    std::vector<int16_t> audio_;
};

}  // namespace dsp
