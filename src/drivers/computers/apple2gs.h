#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/machine.h"
#include "cpu/w65c816.h"
#include "sound/es5503.h"

namespace dsp {

// Apple IIGS (ROM 03): WDC 65C816, FPI + Mega II memory system, VGC video
// (Apple II modes plus Super Hi-Res), Ensoniq DOC sound, ADB keyboard/mouse
// through the GLU, battery RAM/clock chip.
//
// Memory is described by per-page (256 byte) read/write tables covering the
// whole 24-bit address space, rebuilt whenever a soft switch that changes
// the bank 00/01/E0/E1 mapping is touched (language card, RAMRD/RAMWRT,
// ALTZP, 80STORE/PAGE2/HIRES, shadow register). Writes to banks 00/01 that
// fall in shadowed video areas are copied to the "slow" Mega II banks E0/E1,
// which is the only RAM the video hardware reads.
//
// Disks: ProDOS-order images (.po, .2mg, raw .hdv) are served by a
// high-level SmartPort card in slot 7 whose ROM traps into the emulator
// with WDM. Several images can be attached; each becomes one SmartPort unit
// (the first is the boot disk). The ROM's startup "Scan" finds slot 7 first.
// The internal 3.5"/5.25" ports (IWM) are present but report empty drives.
//
// The memory map, soft-switch and ADB microcontroller behaviour follows
// Apple's IIGS Hardware/Firmware References, cross-checked against the KEGS
// emulator's documented implementation.
class Apple2GS : public Machine {
public:
    static constexpr uint32_t kMasterClock = 28636363;
    static constexpr uint32_t kFastClock = kMasterClock / 10;  // ~2.864 MHz
    static constexpr int kScreenWidth = 640;
    static constexpr int kScreenHeight = 400;
    static constexpr double kFramesPerSecond = 59.923;

    // 14.318 MHz ticks: a fast CPU cycle is 5 ticks, a 1 MHz cycle 14.
    static constexpr int kTicksFast = 5;
    static constexpr int kTicksSlow = 14;
    static constexpr int kTicksPerLine = 65 * kTicksSlow;  // 910
    static constexpr int kLinesPerFrame = 262;

    Apple2GS();
    ~Apple2GS() override;

    bool init(const std::string& rom_path, std::string* error) override;
    void reset() override;
    void run_frame() override;

    void set_inputs(const MachineInputs& inputs) override;
    void set_dip_switch(int bank, uint8_t value) override;
    bool load_media(const std::string& path, std::string* error) override;

    const uint32_t* framebuffer() const override { return framebuffer_.data(); }
    int screen_width() const override { return kScreenWidth; }
    int screen_height() const override { return kScreenHeight; }
    double frames_per_second() const override { return kFramesPerSecond; }

    void drain_audio(std::vector<int16_t>& out) override;
    int sample_rate() const override { return 44100; }

    const char* title() const override { return "Apple IIGS"; }
    bool uses_keyboard() const override { return true; }
    bool uses_pointer() const override { return true; }

    // Debug / test helpers.
    W65C816& cpu() { return cpu_; }
    uint8_t peek(uint32_t address) { return mem_read(address); }
    uint8_t peek_slow(uint32_t address) const { return slow_ram_[address & 0x1ffff]; }
    uint64_t frame_count() const { return frame_count_; }
    // Queues a key press (ADB keycode) for scripted input.
    void post_key(int a2code, bool down) { adb_key_update(a2code, !down); }
    void set_trace(bool on) { trace_ = on; }

private:
    void warm_reset();

    // ---- memory ----
    uint8_t mem_read(uint32_t address);
    void mem_write(uint32_t address, uint8_t value);
    uint8_t io_read(uint16_t address);
    void io_write(uint16_t address, uint8_t value);
    void rebuild_map();
    void map_pages(uint32_t first_page, int count, uint8_t* rd, uint8_t* wr, uint8_t wr_flags);
    uint32_t vector_address(uint32_t vector);

    static constexpr uint8_t kWrShadowE0 = 0x01;
    static constexpr uint8_t kWrShadowE1 = 0x02;

    static constexpr int kRamBanks = 64;  // 4 MB of fast RAM (banks $00-$3F)
    std::vector<uint8_t> fast_ram_;
    std::array<uint8_t, 0x20000> slow_ram_{};
    std::array<uint8_t, 0x40000> rom_{};
    std::array<uint8_t, 0x1000> chr_rom_{};
    std::array<uint8_t, 0x100> slot7_rom_{};
    std::vector<uint8_t*> rd_page_;
    std::vector<uint8_t*> wr_page_;
    std::vector<uint8_t> wr_flags_;

    // ---- soft switches / Mega II / FPI state ----
    // statereg bits: 7 ALTZP, 6 PAGE2, 5 RAMRD, 4 RAMWRT, 3 RDROM, 2 LCBANK2,
    // 1 ROMBANK, 0 INTCXROM; bit 8 PREWRITE, bit 9 WRDEFRAM, bit 10 INTC8ROM.
    uint32_t statereg_ = 0;
    uint8_t shadow_ = 0;       // $C035
    uint8_t speed_ = 0x80;     // $C036
    uint8_t slotromsel_ = 0;   // $C02D
    uint8_t newvideo_ = 0x01;  // $C029
    uint8_t c023_ = 0;         // VGC interrupt register
    uint8_t c041_ = 0;         // INTEN
    uint8_t c046_ = 0;         // DIAGTYPE / INTFLAG
    uint8_t text_color_ = 0xf0;  // $C022
    uint8_t border_ = 0;         // $C034 low nibble
    uint8_t c034_ = 0;
    uint8_t c02b_ = 0x08;
    bool st80_ = false, vid80_ = false, altchar_ = false, text_ = true, mixed_ = false, hires_ = false;
    bool an3_ = true;
    bool mono_ = false;  // $C021 bit 7

    // ---- timing / interrupts ----
    uint64_t ticks_ = 0;            // 14.318 MHz ticks since power-on
    uint64_t frame_start_tick_ = 0;
    int line_ = 0;
    uint64_t frame_count_ = 0;
    int quarter_sec_counter_ = 0;
    int one_sec_counter_ = 0;
    enum : uint32_t {
        kIrqScan = 1u << 0,
        kIrq1Sec = 1u << 1,
        kIrqQtrSec = 1u << 2,
        kIrqVbl = 1u << 3,
        kIrqAdbSrq = 1u << 4,
        kIrqAdbData = 1u << 5,
        kIrqAdbMouse = 1u << 6,
        kIrqDoc = 1u << 7,
    };
    uint32_t irq_pending_ = 0;
    void add_irq(uint32_t mask);
    void remove_irq(uint32_t mask);
    void on_cpu_cycles(int cycles);
    uint32_t lines_since_vbl_counter() const;
    bool in_vbl() const;
    uint8_t read_vid_counter(bool vertical) const;
    void start_of_line(int line);

    W65C816 cpu_;

    // ---- clock chip / battery RAM ----
    enum class ClkMode { Idle, Time, Internal, Bram1, Bram2 };
    ClkMode clk_mode_ = ClkMode::Idle;
    bool clk_read_ = false;
    int clk_reg1_ = 0;
    uint8_t c033_ = 0;
    uint32_t clk_time_ = 0;
    std::array<uint8_t, 256> bram_{};
    void clock_write_c034(uint8_t value);
    void do_clock_data();
    void update_clock_time();

    // ---- ADB GLU (high-level microcontroller) ----
    enum class AdbState { Idle, InCmd, Sending };
    AdbState adb_state_ = AdbState::Idle;
    uint8_t adb_cmd_ = 0;
    int adb_cmd_len_ = 0, adb_cmd_so_far_ = 0;
    uint8_t adb_cmd_data_[16] = {};
    uint8_t adb_data_[16] = {};
    int adb_data_pending_ = 0;
    uint8_t adb_interrupt_byte_ = 0;
    uint8_t c027_ = 0;
    uint8_t c025_ = 0;  // modifier keys
    uint8_t adb_mode_ = 0;
    uint8_t adb_memory_[256] = {};
    int kbd_dev_addr_ = 2, mouse_dev_addr_ = 3;
    uint16_t kbd_reg3_ = 0x602;
    std::vector<uint8_t> kbd_reg0_;
    uint8_t kbd_buf_[8] = {};
    int kbd_chars_buffered_ = 0;
    int kbd_read_no_update_ = 0;
    bool key_down_ = false, hard_key_down_ = false;
    int a2code_down_ = 0;
    uint64_t repeat_frame_ = 0;
    int repeat_delay_ = 45, repeat_rate_ = 3;
    uint32_t virtual_key_up_[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    // mouse
    int mouse_target_x_ = 0, mouse_target_y_ = 0;
    int mouse_a2_x_ = 0, mouse_a2_y_ = 0;
    bool mouse_button_ = false, mouse_a2_button_ = false;
    bool mouse_valid_ = false, mouse_coord_ = false;
    std::array<bool, size_t(Key::Count)> prev_keys_{};
    bool prev_button_ = false;

    void adb_reset();
    uint8_t adb_read_c026();
    void adb_write_c026(uint8_t value);
    void adb_do_cmd();
    uint8_t adb_read_c027();
    void adb_write_c027(uint8_t value);
    void adb_send_bytes(int n, const uint8_t* bytes);
    void adb_response_packet(int n, uint32_t value);
    void adb_kbd_talk_reg0();
    void adb_key_update(int a2code, bool is_up);
    void adb_key_event(int a2code, bool is_up);
    uint8_t adb_read_c000();
    uint8_t adb_access_c010();
    uint8_t mouse_read_c024();
    void update_adb_data_irq();

    // ---- sound ----
    Es5503 doc_;
    uint8_t doc_ctl_ = 0;       // $C03C
    uint16_t doc_ptr_ = 0;      // $C03E/F
    uint8_t doc_saved_ = 0;
    double doc_phase_ = 0.0;
    double out_phase_ = 0.0;
    int32_t doc_last_ = 0;
    bool speaker_ = false;
    int32_t speaker_level_ = 0;
    std::vector<int16_t> audio_;
    void sound_advance(int ticks);
    double hp_prev_in_ = 0.0, hp_prev_out_ = 0.0;

    // ---- SCC (serial ports, idle) ----
    uint8_t scc_ptr_[2] = {0, 0};
    uint8_t scc_wr2_ = 0;

    // ---- IWM (empty drives) ----
    uint8_t iwm_state_ = 0;  // bit n = switch $C0E(2n+1) on
    uint8_t iwm_mode_ = 0;
    uint8_t c031_ = 0;
    uint8_t iwm_access(uint16_t offset, bool write, uint8_t value);

    // ---- SmartPort (slot 7) ----
    struct Disk {
        std::string path;
        std::vector<uint8_t> data;
        size_t offset = 0;   // start of block data (2IMG header)
        size_t blocks = 0;
        bool write_protect = false;
        bool persist = false;  // writes go back to the file (ProDOS-order images)
    };
    std::vector<Disk> disks_;
    void wdm(uint8_t signature);
    void smartport_boot();
    void smartport_prodos();
    void smartport_call();
    int disk_read(int unit, uint32_t buf, uint32_t block);
    int disk_write(int unit, uint32_t buf, uint32_t block);

    // ---- video ----
    std::array<uint32_t, kScreenWidth * kScreenHeight> framebuffer_{};
    uint32_t palette16_[16] = {};
    void render();
    void render_text_row(int row, bool col80, uint32_t fg, uint32_t bg, int ybase);
    void render_lores(int first_row, int last_row);
    void render_hires(int first_row, int last_row);
    void render_dhires(int first_row, int last_row);
    void render_shr();
    void fill_rect(int x, int y, int w, int h, uint32_t color);
    bool flash_on() const { return (frame_count_ / 16) & 1; }

    bool trace_ = false;
};

}  // namespace dsp
