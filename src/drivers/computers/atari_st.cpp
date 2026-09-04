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
    video_count_ = 0;
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

void AtariSt::ikbd_keys(const MachineInputs& inputs) {
    for (const IkbdMap& map : kIkbd) {
        const bool down = inputs.key(map.key);
        const size_t idx = size_t(map.key);
        if (down && !keys_down_[idx]) ikbd_push(map.code);
        if (!down && keys_down_[idx]) ikbd_push(uint8_t(map.code | 0x80));
        keys_down_[idx] = down;
    }
    if (inputs.has_pointer) {
        int dx = inputs.pointer_x - last_pointer_x_;
        int dy = inputs.pointer_y - last_pointer_y_;
        last_pointer_x_ = inputs.pointer_x;
        last_pointer_y_ = inputs.pointer_y;
        if (dx || dy || inputs.pointer_button1 || inputs.pointer_button2) {
            if (dx > 127) dx = 127;
            if (dx < -128) dx = -128;
            if (dy > 127) dy = 127;
            if (dy < -128) dy = -128;
            uint8_t head = 0xf8;
            if (inputs.pointer_button1) head = uint8_t(head | 2);
            if (inputs.pointer_button2) head = uint8_t(head | 1);
            ikbd_push(head);
            ikbd_push(uint8_t(dx));
            ikbd_push(uint8_t(dy));
        }
    }
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
    service_acia();
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
    for (IkbdByte& b : ikbd_pending_) b.cycles -= cycles;
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
    write_byte(address, uint8_t(value >> 8));
    write_byte(address + 1, uint8_t(value));
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
