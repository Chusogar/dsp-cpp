#include "drivers/computers/macii.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>

#include "core/rom_loader.h"

extern "C" {
#include "cpu/musashi/m68k.h"
}

namespace dsp {
namespace {

MacII* g_mac = nullptr;       // machine the Musashi callbacks talk to
MacII* g_context = nullptr;   // machine whose state is loaded in Musashi
bool g_musashi_ready = false;

constexpr uint32_t kSlot9 = 0xf9000000u;

// Mac ADB keycodes for the host keys.
struct HostKey {
    Key key;
    uint8_t code;
};
const HostKey kHostKeys[] = {
    {Key::A, 0x00}, {Key::S, 0x01}, {Key::D, 0x02}, {Key::F, 0x03}, {Key::H, 0x04},
    {Key::G, 0x05}, {Key::Z, 0x06}, {Key::X, 0x07}, {Key::C, 0x08}, {Key::V, 0x09},
    {Key::B, 0x0b}, {Key::Q, 0x0c}, {Key::W, 0x0d}, {Key::E, 0x0e}, {Key::R, 0x0f},
    {Key::Y, 0x10}, {Key::T, 0x11}, {Key::Num1, 0x12}, {Key::Num2, 0x13}, {Key::Num3, 0x14},
    {Key::Num4, 0x15}, {Key::Num6, 0x16}, {Key::Num5, 0x17}, {Key::Equals, 0x18},
    {Key::Num9, 0x19}, {Key::Num7, 0x1a}, {Key::Minus, 0x1b}, {Key::Num8, 0x1c},
    {Key::Num0, 0x1d}, {Key::Asterisk, 0x1e}, {Key::O, 0x1f}, {Key::U, 0x20},
    {Key::At, 0x21}, {Key::I, 0x22}, {Key::P, 0x23}, {Key::Enter, 0x24}, {Key::L, 0x25},
    {Key::J, 0x26}, {Key::Quote, 0x27}, {Key::K, 0x28}, {Key::Semicolon, 0x29},
    {Key::Backslash, 0x2a}, {Key::Comma, 0x2b}, {Key::Slash, 0x2c}, {Key::N, 0x2d},
    {Key::M, 0x2e}, {Key::Period, 0x2f}, {Key::Tab, 0x30}, {Key::Space, 0x31},
    {Key::Backquote, 0x32}, {Key::Backspace, 0x33}, {Key::Delete, 0x75}, {Key::F11, 0x35},
    {Key::LeftCtrl, 0x36}, {Key::RightCtrl, 0x36}, {Key::Cbm, 0x37}, {Key::LeftGui, 0x37},
    {Key::RightGui, 0x37}, {Key::LeftShift, 0x38}, {Key::RightShift, 0x38},
    {Key::CapsLock, 0x39}, {Key::RightAlt, 0x3a}, {Key::Left, 0x3b}, {Key::Right, 0x3c},
    {Key::Down, 0x3d}, {Key::Up, 0x3e}, {Key::Plus, 0x45}, {Key::Home, 0x73},
    {Key::F1, 0x7a}, {Key::F4, 0x76}, {Key::F5, 0x60}, {Key::F7, 0x62}, {Key::F8, 0x64},
    {Key::F9, 0x65}, {Key::F10, 0x6d},
};

// 1904-based local time for the RTC.
uint32_t mac_time_now() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    int64_t days = 0;
    for (int y = 1904; y < local.tm_year + 1900; y++) days += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
    days += local.tm_yday;
    return uint32_t(days * 86400 + local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec);
}

}  // namespace
}  // namespace dsp

// ---------------------------------------------------------------------------
// Musashi callbacks
// ---------------------------------------------------------------------------
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { return dsp::g_mac->cpu_read(a, 1); }
unsigned int m68k_read_memory_16(unsigned int a) { return dsp::g_mac->cpu_read(a, 2); }
unsigned int m68k_read_memory_32(unsigned int a) { return dsp::g_mac->cpu_read(a, 4); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { dsp::g_mac->cpu_write(a, v, 1); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { dsp::g_mac->cpu_write(a, v, 2); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { dsp::g_mac->cpu_write(a, v, 4); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return dsp::g_mac->cpu_peek(a, 1); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return dsp::g_mac->cpu_peek(a, 2); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return dsp::g_mac->cpu_peek(a, 4); }
void dsp_m68k_reset_instruction(void) {}
void dsp_m68k_instruction_hook(unsigned int pc) {
    if (dsp::g_mac) dsp::g_mac->instruction_hook(pc);
}
}

namespace dsp {

MacII::MacII() : via1_(kCpuClock / 20), via2_(kCpuClock / 20) {
    ram_.assign(kRamSize, 0);
    rom_.assign(0x40000, 0xff);
    vram_.assign(0x100000, 0);
    decl_rom_.assign(0x8000, 0xff);

    via1_.set_port_a([]() -> uint8_t { return 0x81; }, [this](uint8_t v) { via1_pa_w(v); });
    via1_.set_port_b([this]() { return via1_pb_r(); }, [this](uint8_t v) { via1_pb_w(v); });
    via1_.set_irq_callback([this](IrqLine l) {
        via1_irq_ = l != IrqLine::Clear;
        update_irqs();
    });
    via2_.set_port_a([this]() -> uint8_t { return uint8_t(0xc0 | nubus_irq_); }, [this](uint8_t v) { via2_pa_w(v); });
    via2_.set_port_b([]() -> uint8_t { return 0xcf; }, [this](uint8_t v) { via2_pb_w(v); });
    via2_.set_irq_callback([this](IrqLine l) {
        via2_irq_ = l != IrqLine::Clear;
        update_irqs();
    });
    asc_.set_irq_handler([this](bool on) { via2_.write_cb1(!on); });
    scsi_.set_plus_boot_patch(false);
    scsi_.set_write_through(true);
    scsi_.set_extend_reads(false);
    seed_pram(0x83);
}

// Battery-backed PRAM as a Mac II leaves it after its first boot, so the
// ROM keeps it instead of zapping it: XPRAM signature 'NuMc', SPValid $A8,
// and the slot 9 record of the Display Card 8*24 (board $0027) with the
// saved video mode. Mode $80..$84 = 1/2/4/8/24 bpp sResources.
void MacII::seed_pram(uint8_t video_mode) {
    pram_.fill(0);
    static const uint8_t kHead[32] = {
        0x00, 0x00, 0x4f, 0x48, 0x00, 0x00, 0x00, 0x00, 0x03, 0x88, 0x00, 0xcc, 'N', 'u', 'M', 'c',
        0xa8, 0x00, 0x00, 0x00, 0xcc, 0x0a, 0xcc, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x63, 0x00,
    };
    std::memcpy(pram_.data(), kHead, sizeof kHead);
    static const uint8_t kSlot9[8] = {0x00, 0x27, 0x80, 0xa6, 0xa6, 0x00, 0xff, 0x00};
    std::memcpy(pram_.data() + 0x46, kSlot9, sizeof kSlot9);
    pram_[0x48] = video_mode;
    static const uint8_t kVideo[5] = {0x01, 0xff, 0xff, 0xff, 0xdf};
    std::memcpy(pram_.data() + 0x77, kVideo, sizeof kVideo);
}

MacII::~MacII() {
    if (g_mac == this) g_mac = nullptr;
    if (g_context == this) g_context = nullptr;
}

void MacII::make_context_current() {
    g_mac = this;
    if (g_context == this) return;
    if (g_context) {
        g_context->cpu_context_.resize(m68k_context_size());
        m68k_get_context(g_context->cpu_context_.data());
    }
    if (!cpu_context_.empty()) m68k_set_context(cpu_context_.data());
    g_context = this;
}

bool MacII::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> rom;
    if (!loader.try_read("9779d2c4.rom", rom) && !loader.try_read("97851db6.rom", rom)) {
        if (error) *error = "missing Mac II ROM (9779d2c4.rom or 97851db6.rom)";
        return false;
    }
    if (rom.size() != 0x40000) {
        if (error) *error = "Mac II ROM must be 256 KB";
        return false;
    }
    rom_ = rom;
    std::vector<uint8_t> card;
    if (!loader.try_read("3410868.bin", card) || card.size() != 0x8000) {
        if (error) *error = "missing Macintosh Display Card 8*24 ROM (3410868.bin)";
        return false;
    }
    decl_rom_ = card;
    if (error) error->clear();

    if (!g_musashi_ready) {
        m68k_init();
        g_musashi_ready = true;
    }
    g_mac = this;
    if (g_context && g_context != this) {
        g_context->cpu_context_.resize(m68k_context_size());
        m68k_get_context(g_context->cpu_context_.data());
    }
    g_context = this;
    m68k_set_cpu_type(M68K_CPU_TYPE_68020);
    cpu_context_.resize(m68k_context_size());
    m68k_get_context(cpu_context_.data());
    initialized_ = true;
    reset();
    return true;
}

bool MacII::load_media(const std::string& path, std::string* error) {
    return scsi_.load_file(path, error);
}

void MacII::reset() {
    if (!initialized_) return;
    make_context_current();
    std::fill(ram_.begin(), ram_.end(), uint8_t(0));
    overlay_ = true;
    glue_ = 0;
    hmmu_24bit_ = false;
    nubus_irq_ = 0x3f;
    via1_irq_ = via2_irq_ = false;
    via1_.reset();
    via2_.reset();
    via2_.write_ca1(true);
    via2_.write_cb1(true);
    via2_.write_cb2(true);
    iwm_.reset();
    scsi_.reset();
    asc_.reset();
    scc_ptr_[0] = scc_ptr_[1] = 0;

    // video card
    std::fill(vram_.begin(), vram_.end(), uint8_t(0));
    jm_control_ = 2;
    jm_preload_ = 248;
    jm_base_ = 0;
    jm_stride_ = 20;
    std::memset(crtc_, 0, sizeof crtc_);
    vbl_disable_ = true;
    slot_irq_ = false;
    clut_addr_ = clut_cnt_ = 0;
    ramdac_mode_ = 0;
    for (int i = 0; i < 256; i++) clut_[size_t(i)] = 0xff000000u;

    // RTC keeps PRAM across resets (battery); time from the host.
    rtc_seconds_ = mac_time_now();
    rtc_enb_ = true;
    rtc_state_ = rtc_bits_ = 0;
    rtc_dir_out_ = false;

    adb_state_ = 3;
    adb_due_ = -1;
    adb_int_ = true;
    adb_listen_ = adb_talk_buf_ = false;
    adb_kbd_addr_ = 2;
    adb_mouse_addr_ = 3;
    key_events_.clear();
    mouse_dx_ = mouse_dy_ = 0;
    adb_mouse_polled_ = false;

    m68k_pulse_reset();
}

uint32_t MacII::debug_pc() const { return m68k_get_reg(nullptr, M68K_REG_PC); }

// ---------------------------------------------------------------------------
// Address decoding
// ---------------------------------------------------------------------------

uint32_t MacII::translate(uint32_t a) const {
    if (!hmmu_24bit_) return a;
    a &= 0xffffff;
    if (a >= 0x800000 && a <= 0x8fffff) return a | 0x40000000u;
    if (a >= 0x900000 && a <= 0xefffff) return 0xf0000000u | ((a & 0xf00000) << 4) | (a & 0xfffff);
    if (a >= 0xf00000) return a | 0x50000000u;
    return a;
}

uint8_t* MacII::ram_ptr(uint32_t pa) {
    if (pa >= 0x40000000u || overlay_) return nullptr;
    if (glue_ == 3) return pa < 0x100000 ? &ram_[pa] : nullptr;
    if (pa < kRamSize) return &ram_[pa];
    if (pa < kRamSize + (kRamSize / 2)) return &ram_[pa - kRamSize];  // bank A mirror
    return nullptr;
}

void MacII::bus_error(uint32_t address, bool write) {
    if (in_execute_) m68k_pulse_bus_error_at(address, write ? 1 : 0);
}

uint32_t MacII::cpu_read(uint32_t a, int size) { return phys_read(translate(a), size, true); }

uint32_t MacII::cpu_peek(uint32_t a, int size) {
    const uint32_t pa = translate(a);
    uint32_t v = 0;
    for (int i = 0; i < size; i++) {
        const uint32_t p = pa + uint32_t(i);
        uint8_t b = 0;
        if (uint8_t* r = ram_ptr(p)) b = *r;
        else if (p < 0x40000000u && overlay_) b = rom_[p & 0x3ffff];
        else if (p >= 0x40000000u && p < 0x50000000u) b = rom_[p & 0x3ffff];
        v = (v << 8) | b;
    }
    return v;
}

uint8_t MacII::peek(uint32_t logical) { return uint8_t(cpu_peek(logical, 1)); }
uint32_t MacII::peek32(uint32_t logical) { return cpu_peek(logical, 4); }

void MacII::cpu_write(uint32_t a, uint32_t v, int size) { phys_write(translate(a), v, size); }

uint32_t MacII::phys_read(uint32_t pa, int size, bool) {
    // RAM / ROM fast paths.
    if (pa < 0x40000000u) {
        if (overlay_) {
            uint32_t v = 0;
            for (int i = 0; i < size; i++) v = (v << 8) | rom_[(pa + uint32_t(i)) & 0x3ffff];
            return v;
        }
        uint32_t v = 0;
        for (int i = 0; i < size; i++) {
            uint8_t* r = ram_ptr(pa + uint32_t(i));
            v = (v << 8) | (r ? *r : 0);
        }
        return v;
    }
    if (pa < 0x50000000u) {
        uint32_t v = 0;
        for (int i = 0; i < size; i++) v = (v << 8) | rom_[(pa + uint32_t(i)) & 0x3ffff];
        return v;
    }
    if (pa < 0x60000000u) {
        if ((pa & 0xff000000u) != 0x50000000u) return 0;
        const uint32_t off = pa & 0xfffff;
        // SCSI pseudo-DMA windows: every byte lane is one DMA byte.
        if ((off >= 0x06000 && off < 0x06004) || (off >= 0x06060 && off < 0x06064) || (off >= 0x12000 && off < 0x14000)) {
            uint32_t v = 0;
            for (int i = 0; i < size; i++) v = (v << 8) | scsi_.read(0x260);
            return v;
        }
        // Byte-wide chips repeat on both lanes of a word.
        uint32_t v = 0;
        for (int i = 0; i < size; i += 2) {
            const uint32_t o = off + uint32_t(i);
            uint8_t b = io_read8(o);
            if (size == 1) return b;
            v = (v << 16) | (uint32_t(b) << 8) | b;
        }
        return v;
    }
    if (pa >= 0xf9000000u && pa < 0xfa000000u) return card_read(pa & 0xffffff, size);
    if (pa >= 0x90000000u) {
        bus_error(pa, false);
        return 0xffffffffu >> (32 - 8 * size);
    }
    return 0;
}

void MacII::phys_write(uint32_t pa, uint32_t value, int size) {
    if (pa < 0x40000000u) {
        if (overlay_) return;
        for (int i = 0; i < size; i++) {
            uint8_t* r = ram_ptr(pa + uint32_t(i));
            if (r) *r = uint8_t(value >> (8 * (size - 1 - i)));
        }
        return;
    }
    if (pa < 0x50000000u) return;  // ROM
    if (pa < 0x60000000u) {
        if ((pa & 0xff000000u) != 0x50000000u) return;
        const uint32_t off = pa & 0xfffff;
        if ((off >= 0x06000 && off < 0x06004) || (off >= 0x06060 && off < 0x06064) || (off >= 0x12000 && off < 0x14000)) {
            for (int i = 0; i < size; i++) scsi_.write(0x200, uint8_t(value >> (8 * (size - 1 - i))));
            return;
        }
        if (size == 1) {
            io_write8(off, uint8_t(value));
            return;
        }
        // Word/long: the chip sees the upper byte of each word.
        for (int i = 0; i < size; i += 2) io_write8(off + uint32_t(i), uint8_t(value >> (8 * (size - 1 - i))));
        return;
    }
    if (pa >= 0xf9000000u && pa < 0xfa000000u) {
        card_write(pa & 0xffffff, value, size);
        return;
    }
    if (pa >= 0x90000000u) bus_error(pa, true);
}

uint8_t MacII::io_read8(uint32_t off) {
    tick_devices(0);
    if (off >= 0x40000 && off < 0x42000) return via1_.read(uint8_t((off >> 9) & 0xf));
    if (off < 0x2000) return via1_.read(uint8_t((off >> 9) & 0xf));
    if (off < 0x4000) return via2_.read(uint8_t((off >> 9) & 0xf));
    if (off < 0x6000) {  // SCC 85C30: A1 selects channel (1=A), A2 data/control
        const int which = int((off >> 1) & 3);
        const int ch = (which & 1) ? 0 : 1;
        if (which & 2) return 0;
        const uint8_t reg = scc_ptr_[ch];
        scc_ptr_[ch] = 0;
        switch (reg) {
            case 0: return 0x54;  // Tx underrun/EOM, sync/hunt (idle line), Tx empty
            case 1: return 0x01;  // all sent
            case 2: return ch == 1 ? 0x06 : scc_wr2_;
            default: return 0;
        }
    }
    if (off >= 0x10000 && off < 0x12000) {
        const uint32_t sub = off - 0x10000;
        const bool dack = sub == 0x260;
        return scsi_.read((((sub >> 4) & 7) << 4) | (dack ? 0x200u : 0u));
    }
    if (off >= 0x14000 && off < 0x16000) return asc_.read(off - 0x14000);
    if (off >= 0x16000 && off < 0x18000) return iwm_.read(uint8_t((off >> 9) & 0xf));
    return 0;
}

void MacII::io_write8(uint32_t off, uint8_t v) {
    tick_devices(0);
    if ((off >= 0x40000 && off < 0x42000) || off < 0x2000) {
        via1_.write(uint8_t((off >> 9) & 0xf), v);
        return;
    }
    if (off < 0x4000) {
        via2_.write(uint8_t((off >> 9) & 0xf), v);
        return;
    }
    if (off < 0x6000) {
        const int which = int((off >> 1) & 3);
        const int ch = (which & 1) ? 0 : 1;
        if (which & 2) return;
        if (scc_ptr_[ch] == 0) {
            const uint8_t cmd = uint8_t((v >> 3) & 7);
            scc_ptr_[ch] = uint8_t((v & 7) | (cmd == 1 ? 8 : 0));
        } else {
            if (scc_ptr_[ch] == 2) scc_wr2_ = v;
            scc_ptr_[ch] = 0;
        }
        return;
    }
    if (off >= 0x10000 && off < 0x12000) {
        const uint32_t sub = off - 0x10000;
        const bool dack = sub == 0x200;
        scsi_.write((((sub >> 4) & 7) << 4) | (dack ? 0x200u : 0u), v);
        return;
    }
    if (off >= 0x14000 && off < 0x16000) {
        asc_.write(off - 0x14000, v);
        return;
    }
    if (off >= 0x16000 && off < 0x18000) iwm_.write(uint8_t((off >> 9) & 0xf), v);
}

// ---------------------------------------------------------------------------
// Apple Macintosh Display Card 8*24 (JMFB) in slot 9
// ---------------------------------------------------------------------------

uint32_t MacII::card_reg_read(uint32_t reg) {
    if (reg < 0x200010) {
        switch (reg & 0xc) {
            case 0x0: {
                // Monitor sense: Hi-Res 640x480 (MAME monitor type 6).
                static const unsigned kSense[4] = {6, 2, 4, 6};
                unsigned sense = kSense[0];
                if (jm_control_ & 0x800) sense &= kSense[1];
                if (jm_control_ & 0x400) sense &= kSense[2];
                if (jm_control_ & 0x200) sense &= kSense[3];
                return uint32_t((jm_control_ & 0xf1ff) | (sense << 9));
            }
            case 0x4: return jm_preload_;
            case 0x8: return jm_base_;
            default: return jm_stride_;
        }
    }
    if (reg >= 0x200100 && reg < 0x200200) {
        const uint32_t r = (reg - 0x200100) >> 2;
        if (r == 0xc0 / 4) {
            // Beam position flags (active low): 1 active, 2 back porch,
            // 4 sync, 8 front porch; 0x20 = horizontal blank.
            const int vactive = crtc_[0x24 / 4] ? crtc_[0x24 / 4] : 960;
            const int vbp = crtc_[0x28 / 4], vs = crtc_[0x2c / 4], vfp = crtc_[0x30 / 4];
            const int total = std::max(1, vactive + vbp + vs + vfp);
            const int64_t in_frame = int64_t(line_) * kCyclesPerLine;
            const int halfline = int((in_frame * total) / kCyclesPerFrame);
            // Our frame starts with active video.
            int pos = halfline + vs + vbp;
            if (pos >= total) pos -= total;
            uint8_t result = 0x0f;
            if (pos < vs) result &= uint8_t(~0x04);
            else if (pos < vs + vbp) result &= uint8_t(~0x02);
            else if (pos < vs + vbp + vactive) result &= uint8_t(~0x01);
            else result &= uint8_t(~0x08);
            return result;
        }
        if (r == 0xcc / 4) return 0;
        if (r < 64) return crtc_[r];
        return 0;
    }
    if (reg >= 0x200200 && reg < 0x200210) {
        if ((reg & 0xc) == 0) return 0;  // CLUT address read disabled
        return 0;
    }
    return 0;
}

void MacII::card_reg_write(uint32_t reg, uint32_t data) {
    if (reg < 0x200010) {
        data &= 0xffff;
        switch (reg & 0xc) {
            case 0x0: jm_control_ = uint16_t(data & 0x7fff); break;
            case 0x4: jm_preload_ = uint16_t(data & 0xff); break;
            case 0x8: jm_base_ = data; break;
            default: jm_stride_ = data; break;
        }
        return;
    }
    if (reg >= 0x200100 && reg < 0x200200) {
        data &= 0xffff;
        const uint32_t r = (reg - 0x200100) >> 2;
        if (r == 0x3c / 4) vbl_disable_ = (data & 2) != 0;
        if (r == 0x48 / 4) set_slot_irq(false);
        if (r < 64) crtc_[r] = uint16_t(data & 0x0fff);
        return;
    }
    if (reg >= 0x200200 && reg < 0x200210) {
        data &= 0xff;
        switch (reg & 0xc) {
            case 0x0:
                clut_addr_ = uint8_t(data);
                clut_cnt_ = 0;
                break;
            case 0x4:
                clut_rgb_[clut_cnt_++] = uint8_t(data);
                if (clut_cnt_ == 3) {
                    clut_[clut_addr_] = 0xff000000u | (uint32_t(clut_rgb_[0]) << 16) | (uint32_t(clut_rgb_[1]) << 8) | clut_rgb_[2];
                    clut_addr_++;
                    clut_cnt_ = 0;
                }
                break;
            case 0x8: ramdac_mode_ = uint8_t((data >> 1) & 0xf); break;
            default: break;
        }
    }
}

uint32_t MacII::card_read(uint32_t off, int size) {
    if (off < 0x100000) {
        uint32_t v = 0;
        for (int i = 0; i < size; i++) v = (v << 8) | vram_[(off + uint32_t(i)) & 0xfffff];
        return v;
    }
    if (off >= 0xfe0000) {
        uint32_t v = 0;
        for (int i = 0; i < size; i++) {
            const uint32_t o = off + uint32_t(i);
            uint8_t b = 0;
            if ((o & 3) == 3) b = decl_rom_[((o - 0xfe0000) >> 2) & 0x7fff];
            v = (v << 8) | b;
        }
        return v;
    }
    if (off >= 0x200000 && off < 0x200400) {
        // Registers are 32 bits wide; assemble whatever byte span was asked for.
        uint32_t v = 0, cached_reg = 0xffffffffu, r = 0;
        for (int i = 0; i < size; i++) {
            const uint32_t o = off + uint32_t(i);
            if ((o & ~3u) != cached_reg) {
                cached_reg = o & ~3u;
                r = card_reg_read(cached_reg);
            }
            v = (v << 8) | ((r >> (8 * (3 - (o & 3)))) & 0xff);
        }
        return v;
    }
    return 0;
}

void MacII::card_write(uint32_t off, uint32_t value, int size) {
    if (off < 0x100000) {
        for (int i = 0; i < size; i++) vram_[(off + uint32_t(i)) & 0xfffff] = uint8_t(value >> (8 * (size - 1 - i)));
        return;
    }
    if (off >= 0x200000 && off < 0x200400) {
        uint32_t data = value;
        if (size == 1) data = (value & 0xff) * 0x01010101u;  // lanes are smeared
        else if (size == 2) data = (value & 0xffff) * 0x00010001u;
        card_reg_write(off & ~3u, data);
    }
}

void MacII::set_slot_irq(bool on) {
    slot_irq_ = on;
    if (on) nubus_irq_ &= uint8_t(~0x01);
    else nubus_irq_ |= 0x01;
    via2_.write_ca1((nubus_irq_ & 0x3f) == 0x3f);
}

// ---------------------------------------------------------------------------
// VIAs, interrupts, timing
// ---------------------------------------------------------------------------

void MacII::update_irqs() {
    int level = 0;
    if (via2_irq_) level = 2;
    else if (via1_irq_) level = 1;
    m68k_set_irq(unsigned(level));
}

void MacII::via1_pa_w(uint8_t) {
    const uint8_t v = uint8_t((via1_.ora() & via1_.ddr_a()) | (~via1_.ddr_a() & 0xff));
    overlay_ = (v & 0x10) != 0;
    iwm_.set_hdsel((v & 0x20) != 0);
}

uint8_t MacII::via1_pb_r() {
    uint8_t v = 0xf6;
    if (adb_int_) v |= 0x08;
    if (rtc_out_) v |= 0x01;
    return v;
}

void MacII::via1_pb_w(uint8_t) {
    const uint8_t v = uint8_t((via1_.orb() & via1_.ddr_b()) | (~via1_.ddr_b() & 0xff));
    rtc_ce((v & 0x04) != 0);
    rtc_latch_ = (v & 0x01) != 0;
    rtc_clk((v & 0x02) != 0);
    const int st = (v >> 4) & 3;
    if (st != adb_state_) {
        adb_state_ = st;
        adb_state_changed();
    }
}

void MacII::via2_pa_w(uint8_t) {
    const uint8_t v = uint8_t((via2_.ora() & via2_.ddr_a()) | (~via2_.ddr_a() & 0xff));
    glue_ = uint8_t((v >> 6) & 3);
}

void MacII::via2_pb_w(uint8_t) {
    const uint8_t v = via2_.out_b();
    hmmu_24bit_ = (v & 0x08) == 0;
    via1_.write_ca1((v & 0x80) != 0);  // 60.15 Hz tick from VIA2 T1 on PB7
}

void MacII::tick_devices(int) {
    // Bring the chips up to the CPU's current position.
    const uint64_t now = total_cycles_ + (in_execute_ ? uint64_t(m68k_cycles_run()) : 0);
    int64_t delta = int64_t(now) - int64_t(last_tick_cycle_);
    if (delta <= 0) return;
    last_tick_cycle_ = now;

    via_rem_ += int(delta);
    const int via_ticks = via_rem_ / 20;
    via_rem_ -= via_ticks * 20;
    if (via_ticks > 0) {
        via1_.tick(via_ticks);
        via2_.tick(via_ticks);
    }
    iwm_.tick(int(delta));

    asc_acc_ += delta * Asc::kSampleRate;
    while (asc_acc_ >= int64_t(kCpuClock)) {
        asc_acc_ -= kCpuClock;
        int32_t s = asc_.generate() * 48;
        if (audio_.size() < 48000) audio_.push_back(int16_t(std::clamp(s, -32768, 32767)));
    }

    second_cycles_ += delta;
    if (second_cycles_ >= int64_t(kCpuClock) / 2) {
        second_cycles_ -= kCpuClock / 2;
        rtc_1hz_ = !rtc_1hz_;
        via1_.write_ca2(rtc_1hz_);
        if (rtc_1hz_) rtc_seconds_++;
    }

    if (adb_due_ >= 0 && int64_t(now) >= adb_due_) {
        adb_due_ = -1;
        adb_new_state();
    }
}

void MacII::instruction_hook(uint32_t pc) {
    if (trace_left_ > 0) {
        trace_left_--;
        char buf[128];
        m68k_disassemble(buf, pc, M68K_CPU_TYPE_68020);
        std::fprintf(stderr, "%08x  %-40s d0=%08x d1=%08x d6=%08x d7=%08x a0=%08x a3=%08x a4=%08x a7=%08x\n", pc, buf,
                     m68k_get_reg(nullptr, M68K_REG_D0), m68k_get_reg(nullptr, M68K_REG_D1), m68k_get_reg(nullptr, M68K_REG_D6), m68k_get_reg(nullptr, M68K_REG_D7),
                     m68k_get_reg(nullptr, M68K_REG_A0), m68k_get_reg(nullptr, M68K_REG_A3), m68k_get_reg(nullptr, M68K_REG_A4), m68k_get_reg(nullptr, M68K_REG_A7));
    }
}

void MacII::run_frame() {
    if (!initialized_) return;
    make_context_current();
    for (int line = 0; line < kLines; line++) {
        line_ = line;
        if (line == kHeight && !vbl_disable_) set_slot_irq(true);
        in_execute_ = true;
        const int done = m68k_execute(kCyclesPerLine);
        in_execute_ = false;
        total_cycles_ += uint64_t(done);
        tick_devices(0);
    }
    adb_update();
    render();
}

void MacII::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

// ---------------------------------------------------------------------------
// RTC (343-0042-B, 256-byte extended PRAM)
// ---------------------------------------------------------------------------

void MacII::rtc_ce(bool level) {
    if (level != rtc_enb_) {
        rtc_byte_ = 0;
        rtc_bits_ = 0;
        rtc_dir_out_ = false;
        rtc_out_ = false;
        rtc_state_ = 0;
    }
    rtc_enb_ = level;
}

void MacII::rtc_clk(bool level) {
    const bool falling = !level && rtc_clk_;
    rtc_clk_ = level;
    if (!falling || rtc_enb_) return;
    if (rtc_dir_out_) {
        if (rtc_bits_ > 0) rtc_bits_--;
        rtc_out_ = ((rtc_byte_ >> rtc_bits_) & 1) != 0;
        return;
    }
    rtc_byte_ = uint8_t((rtc_byte_ << 1) | (rtc_latch_ ? 1 : 0));
    if (++rtc_bits_ == 8) rtc_execute(rtc_byte_);
}

void MacII::rtc_execute(uint8_t data) {
    enum { kNormal = 0, kWrite = 1, kXpCommand = 2, kXpWrite = 3 };
    if (rtc_state_ == kXpCommand) {
        rtc_xaddr_ = uint8_t(((rtc_cmd_ & 7) << 5) | ((data & 0x7c) >> 2));
        if (rtc_cmd_ & 0x80) {
            rtc_dir_out_ = true;
            rtc_byte_ = pram_[rtc_xaddr_];
            rtc_bits_ = 8;
            rtc_state_ = kNormal;
        } else {
            rtc_state_ = kXpWrite;
            rtc_byte_ = 0;
            rtc_bits_ = 0;
        }
        return;
    }
    if (rtc_state_ == kXpWrite) {
        if (!rtc_wp_) pram_[rtc_xaddr_] = data;
        rtc_state_ = kNormal;
        rtc_bits_ = 0;
        return;
    }
    if (rtc_state_ == kWrite) {
        rtc_state_ = kNormal;
        rtc_bits_ = 0;
        const int i = (rtc_cmd_ >> 2) & 0x1f;
        if (rtc_wp_ && i != 13) return;
        if (i < 8) {
            const int sh = 8 * (i & 3);
            rtc_seconds_ = (rtc_seconds_ & ~(0xffu << sh)) | (uint32_t(data) << sh);
        } else if (i < 12 || i >= 16) {
            pram_[size_t(i)] = data;
        } else if (i == 13) {
            rtc_wp_ = (data & 0x80) != 0;
        }
        return;
    }
    rtc_cmd_ = data;
    if ((rtc_cmd_ & 0x78) == 0x38) {
        rtc_state_ = kXpCommand;
        rtc_byte_ = 0;
        rtc_bits_ = 0;
        return;
    }
    if (rtc_cmd_ & 0x80) {
        const int i = (rtc_cmd_ >> 2) & 0x1f;
        rtc_dir_out_ = true;
        if (i < 8) rtc_byte_ = uint8_t(rtc_seconds_ >> (8 * (i & 3)));
        else if (i < 12 || i >= 16) rtc_byte_ = pram_[size_t(i)];
        else rtc_byte_ = 0;
        rtc_bits_ = 8;
    } else {
        rtc_state_ = kWrite;
        rtc_byte_ = 0;
        rtc_bits_ = 0;
    }
}

// ---------------------------------------------------------------------------
// ADB transceiver + keyboard and mouse (protocol level, Mini vMac style)
// ---------------------------------------------------------------------------

void MacII::adb_shift_in(uint8_t v) {
    if (((via1_.acr() >> 2) & 7) == 3) via1_.shift_in_external(v);
}

uint8_t MacII::adb_shift_out() {
    if (((via1_.acr() >> 2) & 7) != 7) return 0;
    return via1_.shift_out_external();
}

void MacII::adb_state_changed() {
    // The transceiver answers a few hundred microseconds later.
    adb_due_ = int64_t(total_cycles_ + (in_execute_ ? uint64_t(m68k_cycles_run()) : 0)) + 10000;
}

bool MacII::adb_next_key(uint8_t* ev) {
    if (key_events_.empty()) return false;
    *ev = key_events_.front();
    key_events_.pop_front();
    return true;
}

bool MacII::adb_any_event() const {
    return !key_events_.empty() || mouse_dx_ != 0 || mouse_dy_ != 0 || mouse_button_ != mouse_button_sent_;
}

void MacII::adb_talk() {
    const uint8_t addr = adb_cmd_ >> 4;
    const int reg = adb_cmd_ & 3;
    if (addr == adb_mouse_addr_) {
        if (reg == 0) {
            adb_mouse_polled_ = true;
            int dx = std::clamp(mouse_dx_, -63, 63);
            int dy = std::clamp(mouse_dy_, -63, 63);
            mouse_dx_ -= dx;
            mouse_dy_ -= dy;
            const bool change = mouse_button_ != mouse_button_sent_;
            if (dx || dy || change) {
                mouse_button_sent_ = mouse_button_;
                adb_size_ = 2;
                adb_talk_buf_ = true;
                adb_buf_[0] = uint8_t((mouse_button_ ? 0x00 : 0x80) | (dy & 0x7f));
                adb_buf_[1] = uint8_t(0x80 | (dx & 0x7f));
            }
        } else if (reg == 3) {
            adb_size_ = 2;
            adb_talk_buf_ = true;
            adb_buf_[0] = uint8_t(0x60 | (adb_rand_++ & 0x0f));
            adb_buf_[1] = 0x01;
        }
    } else if (addr == adb_kbd_addr_) {
        if (reg == 0) {
            uint8_t ev;
            if (adb_next_key(&ev)) {
                adb_size_ = 2;
                adb_talk_buf_ = true;
                adb_buf_[0] = ev;
                adb_buf_[1] = adb_next_key(&ev) ? ev : 0xff;
            }
        } else if (reg == 3) {
            adb_size_ = 2;
            adb_talk_buf_ = true;
            adb_buf_[0] = uint8_t(0x60 | (adb_rand_++ & 0x0f));
            adb_buf_[1] = 0x01;
        } else if (reg == 2) {
            adb_size_ = 2;
            adb_talk_buf_ = true;
            adb_buf_[0] = 0xff;
            adb_buf_[1] = 0xff;
        }
    }
}

void MacII::adb_end_listen() {
    const uint8_t addr = adb_cmd_ >> 4;
    if ((adb_cmd_ & 3) == 3 && adb_size_ >= 2 && adb_buf_[1] == 0xfe) {
        if (addr == adb_mouse_addr_) adb_mouse_addr_ = adb_buf_[0] & 0x0f;
        else if (addr == adb_kbd_addr_) adb_kbd_addr_ = adb_buf_[0] & 0x0f;
    }
}

void MacII::adb_new_state() {
    adb_int_ = true;
    switch (adb_state_) {
        case 0: {  // new command
            if (adb_listen_) {
                adb_listen_ = false;
                adb_size_ = adb_index_;
                adb_end_listen();
            }
            adb_talk_buf_ = false;
            adb_index_ = 0;
            adb_cmd_ = adb_shift_out();
            switch ((adb_cmd_ >> 2) & 3) {
                case 0:
                    if ((adb_cmd_ & 3) == 0) {  // SendReset
                        adb_kbd_addr_ = 2;
                        adb_mouse_addr_ = 3;
                    } else if ((adb_cmd_ & 3) == 1) {  // Flush
                        adb_size_ = 2;
                        adb_talk_buf_ = true;
                        adb_buf_[0] = adb_buf_[1] = 0;
                    }
                    break;
                case 2: adb_listen_ = true; break;
                case 3: adb_talk(); break;
                default: break;
            }
            break;
        }
        case 1:
        case 2:
            if (!adb_listen_) {
                if (!adb_talk_buf_ || adb_index_ >= adb_size_) {
                    adb_shift_in(0xff);
                    adb_int_ = false;
                } else {
                    adb_shift_in(adb_buf_[adb_index_++]);
                }
            } else {
                uint8_t b = adb_shift_out();
                if (adb_index_ < sizeof adb_buf_) adb_buf_[adb_index_++] = b;
            }
            break;
        case 3:  // idle: auto-poll the last device
            if (adb_talk_buf_) {
                adb_shift_in(0xff);
            } else if (adb_any_event()) {
                if (((adb_cmd_ >> 2) & 3) == 3) adb_talk();
                adb_shift_in(0xff);
            }
            break;
    }
}

void MacII::adb_update() {
    if (adb_state_ != 3 || adb_due_ >= 0) return;
    if (adb_talk_buf_) return;
    if (adb_any_event()) {
        if (((adb_cmd_ >> 2) & 3) == 3) adb_talk();
        adb_shift_in(0xff);
    }
}

void MacII::debug_mouse(int dx, int dy, bool button) {
    const int x = std::clamp((last_px_ < 0 ? 0 : last_px_) + dx, 0, kWidth - 1);
    const int y = std::clamp((last_py_ < 0 ? 0 : last_py_) + dy, 0, kHeight - 1);
    move_pointer(x, y);
    mouse_button_ = button;
}

// The host pointer is absolute. Once the system is tracking the ADB mouse,
// its cursor globals are set directly (as Mini vMac does): MTemp, RawMouse
// and Mouse get the host position and CrsrNew asks the cursor VBL task to
// redraw it. Relative ADB motion would go through the Mouse control panel's
// acceleration and drift away from the host pointer. Before that (ROM
// boot, memory test) the motion is sent as ADB deltas.
void MacII::move_pointer(int x, int y) {
    x = std::clamp(x, 0, kWidth - 1);
    y = std::clamp(y, 0, kHeight - 1);
    if (adb_mouse_polled_ && !overlay_) {
        const uint8_t pos[4] = {uint8_t(y >> 8), uint8_t(y), uint8_t(x >> 8), uint8_t(x)};
        if (std::memcmp(&ram_[0x828], pos, 4) != 0 || std::memcmp(&ram_[0x82c], pos, 4) != 0) {
            std::memcpy(&ram_[0x828], pos, 4);  // MTemp
            std::memcpy(&ram_[0x82c], pos, 4);  // RawMouse
            std::memcpy(&ram_[0x830], pos, 4);  // Mouse
            ram_[0x8ce] = 0xff;                 // CrsrNew
        }
        mouse_dx_ = mouse_dy_ = 0;
    } else if (last_px_ >= 0) {
        mouse_dx_ += x - last_px_;
        mouse_dy_ += y - last_py_;
    }
    last_px_ = x;
    last_py_ = y;
}

void MacII::set_inputs(const MachineInputs& inputs) {
    for (const HostKey& hk : kHostKeys) {
        const bool now = inputs.key(hk.key);
        if (now != prev_keys_[size_t(hk.key)]) key_events_.push_back(uint8_t(hk.code | (now ? 0 : 0x80)));
    }
    prev_keys_ = inputs.keys;
    if (inputs.has_pointer) {
        move_pointer(inputs.pointer_x, inputs.pointer_y);
        mouse_button_ = inputs.pointer_button1;
    }
}

// ---------------------------------------------------------------------------
// Video output
// ---------------------------------------------------------------------------

void MacII::render() {
    if (!(jm_control_ & 0x40)) {  // transfer (display) disabled
        framebuffer_.fill(0xff000000u);
        return;
    }
    const bool direct = ramdac_mode_ == 0xd;
    const uint32_t base = jm_base_ << (direct ? 6 : 5);
    const uint32_t stride = jm_stride_ << (direct ? 3 : 2);
    for (int y = 0; y < kHeight; y++) {
        const uint32_t row = base + uint32_t(y) * stride;
        uint32_t* out = &framebuffer_[size_t(y) * kWidth];
        for (int x = 0; x < kWidth; x++) {
            uint32_t c;
            switch (ramdac_mode_) {
                case 0x0: {
                    const uint8_t b = vram_[(row + uint32_t(x >> 3)) & 0xfffff];
                    c = clut_[(b >> (7 - (x & 7))) & 1];
                    break;
                }
                case 0x4: {
                    const uint8_t b = vram_[(row + uint32_t(x >> 2)) & 0xfffff];
                    c = clut_[(b >> (6 - 2 * (x & 3))) & 3];
                    break;
                }
                case 0x8: {
                    const uint8_t b = vram_[(row + uint32_t(x >> 1)) & 0xfffff];
                    c = clut_[(x & 1) ? (b & 0xf) : (b >> 4)];
                    break;
                }
                case 0xc: c = clut_[vram_[(row + uint32_t(x)) & 0xfffff]]; break;
                case 0xd: {
                    const uint32_t p = row + uint32_t(x) * 3;
                    c = 0xff000000u | (uint32_t(vram_[p & 0xfffff]) << 16) | (uint32_t(vram_[(p + 1) & 0xfffff]) << 8) |
                        vram_[(p + 2) & 0xfffff];
                    break;
                }
                default: c = 0xff000000u; break;
            }
            out[x] = c;
        }
    }
}

}  // namespace dsp
