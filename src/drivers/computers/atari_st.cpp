#include "drivers/computers/atari_st.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kTos104 = {{"tos104.bin", 0x30000, 0x0000, 0x90f4fbff}};
const std::vector<RomEntry> kTos102 = {{"tos102.bin", 0x30000, 0x0000, 0xd3c32283}};
const std::vector<RomEntry> kTos100 = {{"tos100.bin", 0x30000, 0x0000, 0xd331af30}};

struct IkbdMap {
    Key key;
    uint8_t code;
};

const IkbdMap kIkbd[] = {
    {Key::Escape, 0x01},    {Key::Num1, 0x02},     {Key::Num2, 0x03},
    {Key::Num3, 0x04},      {Key::Num4, 0x05},     {Key::Num5, 0x06},
    {Key::Num6, 0x07},      {Key::Num7, 0x08},     {Key::Num8, 0x09},
    {Key::Num9, 0x0a},      {Key::Num0, 0x0b},     {Key::Minus, 0x0c},
    {Key::Equals, 0x0d},    {Key::Backspace, 0x0e},{Key::Tab, 0x0f},
    {Key::Q, 0x10},         {Key::W, 0x11},        {Key::E, 0x12},
    {Key::R, 0x13},         {Key::T, 0x14},        {Key::Y, 0x15},
    {Key::U, 0x16},         {Key::I, 0x17},        {Key::O, 0x18},
    {Key::P, 0x19},         {Key::Enter, 0x1c},    {Key::LeftCtrl, 0x1d},
    {Key::A, 0x1e},         {Key::S, 0x1f},        {Key::D, 0x20},
    {Key::F, 0x21},         {Key::G, 0x22},        {Key::H, 0x23},
    {Key::J, 0x24},         {Key::K, 0x25},        {Key::L, 0x26},
    {Key::Semicolon, 0x27}, {Key::Quote, 0x28},    {Key::LeftShift, 0x2a},
    {Key::Z, 0x2c},         {Key::X, 0x2d},        {Key::C, 0x2e},
    {Key::V, 0x2f},         {Key::B, 0x30},        {Key::N, 0x31},
    {Key::M, 0x32},         {Key::Comma, 0x33},    {Key::Period, 0x34},
    {Key::Slash, 0x35},     {Key::RightShift, 0x36},{Key::Space, 0x39},
    {Key::CapsLock, 0x3a},  {Key::F1, 0x3b},       {Key::F2, 0x3c},
    {Key::F3, 0x3d},        {Key::F4, 0x3e},       {Key::F5, 0x3f},
    {Key::F6, 0x40},        {Key::F7, 0x41},       {Key::F8, 0x42},
    {Key::F9, 0x43},        {Key::F10, 0x44},      {Key::Home, 0x47},
    {Key::Up, 0x48},        {Key::Left, 0x4b},     {Key::Right, 0x4d},
    {Key::Down, 0x50},
};

uint32_t st_color(uint16_t w) {
    const int r = (w >> 8) & 7;
    const int g = (w >> 4) & 7;
    const int b = w & 7;
    return 0xff000000u | uint32_t(r * 36) << 16 | uint32_t(g * 36) << 8 | uint32_t(b * 36);
}

}  // namespace

AtariSt::AtariSt() : cpu_(kCpuClock), psg_(2000000, 1.2f) {
    ram_.assign(kRamSize, 0);
    rom_.assign(kRomSize, 0xff);
    cpu_.set_memory_handlers([this](uint32_t a) { return read_word(a); },
                             [this](uint32_t a, uint16_t v) { write_word(a, v); });
    cpu_.set_byte_handlers([this](uint32_t a) { return read_byte(a); },
                           [this](uint32_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cpu_cycles(c); });
    cpu_.set_reset_instruction_handler([this]() {
        mfp_.reset();
        floppy_.reset();
        psg_.reset();
        ikbd_rx_.clear();
        ikbd_pending_.clear();
        ikbd_reset_step_ = 0;
        mfp_.set_gpip_bit(7, 1);
        mfp_.set_gpip_bit(5, 1);
        mfp_.set_gpip_bit(4, 1);
    });
    cpu_.set_irq_acknowledge([this](int level) {
        if (level == 6) return mfp_.irq_ack();
        return -1;
    });
    psg_.set_port_handlers(nullptr, nullptr,
                           [this](uint8_t v) {
                               psg_port_a_ = v;
                               floppy_.set_psg_port_a(v);
                           },
                           nullptr);
    mfp_.set_irq_callback([this](bool asserted) {
        cpu_.set_irq(6, asserted ? IrqLine::Assert : IrqLine::Clear);
    });
    floppy_.set_ram(ram_.data(), uint32_t(ram_.size()));
}

bool AtariSt::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::string ignored;
    rom_.assign(kRomSize, 0xff);
    if (!loader.load(kTos104, rom_, &ignored) && !loader.load(kTos102, rom_, &ignored) &&
        !loader.load(kTos100, rom_, error)) {
        if (error && error->empty()) *error = "Atari ST TOS ROM not found in " + rom_path;
        return false;
    }
    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    reset();
    return true;
}

void AtariSt::reset() {
    std::fill(ram_.begin(), ram_.end(), 0);
    palette_.fill(0);
    rom_at_zero_ = true;
    memcfg_ = 0;
    video_hi_ = video_mid_ = video_lo_ = 0;
    sync_mode_ = 0x02;
    resolution_ = 0;
    psg_port_a_ = 0xff;
    acia_control_ = 0;
    ikbd_rx_.clear();
    ikbd_pending_.clear();
    ikbd_cmd_ = 0;
    ikbd_reset_step_ = 0;
    last_pointer_x_ = last_pointer_y_ = 0;
    pointer_frac_x_ = pointer_frac_y_ = 0;
    pointer_seen_ = false;
    last_pointer_b1_ = last_pointer_b2_ = false;
    video_count_ = 0;
    blit_halftone_.fill(0);
    blit_sxinc_ = blit_syinc_ = blit_dxinc_ = blit_dyinc_ = 0;
    blit_src_ = blit_dst_ = 0;
    blit_emask_[0] = blit_emask_[1] = blit_emask_[2] = 0;
    blit_xcount_ = blit_ycount_ = 0;
    blit_hop_ = blit_op_ = blit_ctrl_ = blit_skew_ = 0;
    blit_defer_start_ = false;
    keys_down_.fill(false);
    mfp_acc_ = 0;
    audio_acc_ = 0;
    audio_.clear();
    mfp_.reset();
    psg_.reset();
    floppy_.reset();
    floppy_.set_ram(ram_.data(), uint32_t(ram_.size()));
    // Colour monitor: GPIP bit 7 high. FDC/ACIA idle high (active low IRQs).
    mfp_.set_gpip_bit(7, 1);
    mfp_.set_gpip_bit(5, 1);
    mfp_.set_gpip_bit(4, 1);
    cpu_.reset();
}

bool AtariSt::load_media(const std::string& path, std::string* error) {
    if (!floppy_.load_file(path, error)) return false;
    floppy_.set_ram(ram_.data(), uint32_t(ram_.size()));
    return true;
}

void AtariSt::ikbd_push(uint8_t value) {
    // ~1 ms at 8 MHz. The 6850 has a one-byte buffer; extra bytes wait until
    // TOS reads RDR so GPIP4 can rise and fall again (MFP is edge triggered).
    ikbd_pending_.push_back({value, 8000});
}

void AtariSt::service_acia() {
    if (!ikbd_rx_.empty()) return;
    if (ikbd_pending_.empty() || ikbd_pending_.front().cycles > 0) return;
    ikbd_rx_.push_back(ikbd_pending_.front().value);
    ikbd_pending_.pop_front();
    mfp_.set_gpip_bit(4, 0);
}

void AtariSt::ikbd_byte(uint8_t value) {
    if (ikbd_reset_step_ == 0 && value == 0x80) {
        ikbd_reset_step_ = 1;
        return;
    }
    if (ikbd_reset_step_ == 1) {
        ikbd_reset_step_ = 0;
        if (value == 0x01) ikbd_push(0xf1);
        return;
    }
    ikbd_cmd_ = value;
}

std::vector<uint8_t> AtariSt::ikbd_pending_bytes() const {
    std::vector<uint8_t> out;
    out.reserve(ikbd_pending_.size());
    for (const IkbdByte& b : ikbd_pending_) out.push_back(b.value);
    return out;
}

void AtariSt::ikbd_mouse_packet(int dx, int dy, bool left, bool right) {
    uint8_t head = 0xf8;
    if (left) head = uint8_t(head | 2);
    if (right) head = uint8_t(head | 1);
    ikbd_push(head);
    ikbd_push(uint8_t(dx));
    ikbd_push(uint8_t(dy));
}

void AtariSt::ikbd_mouse(const MachineInputs& inputs) {
    if (!inputs.has_pointer) return;
    if (!pointer_seen_) {
        last_pointer_x_ = inputs.pointer_x;
        last_pointer_y_ = inputs.pointer_y;
        last_pointer_b1_ = inputs.pointer_button1;
        last_pointer_b2_ = inputs.pointer_button2;
        pointer_seen_ = true;
        pointer_frac_x_ = pointer_frac_y_ = 0;
        // A click on the first sample still needs a button packet, but never
        // a motion from (0,0) — that throws GEM's cursor off the screen.
        if (inputs.pointer_button1 || inputs.pointer_button2)
            ikbd_mouse_packet(0, 0, inputs.pointer_button1, inputs.pointer_button2);
        return;
    }

    const int dx_host = inputs.pointer_x - last_pointer_x_;
    const int dy_host = inputs.pointer_y - last_pointer_y_;
    last_pointer_x_ = inputs.pointer_x;
    last_pointer_y_ = inputs.pointer_y;

    // The shifter framebuffer is 640×400 with low/med doubled. IKBD deltas are
    // TOS screen pixels (320×200 low, 640×200 med, 640×400 high).
    const int mode = resolution_ & 3;
    const int xs = (mode == 0) ? 2 : 1;
    const int ys = (mode == 2) ? 1 : 2;
    pointer_frac_x_ += dx_host;
    pointer_frac_y_ += dy_host;
    int dx = pointer_frac_x_ / xs;
    int dy = pointer_frac_y_ / ys;
    pointer_frac_x_ -= dx * xs;
    pointer_frac_y_ -= dy * ys;

    const bool left = inputs.pointer_button1;
    const bool right = inputs.pointer_button2;
    const bool bchange = left != last_pointer_b1_ || right != last_pointer_b2_;
    last_pointer_b1_ = left;
    last_pointer_b2_ = right;
    if (!dx && !dy && !bchange) return;

    // Relative reports are signed 8-bit. Split so a fast host flick cannot
    // wrap; avoid -128 (0x80) which is also the IKBD reset prefix.
    bool send_button = bchange;
    while (dx || dy || send_button) {
        int sx = dx;
        int sy = dy;
        if (sx > 127) sx = 127;
        if (sx < -127) sx = -127;
        if (sy > 127) sy = 127;
        if (sy < -127) sy = -127;
        ikbd_mouse_packet(sx, sy, left, right);
        dx -= sx;
        dy -= sy;
        send_button = false;
        if (!dx && !dy) break;
    }
}

void AtariSt::ikbd_keys(const MachineInputs& inputs) {
    for (const IkbdMap& map : kIkbd) {
        const bool down = inputs.key(map.key);
        const size_t idx = size_t(map.key);
        if (down && !keys_down_[idx]) ikbd_push(map.code);
        if (!down && keys_down_[idx]) ikbd_push(uint8_t(map.code | 0x80));
        keys_down_[idx] = down;
    }
    ikbd_mouse(inputs);
}

uint8_t AtariSt::acia_status() const {
    uint8_t s = 0x02;  // TDRE
    if (!ikbd_rx_.empty()) s = uint8_t(s | 0x01 | 0x80);
    return s;
}

uint8_t AtariSt::acia_read_data() {
    if (ikbd_rx_.empty()) return 0;
    const uint8_t v = ikbd_rx_.front();
    ikbd_rx_.pop_front();
    if (ikbd_rx_.empty()) mfp_.set_gpip_bit(4, 1);
    return v;
}

void AtariSt::acia_write_control(uint8_t value) {
    acia_control_ = value;
    if ((value & 3) == 3) {
        ikbd_rx_.clear();
        ikbd_pending_.clear();
        ikbd_reset_step_ = 0;
        mfp_.set_gpip_bit(4, 1);
    }
}

void AtariSt::acia_write_data(uint8_t value) { ikbd_byte(value); }

void AtariSt::update_irqs() {
    const bool fdc = floppy_.irq();
    mfp_.set_gpip_bit(5, fdc ? 0 : 1);
}

void AtariSt::on_cpu_cycles(int cycles) {
    floppy_.tick(cycles);
    if (!ikbd_pending_.empty()) ikbd_pending_.front().cycles -= cycles;
    service_acia();
    update_irqs();
    mfp_acc_ += int64_t(cycles) * Mc68901::kClock;
    while (mfp_acc_ >= int64_t(kCpuClock)) {
        mfp_acc_ -= int64_t(kCpuClock);
        mfp_.tick(1);
    }
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= int64_t(kCpuClock)) {
        audio_acc_ -= int64_t(kCpuClock);
        int32_t s = psg_.update();
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        audio_.push_back(int16_t(s));
    }
}

uint8_t AtariSt::read_byte(uint32_t address) {
    address &= 0xffffff;
    if (rom_at_zero_ && address < 8) return rom_[address];
    if (address < ram_.size()) return ram_[address];
    if (address >= 0xfc0000 && address < 0xfc0000 + rom_.size()) {
        return rom_[address - 0xfc0000];
    }
    if (address >= 0xff8000) {
        switch (address) {
            case 0xff8001: return memcfg_;
            case 0xff8201: return video_hi_;
            case 0xff8203: return video_mid_;
            case 0xff8205: return uint8_t(video_count_ >> 16);
            case 0xff8207: return uint8_t(video_count_ >> 8);
            case 0xff8209: return uint8_t(video_count_);
            case 0xff820a: return sync_mode_;
            case 0xff8260: return resolution_;
            case 0xff8604: {
                const uint16_t v = floppy_.dma_data_r();
                update_irqs();
                return uint8_t(v >> 8);
            }
            case 0xff8605: {
                const uint16_t v = floppy_.dma_data_r();
                update_irqs();
                return uint8_t(v);
            }
            case 0xff8606: return uint8_t(floppy_.dma_status() >> 8);
            case 0xff8607: return uint8_t(floppy_.dma_status());
            case 0xff8609: return floppy_.dma_addr_r(0);
            case 0xff860b: return floppy_.dma_addr_r(1);
            case 0xff860d: return floppy_.dma_addr_r(2);
            case 0xff8800:
            case 0xff8801: return psg_.read();
            case 0xfffc00: return acia_status();
            case 0xfffc02: return acia_read_data();
            case 0xfffc04: return 0x02;  // MIDI ACIA TDRE
            case 0xfffc06: return 0;
            default: break;
        }
        if (address >= 0xff8240 && address < 0xff8260) {
            const int n = int(address - 0xff8240) >> 1;
            const uint16_t w = palette_[size_t(n)];
            return (address & 1) ? uint8_t(w) : uint8_t(w >> 8);
        }
        if (address >= 0xfffa00 && address <= 0xfffa2f && (address & 1)) {
            return mfp_.read(int((address - 0xfffa01) >> 1));
        }
        if (address >= 0xff8a00 && address <= 0xff8a3d) {
            if (address == 0xff8a3a) return blit_hop_;
            if (address == 0xff8a3b) return blit_op_;
            if (address == 0xff8a3c) return blit_ctrl_;
            if (address == 0xff8a3d) return blit_skew_;
            const uint16_t w = blit_get_word(address & 0xfffffe);
            return (address & 1) ? uint8_t(w) : uint8_t(w >> 8);
        }
    }
    return 0xff;
}

void AtariSt::write_byte(uint32_t address, uint8_t value) {
    address &= 0xffffff;
    if (rom_at_zero_ && address < 8) return;
    if (address < ram_.size()) {
        ram_[address] = value;
        return;
    }
    if (address >= 0xff8000) {
        switch (address) {
            case 0xff8001:
                memcfg_ = value;
                rom_at_zero_ = false;
                return;
            case 0xff8201: video_hi_ = value; return;
            case 0xff8203: video_mid_ = value; return;
            case 0xff820d: video_lo_ = value; return;
            case 0xff820a: sync_mode_ = value; return;
            case 0xff8260: resolution_ = uint8_t(value & 3); return;
            case 0xff8604: floppy_.dma_data_w(value); update_irqs(); return;
            case 0xff8605:
                floppy_.dma_data_w((floppy_.dma_mode() & 0xff00) | value);
                update_irqs();
                return;
            case 0xff8606: floppy_.dma_mode_w(uint16_t(value) << 8); return;
            case 0xff8607: floppy_.dma_mode_w(value); return;
            case 0xff8609: floppy_.dma_addr_w(0, value); return;
            case 0xff860b: floppy_.dma_addr_w(1, value); return;
            case 0xff860d: floppy_.dma_addr_w(2, value); return;
            case 0xff8800:
            case 0xff8801: psg_.control(value); return;
            case 0xff8802:
            case 0xff8803: psg_.write(value); return;
            case 0xfffc00: acia_write_control(value); return;
            case 0xfffc02: acia_write_data(value); return;
            default: break;
        }
        if (address >= 0xff8240 && address < 0xff8260) {
            const int n = int(address - 0xff8240) >> 1;
            uint16_t w = palette_[size_t(n)];
            if (address & 1) w = uint16_t((w & 0xff00) | value);
            else w = uint16_t((w & 0x00ff) | (uint16_t(value) << 8));
            palette_[size_t(n)] = uint16_t(w & 0x0777);
            return;
        }
        if (address >= 0xfffa00 && address <= 0xfffa2f && (address & 1)) {
            mfp_.write(int((address - 0xfffa01) >> 1), value);
            return;
        }
        if (address >= 0xff8a00 && address <= 0xff8a3d) {
            if (address == 0xff8a3a) {
                blit_hop_ = uint8_t(value & 3);
                return;
            }
            if (address == 0xff8a3b) {
                blit_op_ = uint8_t(value & 15);
                return;
            }
            if (address == 0xff8a3c) {
                blit_ctrl_ = value;
                if ((value & 0x80) && !blit_defer_start_) run_blitter();
                return;
            }
            if (address == 0xff8a3d) {
                blit_skew_ = value;
                return;
            }
            const uint32_t even = address & 0xfffffe;
            uint16_t w = blit_get_word(even);
            if (address & 1) w = uint16_t((w & 0xff00) | value);
            else w = uint16_t((w & 0x00ff) | (uint16_t(value) << 8));
            blit_set_word(even, w);
            return;
        }
    }
}

uint16_t AtariSt::read_word(uint32_t address) {
    address &= 0xfffffe;
    if (rom_at_zero_ && address < 8) {
        return uint16_t((rom_[address] << 8) | rom_[address + 1]);
    }
    if (address + 1 < ram_.size()) {
        return uint16_t((ram_[address] << 8) | ram_[address + 1]);
    }
    if (address >= 0xfc0000 && address + 1 < 0xfc0000 + rom_.size()) {
        const uint32_t o = address - 0xfc0000;
        return uint16_t((rom_[o] << 8) | rom_[o + 1]);
    }
    if (address == 0xff8604) {
        const uint16_t v = floppy_.dma_data_r();
        update_irqs();
        return v;
    }
    if (address == 0xff8606) return floppy_.dma_status();
    if (address == 0xff8800) return uint16_t(psg_.read() << 8);
    return uint16_t((read_byte(address) << 8) | read_byte(address + 1));
}

void AtariSt::write_word(uint32_t address, uint16_t value) {
    address &= 0xfffffe;
    if (rom_at_zero_ && address < 8) return;
    if (address + 1 < ram_.size()) {
        ram_[address] = uint8_t(value >> 8);
        ram_[address + 1] = uint8_t(value);
        return;
    }
    if (address == 0xff8604) {
        floppy_.dma_data_w(value);
        update_irqs();
        return;
    }
    if (address == 0xff8606) {
        floppy_.dma_mode_w(value);
        return;
    }
    if (address == 0xff8800) {
        psg_.control(uint8_t(value >> 8));
        psg_.write(uint8_t(value));
        return;
    }
    if (address >= 0xff8240 && address < 0xff8260) {
        palette_[size_t(address - 0xff8240) >> 1] = uint16_t(value & 0x0777);
        return;
    }
    // A word write to $FF8A3C stores control then skew. Starting the blit on
    // the first byte would run with the previous skew.
    if (address == 0xff8a3c) {
        blit_defer_start_ = true;
        write_byte(address, uint8_t(value >> 8));
        blit_defer_start_ = false;
        write_byte(address + 1, uint8_t(value));
        if (blit_ctrl_ & 0x80) run_blitter();
        return;
    }
    write_byte(address, uint8_t(value >> 8));
    write_byte(address + 1, uint8_t(value));
}

uint16_t AtariSt::blit_get_word(uint32_t even_addr) const {
    even_addr &= 0xfffffe;
    if (even_addr >= 0xff8a00 && even_addr < 0xff8a20) {
        return blit_halftone_[size_t(even_addr - 0xff8a00) >> 1];
    }
    switch (even_addr) {
        case 0xff8a20: return uint16_t(blit_sxinc_);
        case 0xff8a22: return uint16_t(blit_syinc_);
        case 0xff8a24: return uint16_t(blit_src_ >> 16);
        case 0xff8a26: return uint16_t(blit_src_);
        case 0xff8a28: return blit_emask_[0];
        case 0xff8a2a: return blit_emask_[1];
        case 0xff8a2c: return blit_emask_[2];
        case 0xff8a2e: return uint16_t(blit_dxinc_);
        case 0xff8a30: return uint16_t(blit_dyinc_);
        case 0xff8a32: return uint16_t(blit_dst_ >> 16);
        case 0xff8a34: return uint16_t(blit_dst_);
        case 0xff8a36: return blit_xcount_;
        case 0xff8a38: return blit_ycount_;
        default: return 0;
    }
}

void AtariSt::blit_set_word(uint32_t even_addr, uint16_t value) {
    even_addr &= 0xfffffe;
    if (even_addr >= 0xff8a00 && even_addr < 0xff8a20) {
        blit_halftone_[size_t(even_addr - 0xff8a00) >> 1] = value;
        return;
    }
    switch (even_addr) {
        case 0xff8a20: blit_sxinc_ = int16_t(value); return;
        case 0xff8a22: blit_syinc_ = int16_t(value); return;
        case 0xff8a24: blit_src_ = (blit_src_ & 0xffff) | (uint32_t(value) << 16); return;
        case 0xff8a26: blit_src_ = (blit_src_ & 0xffff0000u) | value; return;
        case 0xff8a28: blit_emask_[0] = value; return;
        case 0xff8a2a: blit_emask_[1] = value; return;
        case 0xff8a2c: blit_emask_[2] = value; return;
        case 0xff8a2e: blit_dxinc_ = int16_t(value); return;
        case 0xff8a30: blit_dyinc_ = int16_t(value); return;
        case 0xff8a32: blit_dst_ = (blit_dst_ & 0xffff) | (uint32_t(value) << 16); return;
        case 0xff8a34: blit_dst_ = (blit_dst_ & 0xffff0000u) | value; return;
        case 0xff8a36: blit_xcount_ = value; return;
        case 0xff8a38: blit_ycount_ = value; return;
        default: return;
    }
}

uint16_t AtariSt::blit_mem_read(uint32_t address) const {
    address &= 0xfffffe;
    if (address + 1 < ram_.size()) {
        return uint16_t((ram_[address] << 8) | ram_[address + 1]);
    }
    // Line-A text blits the system font out of TOS ($FC0000 / $E00000).
    auto from_rom = [&](uint32_t base) -> uint16_t {
        if (address >= base && address + 1 < base + rom_.size()) {
            const uint32_t o = address - base;
            return uint16_t((rom_[o] << 8) | rom_[o + 1]);
        }
        return 0;
    };
    if (address >= 0xfc0000) return from_rom(0xfc0000);
    if (address >= 0xe00000) return from_rom(0xe00000);
    return 0xffff;
}

void AtariSt::blit_mem_write(uint32_t address, uint16_t value) {
    address &= 0xfffffe;
    if (address + 1 < ram_.size()) {
        ram_[address] = uint8_t(value >> 8);
        ram_[address + 1] = uint8_t(value);
    }
}

void AtariSt::run_blitter() {
    // Line-A polls $FF8A3C bit 7. Finish the blit immediately so TOS never
    // sits in `tst.b (a5); bmi.s` after opening a GEM window.
    if (blit_ycount_ == 0) {
        blit_ctrl_ = uint8_t(blit_ctrl_ & 0x3f);
        return;
    }

    const int hop = blit_hop_ & 3;
    const int op = blit_op_ & 15;
    const int skew = blit_skew_ & 15;
    const bool fxsr = (blit_skew_ & 0x80) != 0;
    const bool nfsr = (blit_skew_ & 0x40) != 0;
    const bool smudge = (blit_ctrl_ & 0x20) != 0;
    int line = blit_ctrl_ & 15;

    static const bool kOpSrc[16] = {false, true,  true,  true,  true,  false, true,  true,
                                    true,  true,  false, true,  true,  true,  true,  false};
    static const bool kOpDst[16] = {false, true,  true,  false, true,  true,  true,  true,
                                    true,  true,  true,  true,  false, true,  true,  false};
    const bool hop_src = (hop & 2) != 0 || (hop == 1 && smudge);
    const bool need_src = kOpSrc[op] && hop_src;

    uint32_t xspan = blit_xcount_ ? uint32_t(blit_xcount_) : 65536u;
    uint32_t yleft = blit_ycount_;
    uint32_t src = blit_src_ & 0xfffffe;
    uint32_t dst = blit_dst_ & 0xfffffe;
    uint32_t buffer = 0;
    uint32_t words = 0;
    constexpr uint32_t kMaxWords = 0x100000;

    auto add_src = [&](int16_t inc) {
        src = uint32_t(int32_t(src) + inc) & 0xfffffe;
    };
    auto add_dst = [&](int16_t inc) {
        dst = uint32_t(int32_t(dst) + inc) & 0xfffffe;
    };
    auto fetch_src = [&]() {
        if (blit_sxinc_ < 0) buffer >>= 16;
        else buffer <<= 16;
        const uint32_t w = blit_mem_read(src);
        if (blit_sxinc_ < 0) buffer |= w << 16;
        else buffer |= w;
    };

    while (yleft && words < kMaxWords) {
        uint32_t xleft = xspan;
        bool skip_src = false;
        while (xleft && words < kMaxWords) {
            const bool first = xleft == xspan;
            const bool last = xleft == 1;
            uint16_t mask = blit_emask_[1];
            if (first || xspan == 1) mask = blit_emask_[0];
            else if (last) mask = blit_emask_[2];

            bool fetched = false;
            if (need_src) {
                if (first && fxsr) {
                    fetch_src();
                    add_src(blit_sxinc_);
                }
                if (!skip_src) {
                    fetch_src();
                    fetched = true;
                }
            }

            const uint16_t srcw = uint16_t(buffer >> skew);
            const uint16_t ht =
                smudge ? blit_halftone_[size_t(srcw & 15)] : blit_halftone_[size_t(line)];
            uint16_t hopv = 0xffff;
            if (hop == 1) hopv = ht;
            else if (hop == 2) hopv = srcw;
            else if (hop == 3) hopv = uint16_t(srcw & ht);

            const bool read_dst = kOpDst[op] || mask != 0xffff;
            const uint16_t dstw = read_dst ? blit_mem_read(dst) : 0;
            uint16_t lop = 0;
            switch (op) {
                case 0: lop = 0; break;
                case 1: lop = uint16_t(hopv & dstw); break;
                case 2: lop = uint16_t(hopv & ~dstw); break;
                case 3: lop = hopv; break;
                case 4: lop = uint16_t(~hopv & dstw); break;
                case 5: lop = dstw; break;
                case 6: lop = uint16_t(hopv ^ dstw); break;
                case 7: lop = uint16_t(hopv | dstw); break;
                case 8: lop = uint16_t(~hopv & ~dstw); break;
                case 9: lop = uint16_t(~hopv ^ dstw); break;
                case 10: lop = uint16_t(~dstw); break;
                case 11: lop = uint16_t(hopv | ~dstw); break;
                case 12: lop = uint16_t(~hopv); break;
                case 13: lop = uint16_t(~hopv | dstw); break;
                case 14: lop = uint16_t(~hopv | ~dstw); break;
                default: lop = 0xffff; break;
            }
            blit_mem_write(dst, uint16_t((lop & mask) | (dstw & ~mask)));
            words++;

            if (xleft == 2 && nfsr) skip_src = true;
            if (fetched) {
                if (last || skip_src) add_src(blit_syinc_);
                else add_src(blit_sxinc_);
            }
            if (last) {
                add_dst(blit_dyinc_);
                line = blit_dyinc_ >= 0 ? ((line + 1) & 15) : ((line - 1) & 15);
            } else {
                add_dst(blit_dxinc_);
            }
            xleft--;
        }
        yleft--;
    }

    blit_src_ = src;
    blit_dst_ = dst;
    blit_xcount_ = blit_xcount_ ? blit_xcount_ : uint16_t(xspan);
    blit_ycount_ = 0;
    blit_ctrl_ = uint8_t((blit_ctrl_ & 0x30) | (line & 15));
}

void AtariSt::render() {
    const uint32_t vbase =
        (uint32_t(video_hi_) << 16) | (uint32_t(video_mid_) << 8) | video_lo_;
    const int mode = resolution_ & 3;
    uint32_t pal[16];
    for (int i = 0; i < 16; i++) pal[i] = st_color(palette_[size_t(i)]);
    std::fill(framebuffer_.begin(), framebuffer_.end(), pal[0]);

    auto pix = [&](int x, int y, uint32_t c) {
        if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
        framebuffer_[size_t(y) * kWidth + x] = c;
    };

    if (mode == 2) {
        // High: 640×400, 1 bitplane.
        for (int y = 0; y < 400; y++) {
            const uint32_t row = vbase + uint32_t(y) * 80u;
            for (int x = 0; x < 640; x += 16) {
                if (row + uint32_t(x / 8) + 1 >= ram_.size()) continue;
                const uint16_t w =
                    uint16_t((ram_[row + uint32_t(x / 8)] << 8) | ram_[row + uint32_t(x / 8) + 1]);
                for (int b = 0; b < 16; b++) {
                    pix(x + b, y, (w & (0x8000 >> b)) ? pal[1] : pal[0]);
                }
            }
        }
        return;
    }
    if (mode == 1) {
        // Medium: 640×200, 2 bitplanes, line-doubled.
        for (int y = 0; y < 200; y++) {
            const uint32_t row = vbase + uint32_t(y) * 160u;
            for (int x = 0; x < 640; x += 16) {
                const uint32_t o = row + uint32_t(x / 4);
                if (o + 3 >= ram_.size()) continue;
                uint16_t p0 = uint16_t((ram_[o] << 8) | ram_[o + 1]);
                uint16_t p1 = uint16_t((ram_[o + 2] << 8) | ram_[o + 3]);
                for (int b = 0; b < 16; b++) {
                    const int c = ((p0 >> 15) & 1) | (((p1 >> 15) & 1) << 1);
                    pix(x + b, y * 2, pal[c]);
                    pix(x + b, y * 2 + 1, pal[c]);
                    p0 = uint16_t(p0 << 1);
                    p1 = uint16_t(p1 << 1);
                }
            }
        }
        return;
    }
    // Low: 320×200, 4 bitplanes, doubled in X and Y.
    for (int y = 0; y < 200; y++) {
        const uint32_t row = vbase + uint32_t(y) * 160u;
        for (int x = 0; x < 320; x += 16) {
            const uint32_t o = row + uint32_t(x / 2);
            if (o + 7 >= ram_.size()) continue;
            uint16_t p0 = uint16_t((ram_[o] << 8) | ram_[o + 1]);
            uint16_t p1 = uint16_t((ram_[o + 2] << 8) | ram_[o + 3]);
            uint16_t p2 = uint16_t((ram_[o + 4] << 8) | ram_[o + 5]);
            uint16_t p3 = uint16_t((ram_[o + 6] << 8) | ram_[o + 7]);
            for (int b = 0; b < 16; b++) {
                const int c = ((p0 >> 15) & 1) | (((p1 >> 15) & 1) << 1) |
                              (((p2 >> 15) & 1) << 2) | (((p3 >> 15) & 1) << 3);
                pix(x * 2 + b * 2, y * 2, pal[c]);
                pix(x * 2 + b * 2 + 1, y * 2, pal[c]);
                pix(x * 2 + b * 2, y * 2 + 1, pal[c]);
                pix(x * 2 + b * 2 + 1, y * 2 + 1, pal[c]);
                p0 = uint16_t(p0 << 1);
                p1 = uint16_t(p1 << 1);
                p2 = uint16_t(p2 << 1);
                p3 = uint16_t(p3 << 1);
            }
        }
    }
}

void AtariSt::run_frame() {
    const uint32_t vbase =
        (uint32_t(video_hi_) << 16) | (uint32_t(video_mid_) << 8) | video_lo_;
    const int pitch = (resolution_ & 3) == 2 ? 80 : 160;
    cpu_.set_irq(4, IrqLine::Hold);  // VBL
    for (int line = 0; line < kLines; line++) {
        if (line >= 63 && line < 263) {
            video_count_ = vbase + uint32_t(line - 63) * uint32_t(pitch);
        } else {
            video_count_ = vbase;
        }
        mfp_.pulse_tb();
        cpu_.run(kCyclesPerLine);
        update_irqs();
    }
    render();
}

void AtariSt::set_inputs(const MachineInputs& inputs) { ikbd_keys(inputs); }

void AtariSt::set_dip_switch(int, uint8_t) {}

void AtariSt::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
