#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/arm7tdmi.h"
#include "sound/gba_apu.h"
#include "video/gba_ppu.h"

namespace dsp {

// Nintendo Game Boy Advance (AGB): ARM7TDMI at 16.78 MHz, 16 KiB BIOS,
// 256 KiB EWRAM, 32 KiB IWRAM, the LCD controller (GbaPpu), four DMA
// channels, four timers, the keypad and the GBA/PSG sound (GbaApu).
// Cartridges are ROM images (.gba, plain or zipped) with SRAM, Flash
// 64/128 KiB or EEPROM 512 B/8 KiB saves, kept in a .sav next to the ROM.
// The real BIOS (gba.bin) boots the cartridge after the Nintendo logo.
class Gba : public Machine, private ArmBus {
public:
    static constexpr uint32_t kClock = 16777216;
    static constexpr int kCyclesPerLine = 1232;
    static constexpr int kHDrawCycles = 960;
    static constexpr int kLines = 228;
    static constexpr int kVisibleLines = 160;
    static constexpr double kFps = double(kClock) / double(kCyclesPerLine * kLines);

    Gba();
    ~Gba() override;

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    bool load_media(const std::string& path, std::string* error) override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int, uint8_t) override {}
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return GbaPpu::kWidth; }
    int screen_height() const override { return GbaPpu::kHeight; }
    double frames_per_second() const override { return kFps; }
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return GbaApu::kSampleRate; }
    const char* title() const override { return "Nintendo Game Boy Advance"; }

    enum class SaveType { None, Sram, Flash64, Flash128, Eeprom };
    SaveType save_type() const { return save_type_; }
    const std::string& game_title() const { return game_title_; }
    bool cart_loaded() const { return !rom_.empty(); }
    uint32_t debug_pc() const { return cpu_.pc(); }
    bool debug_thumb() const { return cpu_.thumb(); }
    uint32_t debug_reg(int n) const { return cpu_.reg(n); }
    uint8_t peek8(uint32_t address) { return read8(address); }
    uint16_t peek16(uint32_t address) { return read16(address, false); }
    uint32_t peek32(uint32_t address) { return read32(address, false); }
    void poke8(uint32_t address, uint8_t value) { write8(address, value); }
    void poke16(uint32_t address, uint16_t value) { write16(address, value); }
    void poke32(uint32_t address, uint32_t value) { write32(address, value); }
    uint64_t cycles() const { return cycles_; }
    bool halted() const { return halted_; }
    // Saves the cartridge RAM now (also done on exit and every few seconds).
    void flush_save();

private:
    // ArmBus
    uint8_t read8(uint32_t address) override;
    uint16_t read16(uint32_t address, bool code) override;
    uint32_t read32(uint32_t address, bool code) override;
    void write8(uint32_t address, uint8_t value) override;
    void write16(uint32_t address, uint16_t value) override;
    void write32(uint32_t address, uint32_t value) override;
    void idle(int cycles) override { cycles_ += uint64_t(cycles); }

    int rom_wait(uint32_t address, int width, bool code);
    uint32_t open_bus(uint32_t address) const;

    uint16_t io_read16(uint32_t offset);
    void io_write16(uint32_t offset, uint16_t value, uint16_t mask);
    uint16_t io16(uint32_t offset) const { return uint16_t(io_[offset] | (io_[offset + 1] << 8)); }
    void set_io16(uint32_t offset, uint16_t value) {
        io_[offset] = uint8_t(value);
        io_[offset + 1] = uint8_t(value >> 8);
    }

    void raise_irq(int bit);
    void update_irq();
    void run_until(uint64_t target);
    void tick(uint64_t cycles);
    void tick_timers(uint64_t cycles);
    void timer_overflow(int t);
    uint64_t cycles_to_timer_event() const;
    void dma_write_control(int n, uint16_t value);
    void dma_trigger(int timing);
    void run_dma(int n);
    void check_keypad_irq();

    uint8_t save_read8(uint32_t address);
    void save_write8(uint32_t address, uint8_t value);
    bool is_eeprom(uint32_t address) const;
    uint16_t eeprom_read();
    void eeprom_write(uint16_t value);
    void detect_save_type();
    void load_save();

    Arm7tdmi cpu_;
    GbaPpu ppu_;
    GbaApu apu_;

    std::vector<uint8_t> bios_;
    std::vector<uint8_t> ewram_;
    std::vector<uint8_t> iwram_;
    std::array<uint8_t, 0x400> io_{};
    std::array<uint8_t, 0x400> pal_{};
    std::vector<uint8_t> vram_;
    std::array<uint8_t, 0x400> oam_{};
    std::vector<uint8_t> rom_;
    std::vector<uint8_t> save_;
    std::array<uint32_t, GbaPpu::kWidth * GbaPpu::kHeight> framebuffer_{};

    uint64_t cycles_ = 0;
    int line_ = 0;
    bool halted_ = false;
    uint16_t ie_ = 0, if_ = 0, ime_ = 0;
    uint16_t dispstat_ = 0;
    uint16_t keyinput_ = 0x3FF;
    uint32_t bios_latch_ = 0;
    uint32_t last_fetch_ = 0;
    uint32_t last_rom_address_ = 0xFFFFFFFF;

    struct Timer {
        uint16_t reload = 0;
        uint16_t control = 0;
        uint32_t counter = 0;
        uint32_t sub = 0;
    };
    std::array<Timer, 4> timers_{};

    struct Dma {
        uint32_t src = 0, dst = 0;
        uint32_t isrc = 0, idst = 0, icount = 0;
        uint16_t count = 0, control = 0;
    };
    std::array<Dma, 4> dma_{};
    bool dma_running_ = false;

    uint64_t audio_acc_ = 0;
    std::vector<int16_t> audio_;

    SaveType save_type_ = SaveType::None;
    std::string save_path_;
    std::string game_title_;
    bool save_dirty_ = false;
    int save_timer_ = 0;
    // Flash
    int flash_state_ = 0;
    bool flash_id_ = false, flash_erase_ = false, flash_write_ = false, flash_bank_cmd_ = false;
    int flash_bank_ = 0;
    // EEPROM
    int eeprom_addr_bits_ = 6;
    std::vector<uint8_t> eeprom_bits_;
    bool eeprom_reading_ = false;
    int eeprom_read_pos_ = 0;
    uint32_t eeprom_read_addr_ = 0;
};

}  // namespace dsp
