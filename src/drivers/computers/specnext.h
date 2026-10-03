#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/z80.h"
#include "machine/sdcard_spi.h"
#include "machine/zxn_dma.h"
#include "sound/ay8910.h"

namespace dsp {

// ZX Spectrum Next (TBBlue core 3.02, "Emulators" machine id 8, 2 MB RAM).
//
// Z80N at 3.5/7/14/28 MHz, 2 MB SRAM in 8 KB pages behind the MMU (with
// the 128K/+3/Pentagon port paging translated onto it), the boot ROM, config
// mode, DivMMC automap with its own ROM/RAM pages, the Next registers
// ($243B/$253B and NEXTREG), the ULA (Timex modes, ULANext, ULA+), LoRes,
// Layer 2 (256x192, 320x256, 640x256), the 40/80 column tilemap, 128
// hardware sprites with anchors/relatives, the copper, the zxnDMA, the CTC,
// the hardware IM2 interrupt controller (ULA, line and CTC sources), three
// AY-3-8912 (TurboSound), four 8-bit DACs, the beeper and an SD card in
// SPI mode.
//
// Booting needs the boot ROM (boot-30204.bin from the tbblue distribution,
// GPL) and an SD card image with the system: the boot ROM loads TBBLUE.FW
// from the card, the firmware loads the NextZXOS ROMs into SRAM and starts
// the machine, exactly as on the real board. A .nex program can also be
// given; it is loaded into memory once NextZXOS has started.
//
// The picture is the 320x256 VGA area (paper + 32 pixel border) drawn at
// 640 pixels across so that the hi-res modes keep every pixel.
class SpecNext : public Machine {
public:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 256;
    static constexpr uint32_t kMasterClock = 28000000;  // 28 MHz video/system clock
    static constexpr int kSampleRate = AY8910::kSampleRate;
    static constexpr int kRamPages = 256;               // 2 MB of 8 KB pages

    SpecNext();
    ~SpecNext() override;

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    bool load_media(const std::string& path, std::string* error) override;
    void run_frame() override;
    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int, uint8_t) override {}
    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kWidth; }
    int screen_height() const override { return kHeight; }
    int display_width() const override { return kWidth; }
    int display_height() const override { return kHeight * 2; }
    double frames_per_second() const override;
    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return kSampleRate; }
    const char* title() const override { return "ZX Spectrum Next"; }
    bool uses_keyboard() const override { return true; }

    // Opens an SD card image (.img/.mmc/.hdf). `read_only` keeps writes in
    // memory (tests); otherwise they go to the image like on a real card.
    bool insert_sd(const std::string& path, bool read_only, std::string* error);
    bool sd_inserted() const { return sd_.inserted(); }
    // Loads a .nex program now (normally done automatically after boot).
    bool load_nex(const std::vector<uint8_t>& data, std::string* error);
    bool nex_pending() const { return !pending_nex_.empty(); }

    // Introspection for tests and debugging.
    Z80& cpu() { return cpu_; }
    uint8_t peek(uint16_t address) { return mem_read(address); }
    void poke(uint16_t address, uint8_t value) { mem_write(address, value); }
    uint8_t nextreg_read(uint8_t reg) { return reg_r(reg); }
    void nextreg_write(uint8_t reg, uint8_t value) { reg_w(reg, value); }
    uint8_t io_read(uint16_t port) { return io_in(port); }
    void io_write(uint16_t port, uint8_t value) { io_out(port, value); }
    uint8_t sram(uint32_t address) const { return sram_[address & (kRamPages * 0x2000 - 1)]; }
    void sram_write(uint32_t address, uint8_t value) { sram_[address & (kRamPages * 0x2000 - 1)] = value; }
    bool bootrom_enabled() const { return bootrom_en_; }
    bool config_mode() const { return nr_03_config_mode_; }
    uint8_t machine_type() const { return nr_03_machine_type_; }
    uint8_t mmu(int slot) const { return mmu_[size_t(slot & 7)]; }
    uint64_t frame_count() const { return frame_count_; }
    uint32_t sd_sectors_read() const { return sd_.sectors_read(); }
    int frames_since_os_start() const { return os_frames_; }
    void hard_reset();
    void soft_reset();

private:
    struct VideoTimings {
        int int_h, min_hactive, max_hc;
        int int_v, min_vactive, max_vc;
    };

    // --- memory ---------------------------------------------------------
    uint8_t mem_read(uint16_t address) {
        const uint8_t* p = rd_[address >> 13];
        return p ? p[address & 0x1fff] : 0xff;
    }
    void mem_write(uint16_t address, uint8_t value) {
        uint8_t* p = wr_[address >> 13];
        if (p) p[address & 0x1fff] = value;
    }
    uint8_t opcode_read(uint16_t address);
    uint8_t do_m1(uint16_t address);
    void bank_update(int bank);
    void bank_update_range(int bank, int count) {
        for (int i = 0; i < count; ++i) bank_update(bank + i);
    }
    void memory_change(uint16_t port, uint8_t data);
    void mmu_w(int bank, uint8_t data);
    void mmu_x2_w(int bank, uint8_t data);

    // --- I/O --------------------------------------------------------------
    uint8_t io_in(uint16_t port);
    void io_out(uint16_t port, uint8_t value);
    uint8_t ula_read(uint16_t port);
    uint8_t kempston_read(int port_37) const;
    void turbosound_address_w(uint8_t data);
    void port_7ffd_w(uint8_t data);
    void port_e3_w(uint8_t data);
    void port_e7_w(uint8_t data);
    void port_ff_w(uint8_t data);
    void dac_w(int mask, uint8_t value);
    uint32_t internal_port_enable() const;
    bool port_en(int bit) const { return (internal_port_enable() >> bit) & 1; }

    // --- Next registers ----------------------------------------------------
    uint8_t reg_r(uint8_t reg);
    void reg_w(uint8_t reg, uint8_t value);
    void nr_02_w(uint8_t value);
    void nr_07_w(uint8_t value);
    void palette_write(uint8_t priority, uint16_t value9);
    uint16_t palette_read() const;
    void sprite_mirror_w(uint8_t value);
    void sprite_io_w(uint16_t port, uint8_t value);

    // --- machine state ------------------------------------------------------
    void reset_hard_registers();
    void machine_reset();
    void update_video_mode();
    bool machine_type_48() const { return nr_03_machine_type_ == 0 || nr_03_machine_type_ == 1; }
    bool machine_type_128() const { return nr_03_machine_type_ == 2 || nr_03_machine_type_ == 4; }
    bool machine_type_p3() const { return !machine_type_48() && !machine_type_128(); }
    int irq_pulse_cycles() const;
    uint16_t port_7ffd_bank() const;
    bool port_7ffd_locked() const;
    bool mapping_pentagon() const { return nr_8f_mapping_mode_ == 2 || pentagon_1024_en(); }
    bool pentagon_1024_en() const { return nr_8f_mapping_mode_ == 3 && !(port_eff7_ & 4); }

    // --- timing / events ----------------------------------------------------
    void on_cycles(int t_states);
    void advance(int ticks);
    void line_event(int vc);
    void frame_start();
    int vpos_to_cvc(int vpos) const;
    int cvc_to_vpos(int cvc) const;
    int ticks_per_t() const { return 8 >> nr_07_cpu_speed_; }

    // --- interrupts -----------------------------------------------------------
    enum IntSource { kIntLine = 0, kIntCtc0 = 3, kIntUla = 11, kIntCount = 16 };
    void int_raise(int source);
    void int_clear(int source);
    void update_irq();
    void irq_acknowledge();
    void irq_reti();
    uint8_t int_vector(int source) const;

    // --- CTC ------------------------------------------------------------------
    struct CtcChannel {
        uint8_t control = 0x03;
        uint8_t tconst = 0;
        uint16_t counter = 256;
        int prescale_acc = 0;
        bool waiting_tc = false;
        bool running = false;
        bool waiting_trigger = false;
    };
    void ctc_write(int ch, uint8_t value);
    uint8_t ctc_read(int ch) const;
    void ctc_tick(int ticks);
    void ctc_count(int ch);

    // --- copper ---------------------------------------------------------------
    void copper_mode_w(uint8_t mode);
    void copper_run(int ticks);

    // --- DivMMC ---------------------------------------------------------------
    struct DivMmc {
        uint8_t reg = 0;
        int cpu_a_15_13 = 0;
        bool mreq_n = false, m1_n = false, en = true;
        bool automap_reset = false, automap_active = false, automap_rom3_active = false;
        bool retn_seen = false, button = false;
        bool instant_on = false, delayed_on = false, delayed_off = false;
        bool rom3_instant_on = false, rom3_delayed_on = false;
        bool nmi_instant_on = false, nmi_delayed_on = false;
        bool button_nmi = false, hold = false, held = false;

        bool conmem() const { return reg & 0x80; }
        bool mapram() const { return reg & 0x40; }
        bool page0() const { return cpu_a_15_13 == 0; }
        bool page1() const { return cpu_a_15_13 == 1; }
        bool automap() const {
            return !automap_reset &&
                   (held || (automap_active && (instant_on || (nmi_instant_on && button_nmi))) ||
                    (automap_rom3_active && rom3_instant_on));
        }
        bool rom_en() const { return en && page0() && (conmem() || automap()) && !mapram(); }
        bool ram_en() const {
            return en && ((page0() && (conmem() || automap()) && mapram()) ||
                          (page1() && (conmem() || automap())));
        }
        int ram_bank() const { return page0() ? 3 : (reg & 0x0f); }
        bool rdonly() const { return page0() || (mapram() && ram_bank() == 3); }
        void clock();
        void reset_lines();
    };

    // --- Multiface (NextZXOS NMI menu ROM in SRAM pages 0x0A/0x0B) -------------
    struct Multiface {
        bool a_0066 = false, mreq_n = false, m1_n = true, retn_seen = false;
        bool enable = true, button = false;
        uint8_t mode = 0;  // nr_0A bits 7-6: 00 = +3, 11 = 48K, else 128K
        bool en_rd = false, en_wr = false, dis_rd = false, dis_wr = false;
        bool nmi_active = false, invisible = true, mf_enable = false;

        bool mode_48() const { return mode == 3; }
        bool mode_p3() const { return mode == 0; }
        bool mode_128() const { return !mode_p3() && !mode_48(); }
        bool button_pulse() const { return button && !nmi_active; }
        bool invisible_eff() const { return invisible && !mode_48(); }
        bool fetch_66() const { return a_0066 && !m1_n && nmi_active; }
        bool enabled() const { return enable && (mf_enable || fetch_66()); }
        bool port_en() const { return enable && en_rd && !invisible_eff() && (mode_128() || mode_p3()); }
        void clock();
    };
    uint8_t mf_port_read(uint16_t port, uint8_t lsb);
    void mf_port_write(uint8_t lsb);
    void nmi_request();
    void leave_nmi();

    // --- video ----------------------------------------------------------------
    void render_line(int out_row, int vc);
    void render_ula(int row, uint16_t* out);
    void render_lores(int row, uint16_t* out);
    void render_tilemap(int row, uint16_t* out);
    void render_layer2(int row, uint16_t* out);
    void render_sprites(int row, uint16_t* out);
    void build_sprite_cache();
    uint32_t rgb9_to_argb(uint16_t c) const { return rgb_lut_[c & 0x1ff]; }
    const uint8_t* bank5() const { return &sram_[0x40000 + 5 * 0x4000]; }
    const uint8_t* bank7() const { return &sram_[0x40000 + 7 * 0x4000]; }
    bool global_transparent(uint16_t rgb9) const { return (rgb9 >> 1) == nr_14_global_transparent_; }

    // --- input / audio --------------------------------------------------------
    void apply_inputs();
    void mix_audio(int ticks);
    void check_os_started();

    // Hardware
    Z80 cpu_;
    std::array<std::unique_ptr<AY8910>, 3> ay_;
    ZxnDma dma_;
    SdCardSpi sd_;

    std::vector<uint8_t> sram_;
    std::vector<uint8_t> boot_rom_;
    std::array<const uint8_t*, 8> rd_{};
    std::array<uint8_t*, 8> wr_{};
    std::array<uint8_t, 8> mmu_{};
    std::array<int, 8> page_shadow_{};

    DivMmc divmmc_;
    bool divmmc_delayed_check_ = false;
    Multiface mf_;
    bool mf_button_ = false, drive_button_ = false;          // NMI buttons held
    bool nr_02_mf_nmi_ = false, nr_02_divmmc_nmi_ = false;  // software NMIs

    std::array<uint32_t, kWidth * kHeight> framebuffer_{};
    std::array<uint32_t, 512> rgb_lut_{};

    // Palettes: [0] ULA 1st, [1] L2 1st, [2] sprites 1st, [3] tilemap 1st,
    // [4..7] the second palettes. 9-bit RGB333, bit 15 = Layer 2 priority.
    std::array<std::array<uint16_t, 256>, 8> palette_{};

    // Sprites
    std::array<uint8_t, 0x4000> sprite_pattern_{};
    std::array<uint8_t, 128 * 8> sprite_attr_{};
    uint16_t sprite_pattern_index_ = 0;
    uint16_t sprite_attr_index_ = 0;
    uint16_t sprite_mirror_q_ = 0;
    uint8_t sprite_mirror_index_ = 7;
    bool sprite_mirror_inc_ = false;
    struct SpriteData {
        int x, y;
        bool rotate, xmirror, ymirror, h, rel_type;
        uint8_t paloff, pattern, xscale, yscale;
    };
    std::vector<SpriteData> sprite_cache_;
    bool sprite_cache_valid_ = false;

    // Copper
    std::array<uint8_t, 0x800> copper_ram_{};
    uint16_t copper_pc_ = 0;
    uint8_t copper_mode_ = 0;
    int copper_budget_ = 0;
    bool copper_frame_pending_ = false;

    // CTC
    std::array<CtcChannel, 4> ctc_{};

    // Interrupt controller
    std::array<bool, kIntCount> int_pending_{};
    std::array<bool, kIntCount> int_service_{};
    int pulse_left_ = 0;
    bool irq_asserted_ = false;

    // Timing
    VideoTimings vt_{};
    int line_ticks_ = 0;
    int frame_ticks_ = 0;
    int frame_tick_ = 0;
    int render_vc_ = 0;          // next line whose render point is pending
    bool ula_int_done_ = false;
    bool frame_done_ = false;
    uint64_t frame_count_ = 0;
    uint8_t eff_machine_timing_ = 3;
    bool eff_5060_ = false;
    int flash_counter_ = 0;

    // Ports
    uint8_t port_fe_ = 0;
    uint8_t port_ff_ = 0;
    uint8_t port_7ffd_ = 0;
    uint8_t port_1ffd_ = 0;
    uint8_t port_dffd_ = 0;
    uint8_t port_eff7_ = 0;
    uint8_t port_e3_ = 0;
    uint8_t port_e7_ = 0xff;
    bool port_1ffd_special_old_ = false;
    uint8_t nr_register_ = 0x24;
    uint8_t ay_select_ = 0;
    uint8_t ay_address_[3] = {};
    bool layer2_en_ = false, layer2_map_wr_ = false, layer2_map_rd_ = false, layer2_map_shadow_ = false;
    uint8_t layer2_map_segment_ = 0, layer2_offset_ = 0;
    uint8_t ulap_mode_ = 0, ulap_index_ = 0;
    bool ulap_en_ = false;
    uint8_t spi_miso_ = 0xff;

    // Next registers (decoded)
    bool bootrom_en_ = true;
    bool hard_reset_pending_ = true;
    uint8_t nr_02_reset_type_ = 4;
    uint8_t nr_03_machine_type_ = 3, nr_03_machine_timing_ = 3;
    bool nr_03_user_dt_lock_ = false, nr_03_config_mode_ = true;
    uint8_t nr_04_romram_bank_ = 0;
    uint8_t nr_05_ = 0x40;
    uint8_t nr_06_ = 0;
    uint8_t nr_07_cpu_speed_ = 0;
    uint8_t nr_08_ = 0x10;
    uint8_t nr_09_ = 0;
    uint8_t nr_0a_ = 0x11;
    uint8_t nr_0b_ = 0x01;
    uint8_t nr_10_coreid_ = 0;
    uint8_t nr_11_video_timing_ = 0;
    uint8_t nr_12_layer2_bank_ = 8, nr_13_layer2_shadow_bank_ = 11;
    uint8_t nr_14_global_transparent_ = 0xe3;
    uint8_t nr_15_ = 0;
    uint8_t nr_16_l2_scrollx_ = 0, nr_17_l2_scrolly_ = 0;
    std::array<uint8_t, 4> clip_l2_{}, clip_spr_{}, clip_ula_{}, clip_tm_{};
    uint8_t clip_idx_l2_ = 0, clip_idx_spr_ = 0, clip_idx_ula_ = 0, clip_idx_tm_ = 0;
    bool nr_22_line_int_en_ = false;
    uint16_t nr_23_line_int_ = 0;
    uint8_t nr_26_ula_scrollx_ = 0, nr_27_ula_scrolly_ = 0;
    uint16_t nr_30_tm_scrollx_ = 0;
    uint8_t nr_31_tm_scrolly_ = 0, nr_32_lores_scrollx_ = 0, nr_33_lores_scrolly_ = 0;
    uint8_t nr_palette_idx_ = 0;
    bool nr_palette_sub_idx_ = false;
    uint8_t nr_42_ulanext_format_ = 7;
    uint8_t nr_43_ = 0;
    uint8_t nr_stored_palette_value_ = 0;
    uint8_t nr_4a_fallback_ = 0xe3;
    uint8_t nr_4b_sprite_transparent_ = 0xe3;
    uint8_t nr_4c_tm_transparent_ = 0x0f;
    uint16_t nr_copper_addr_ = 0;
    uint8_t nr_copper_data_stored_ = 0;
    uint8_t nr_64_copper_offset_ = 0;
    uint8_t nr_68_ = 0;
    uint8_t nr_6a_ = 0;
    uint8_t nr_6b_ = 0, nr_6c_ = 0, nr_6e_ = 0x2c, nr_6f_ = 0x0c;
    uint8_t nr_70_ = 0;
    uint8_t nr_71_ = 0;
    uint8_t nr_7f_ = 0xff;
    uint8_t nr_80_ = 0;
    uint8_t nr_82_ = 0xff, nr_83_ = 0xff, nr_84_ = 0xff, nr_85_ = 0x8f;
    uint8_t nr_86_ = 0xff, nr_87_ = 0xff, nr_88_ = 0xff, nr_89_ = 0x8f;
    uint8_t nr_8c_altrom_ = 0;
    uint8_t nr_8f_mapping_mode_ = 0;
    uint8_t nr_b8_ = 0x83, nr_b9_ = 0x01, nr_ba_ = 0x00, nr_bb_ = 0xcd;
    uint8_t nr_c0_ = 0;
    uint8_t nr_c2_ = 0, nr_c3_ = 0;
    uint8_t nr_c4_ = 0x81, nr_c6_ = 0;
    uint8_t nr_cc_ = 0, nr_cd_ = 0, nr_ce_ = 0;
    std::array<uint8_t, 256> nr_raw_{};  // registers that are only stored

    // Input
    MachineInputs inputs_{};
    std::array<uint8_t, 8> key_rows_{};
    uint8_t ext_keys_b0_ = 0, ext_keys_b1_ = 0;
    uint8_t joy_left_ = 0, joy_right_ = 0;

    // Audio
    std::vector<int16_t> audio_;
    int64_t audio_acc_ = 0;
    std::array<uint8_t, 4> dac_{};
    double dc_x_ = 0.0, dc_y_ = 0.0;

    // Boot / program loading
    std::string rom_dir_;
    std::vector<uint8_t> pending_nex_;
    int os_frames_ = -1;   // frames since the firmware left config mode
};

}  // namespace dsp
