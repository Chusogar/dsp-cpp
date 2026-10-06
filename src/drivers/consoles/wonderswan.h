#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/nec_v30.h"

namespace dsp {

// Bandai WonderSwan / WonderSwan Color (ASWAN / SPHINX).
//
// NEC V30MZ @ 3.072 MHz (emulated with the existing NecV30 core), 224x144 LCD,
// 16 KiB (mono) or 64 KiB (color) shared IRAM, cartridge ROM banking via ports
// $C0-$C3, and SoC I/O for display / keypad / IRQ / EEPROM. Color mode is
// selected by the machine name (`wswan` vs `wscolor`) or auto-detected from a
// `.wsc` cartridge. Optional `boot.rom` from MAME's wswan.zip / wscolor.zip;
// without a BIOS the driver applies Mednafen-style post-boot I/O and starts at
// FFFF:0000 so the cartridge footer JMP runs.
class WonderSwan : public Machine {
public:
    static constexpr uint32_t kClock = 3072000;
    static constexpr int kScreenWidth = 224;
    static constexpr int kScreenHeight = 144;
    static constexpr int kCyclesPerLine = 256;
    static constexpr int kDefaultVtotal = 158;  // scanlines = vtotal + 1
    static constexpr int kSampleRate = 24000;
    static constexpr size_t kMaxCartridge = 16 * 1024 * 1024;

    enum class Model { WonderSwan, WonderSwanColor };

    explicit WonderSwan(Model model = Model::WonderSwanColor);

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override {
        return double(kClock) / kCyclesPerLine / (vtotal_ + 1);
    }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }

    const char* title() const override {
        return color_ ? "WonderSwan Color" : "WonderSwan";
    }

    bool load_media(const std::string& path, std::string* error) override;
    bool load_bios(const std::string& path, std::string* error);
    bool bios_loaded() const { return !boot_rom_.empty(); }

    uint32_t debug_pc() const { return cpu_.pc(); }
    bool debug_color() const { return color_; }
    uint8_t debug_read(uint32_t address) { return read_mem(address); }
    uint8_t debug_in(uint16_t port) { return read_io(port); }

private:
    struct Timer {
        bool enable = false;
        bool repeat = false;
        uint16_t frequency = 0;
        uint16_t counter = 0;
        bool step();
    };

    struct Layer {
        bool enable = false;
        uint8_t map_base = 0;
        uint8_t hscroll = 0;
        uint8_t vscroll = 0;
    };

    struct Window {
        bool enable = false;
        bool invert = false;
        uint8_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool inside(uint8_t x, uint8_t y) const;
        bool outside(uint8_t x, uint8_t y) const;
    };

    struct SpriteRegs {
        bool enable = false;
        uint8_t oam_base = 0;
        uint8_t first = 0;
        uint8_t count = 0;
        Window window;
    };

    uint8_t read_mem(uint32_t address);
    void write_mem(uint32_t address, uint8_t value);
    uint8_t read_io(uint16_t port);
    void write_io(uint16_t port, uint8_t value);
    uint8_t read_iram(uint16_t address) const;
    void write_iram(uint16_t address, uint8_t value);
    uint16_t read_iram16(uint16_t address) const;
    uint32_t read_iram32(uint16_t address) const;
    uint8_t read_cart_rom(uint32_t address) const;
    uint8_t read_cart_ram(uint32_t address) const;
    void write_cart_ram(uint32_t address, uint8_t value);

    void raise_irq(int irq);
    void lower_irq(int irq);
    void poll_irq();
    void acknowledge_irq(uint8_t mask);
    uint8_t read_keypad() const;

    void render_scanline(int y);
    uint8_t fetch_tile(uint16_t tile, uint8_t x, uint8_t y) const;
    uint16_t palette_color(uint8_t palette, uint8_t color) const;
    uint16_t backdrop_color(uint8_t color) const;
    bool opaque(uint8_t palette, uint8_t color) const;
    bool planar() const { return (disp_mode_ & 7) != 7; }
    bool packed() const { return (disp_mode_ & 7) == 7; }
    int depth() const { return ((disp_mode_ >> 1) & 3) != 3 ? 2 : 4; }
    bool grayscale() const { return ((disp_mode_ >> 2) & 1) == 0; }
    uint32_t rgb12_to_argb(uint16_t color) const;

    void apply_startio();
    void search_bios(const std::string& rom_path);
    bool load_cart_bytes(std::vector<uint8_t> data, std::string* error);
    void eeprom_write_ctrl(bool internal_eep, uint8_t value);
    uint8_t eeprom_read_status(bool internal_eep) const;

    NecV30 cpu_;
    Model model_;
    bool color_ = true;

    std::array<uint8_t, 0x10000> iram_{};
    std::vector<uint8_t> cart_rom_;
    std::vector<uint8_t> cart_ram_;
    std::vector<uint8_t> boot_rom_;
    std::vector<uint8_t> internal_eeprom_;
    std::vector<uint8_t> cart_eeprom_;

    // Cartridge banking (Mednafen BankSelector layout).
    uint8_t rom_bank2_ = 0xff;  // $C0 linear high nibble
    uint8_t sram_bank_ = 0xff;  // $C1
    uint8_t rom_bank0_ = 0xff;  // $C2 area $20000
    uint8_t rom_bank1_ = 0xff;  // $C3 area $30000

    bool cartridge_enable_ = false;
    bool cartridge_rom_width_ = true;
    bool cartridge_rom_wait_ = false;
    uint8_t disp_mode_ = 0;

    uint8_t irq_base_ = 0;
    uint8_t irq_enable_ = 0;
    uint8_t irq_status_ = 0;
    uint8_t nmi_control_ = 0;

    uint8_t keypad_matrix_ = 0;
    uint16_t buttons_ = 0;  // packed WS button state

    // Display
    Layer screen1_{}, screen2_{};
    SpriteRegs sprite_{};
    Window screen2_window_{};
    bool lcd_enable_ = true;
    bool lcd_contrast_ = false;
    uint8_t backdrop_ = 0;
    uint8_t lcd_icons_ = 0;
    uint8_t vtotal_ = kDefaultVtotal;
    uint8_t vsync_line_ = 155;
    uint8_t vcompare_ = 0xbb;
    int vcounter_ = 0;
    bool field_ = false;

    uint8_t mono_pool_[8]{};
    uint8_t mono_pal_[16][4]{};

    // Latched per scanline
    Layer screen1_latched_{}, screen2_latched_{};
    SpriteRegs sprite_latched_{};
    Window screen2_window_latched_{};
    bool lcd_enable_latched_ = true;
    bool lcd_contrast_latched_ = false;
    uint8_t backdrop_latched_ = 0;
    std::array<uint32_t, 128> oam_cache_{};
    int oam_count_ = 0;

    Timer htimer_{}, vtimer_{};

    // Color / general DMA ($40-$48)
    uint32_t dma_source_ = 0;
    uint16_t dma_dest_ = 0;
    uint16_t dma_length_ = 0;
    uint8_t dma_control_ = 0;
    void run_gdma();

    // Internal / cart EEPROM (minimal M93LCx6)
    uint16_t eep_data_[2]{};
    uint16_t eep_cmd_[2]{};
    uint8_t eep_ctrl_[2]{};
    bool eep_ready_[2]{true, true};
    bool eep_protect_ = false;

    // Sound regs (stubbed; silence output)
    std::array<uint8_t, 0x40> sound_io_{};

    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};
    std::vector<int16_t> audio_;
    uint64_t audio_accumulator_ = 0;

    std::array<uint8_t, 256> io_shadow_{};
};

}  // namespace dsp
