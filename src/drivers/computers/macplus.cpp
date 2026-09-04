#include "drivers/computers/macplus.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kPlusEvenV3 = {{"342-0341-c.u6d", 0x10000, 0, 0xf69697e6}};
const std::vector<RomEntry> kPlusOddV3 = {{"342-0342-b.u8d", 0x10000, 0, 0x49f25913}};
const std::vector<RomEntry> kPlusEvenV2 = {{"342-0341-b.u6d", 0x10000, 0, 0x65341487}};
const std::vector<RomEntry> kPlusOddV2 = {{"342-0342-a.u8d", 0x10000, 0, 0xfb766270}};
const std::vector<RomEntry> kPlusEvenV1 = {{"342-0341-a.u6d", 0x10000, 0, 0x5095fe39}};
const std::vector<RomEntry> kPlusOddV1 = {{"342-0342-a.u8d", 0x10000, 0, 0xfb766270}};

bool interleave_rom(const std::vector<uint8_t>& even, const std::vector<uint8_t>& odd,
                    std::vector<uint8_t>& dest) {
    if (even.size() < 0x10000 || odd.size() < 0x10000) return false;
    dest.assign(MacPlus::kRomSize, 0xff);
    for (uint32_t i = 0; i < 0x10000; i++) {
        dest[i * 2] = even[i];
        dest[i * 2 + 1] = odd[i];
    }
    return true;
}

}  // namespace

MacPlus::MacPlus() : cpu_(kCpuClock), via_(kCpuClock / 10) {
    ram_.assign(kRamSize, 0);
    rom_.assign(kRomSize, 0xff);
    cpu_.set_memory_handlers([this](uint32_t a) { return read_word(a); },
                             [this](uint32_t a, uint16_t v) { write_word(a, v); });
    cpu_.set_byte_handlers([this](uint32_t a) { return read_byte(a); },
                           [this](uint32_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cpu_cycles(c); });
    via_.set_port_a([this]() { return via_pa_r(); }, [this](uint8_t v) { via_pa_w(v); });
    via_.set_port_b([this]() { return via_pb_r(); }, [this](uint8_t v) { via_pb_w(v); });
    via_.set_irq_callback([this](IrqLine line) {
        via_irq_ = line != IrqLine::Clear;
        update_irqs();
    });
}

bool MacPlus::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::vector<uint8_t> even, odd;
    std::string ignored;
    even.assign(0x10000, 0xff);
    odd.assign(0x10000, 0xff);
    bool ok = false;
    if (loader.load(kPlusEvenV3, even, &ignored) && loader.load(kPlusOddV3, odd, &ignored))
        ok = true;
    else if (loader.load(kPlusEvenV2, even, &ignored) && loader.load(kPlusOddV2, odd, &ignored))
        ok = true;
    else if (loader.load(kPlusEvenV1, even, &ignored) && loader.load(kPlusOddV1, odd, error))
        ok = true;
    if (!ok || !interleave_rom(even, odd, rom_)) {
        if (error && error->empty()) *error = "Macintosh Plus ROM not found in " + rom_path;
        return false;
    }
    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    reset();
    return true;
}

void MacPlus::reset() {
    std::fill(ram_.begin(), ram_.end(), 0);
    overlay_ = true;
    screen_buffer_ = 1;
    main_sound_ = true;
    snd_enable_ = false;
    snd_vol_ = 3;
    via_irq_ = false;
    scc_irq_ = false;
    mouse_button_ = false;
    mouse_bit_[0] = mouse_bit_[1] = 0;
    mouse_last_[0] = mouse_last_[1] = 0;
    last_pointer_x_ = last_pointer_y_ = 0;
    pointer_seen_ = false;
    rtc_ca2_ = false;
    scc_ptr_[0] = scc_ptr_[1] = 0;
    scc_wr1_[0] = scc_wr1_[1] = 0;
    scc_dcd_[0] = scc_dcd_[1] = false;
    via_acc_ = 0;
    rtc_acc_ = 0;
    kbd_acc_ = 0;
    kbd_cmd_ = 0;
    kbd_reply_ = 0x7b;
    kbd_shift_ = 0x7b;
    kbd_bits_ = 0;
    audio_.clear();
    via_.reset();
    iwm_.reset();
    rtc_.reset();
    scsi_.reset();
    cpu_.reset();
}

bool MacPlus::load_media(const std::string& path, std::string* error) {
    std::string floppy_error;
    if (iwm_.load_file(path, &floppy_error)) return true;
    if (scsi_.load_file(path, error)) return true;
    if (error && error->empty()) *error = floppy_error;
    return false;
}

void MacPlus::update_irqs() {
    cpu_.set_irq(2, scc_irq_ ? IrqLine::Assert : IrqLine::Clear);
    cpu_.set_irq(1, via_irq_ ? IrqLine::Assert : IrqLine::Clear);
}

void MacPlus::on_cpu_cycles(int cycles) {
    iwm_.tick(cycles);
    via_acc_ += cycles;
    while (via_acc_ >= 10) {
        via_acc_ -= 10;
        via_.tick(1);
    }
    const uint8_t sr_mode = uint8_t((via_.acr() >> 2) & 7);
    if ((sr_mode == 3 || sr_mode == 7) && via_.sr_busy()) {
        kbd_acc_ += cycles;
        if (kbd_acc_ >= 80) {
            kbd_acc_ = 0;
            clock_keyboard();
        }
    } else {
        kbd_acc_ = 0;
    }
    rtc_acc_ += cycles;
    if (rtc_acc_ >= int64_t(kCpuClock)) {
        rtc_acc_ -= int64_t(kCpuClock);
        rtc_.tick_seconds();
        rtc_ca2_ = !rtc_ca2_;
        via_.write_ca2(rtc_ca2_);
    }
}

uint8_t MacPlus::via_pa_r() { return 0x81; }

uint8_t MacPlus::via_pb_r() {
    uint8_t val = 0x40;
    val = uint8_t(val | (mouse_bit_[1] << 5));
    val = uint8_t(val | (mouse_bit_[0] << 4));
    if (!mouse_button_) val = uint8_t(val | 0x08);
    if (rtc_.data_r()) val = uint8_t(val | 0x01);
    return val;
}

uint8_t MacPlus::keyboard_reply(uint8_t command) {
    switch (command) {
        case 0x10:  // Inquiry
        case 0x14:  // Instant
            return 0x7b;
        case 0x16:  // Model number: US Macintosh Plus (M0110A)
            return 0x0b;
        case 0x36:  // Self-test
            return 0x7d;
        default:
            return 0x7b;
    }
}

void MacPlus::clock_keyboard() {
    const uint8_t sr_mode = uint8_t((via_.acr() >> 2) & 7);
    if (sr_mode == 3) {
        via_.set_cb2_data((kbd_shift_ & 0x80) != 0);
        kbd_shift_ = uint8_t(kbd_shift_ << 1);
    }
    via_.write_cb1(true);
    via_.write_cb1(false);
    if (sr_mode == 7) {
        kbd_cmd_ = uint8_t((kbd_cmd_ << 1) | (via_.cb2() ? 1 : 0));
        if (++kbd_bits_ >= 8 || !via_.sr_busy()) {
            kbd_reply_ = keyboard_reply(kbd_cmd_);
            kbd_shift_ = kbd_reply_;
            kbd_cmd_ = 0;
            kbd_bits_ = 0;
        }
    } else if (sr_mode == 3 && !via_.sr_busy()) {
        kbd_shift_ = kbd_reply_;
    }
}

void MacPlus::via_pa_w(uint8_t data) {
    screen_buffer_ = (data & 0x40) ? 1 : 0;
    iwm_.set_hdsel((data & 0x20) != 0);
    main_sound_ = (data & 0x08) != 0;
    snd_vol_ = data & 7;
    // PA4 is pulled up, so overlay stays on until the pin is an output.
    if (via_.ddr_a() & 0x10)
        overlay_ = (data & 0x10) != 0;
    else
        overlay_ = true;
}

void MacPlus::via_pb_w(uint8_t data) {
    snd_enable_ = (data & 0x80) == 0;
    rtc_.ce_w((data & 0x04) != 0);
    rtc_.clk_w((data & 0x02) != 0);
    rtc_.data_w((data & 0x01) != 0);
}

uint8_t MacPlus::scc_read(uint32_t address) {
    const int which = (address >> 1) & 3;
    const int ch = (which & 1) ? 0 : 1;  // 0 = A, 1 = B
    const bool data_reg = (which & 2) != 0;
    if (data_reg) return 0;
    uint8_t rr = scc_ptr_[ch];
    scc_ptr_[ch] = 0;
    if (rr == 0) {
        uint8_t v = 0x04;  // Tx empty
        if (scc_dcd_[ch]) v = uint8_t(v | 0x08);
        return v;
    }
    return 0;
}

void MacPlus::scc_write(uint32_t address, uint8_t value) {
    const int which = (address >> 1) & 3;
    const int ch = (which & 1) ? 0 : 1;
    const bool data_reg = (which & 2) != 0;
    if (data_reg) return;
    if (scc_ptr_[ch] == 0) {
        scc_ptr_[ch] = uint8_t(value & 7);
        return;
    }
    if (scc_ptr_[ch] == 1) scc_wr1_[ch] = value;
    scc_ptr_[ch] = 0;
}

uint32_t MacPlus::ram_index(uint32_t address) const {
    address &= 0xffffff;
    // $600000–$7FFFFF is the 2MB overlay-time RAM window and always aliases
    // the first 2MB. The $000000–$3FFFFF decode is unique through all 4MB so
    // the ROM memory test can set MemTop to $400000 instead of seeing a wrap.
    if (address >= 0x600000 && address < 0x800000) address -= 0x600000;
    return address & (kRamSize - 1);
}

uint8_t MacPlus::read_byte(uint32_t address) {
    address &= 0xffffff;
    if (address < 0x400000) {
        if (overlay_) return rom_[address & (kRomSize - 1)];
        return ram_at(address);
    }
    // 128K Plus ROMs decode A17 onto /OE: $420000 is open bus, $440000 still
    // hits ROM[0]. The ROM compares those two longs and only sets HWCfgFlags
    // (SCSI present) when they differ — a 512KE mirrors the ROM and has no SCSI.
    if (address < 0x500000) {
        if (address & 0x20000) return 0xff;
        return rom_[address & (kRomSize - 1)];
    }
    if (address >= 0x580000 && address < 0x600000) return scsi_.read(address);
    if (address >= 0x600000 && address < 0x800000) return ram_at(address);
    if (address >= 0x800000 && address < 0xa00000) return scc_read(address);
    if (address >= 0xc00000 && address < 0xe00000) return iwm_.read(uint8_t((address >> 9) & 0x0f));
    if (address >= 0xe80000 && address < 0xf00000) return via_.read(uint8_t((address >> 9) & 0x0f));
    return 0xff;
}

void MacPlus::write_byte(uint32_t address, uint8_t value) {
    address &= 0xffffff;
    // Overlay only remaps reads at 0; writes always land in RAM (write-through),
    // and $600000 is the overlay-time RAM window documented for 128K/512K/Plus.
    if (address < 0x400000) {
        ram_at(address, value);
        return;
    }
    if (address >= 0x580000 && address < 0x600000) {
        scsi_.write(address, value);
        return;
    }
    if (address >= 0x600000 && address < 0x800000) {
        ram_at(address, value);
        return;
    }
    if (address >= 0xa00000 && address < 0xc00000) {
        scc_write(address, value);
        return;
    }
    if (address >= 0xc00000 && address < 0xe00000) {
        iwm_.write(uint8_t((address >> 9) & 0x0f), value);
        return;
    }
    if (address >= 0xe80000 && address < 0xf00000) {
        via_.write(uint8_t((address >> 9) & 0x0f), value);
        return;
    }
}

uint16_t MacPlus::read_word(uint32_t address) {
    address &= 0xfffffe;
    if (address >= 0x580000 && address < 0x600000) {
        const uint8_t v = scsi_.read(address);
        return uint16_t((uint16_t(v) << 8) | v);
    }
    if (address >= 0xc00000 && address < 0xe00000) {
        const uint8_t v = iwm_.read(uint8_t((address >> 9) & 0x0f));
        return uint16_t((uint16_t(v) << 8) | v);
    }
    if (address >= 0xe80000 && address < 0xf00000) {
        const uint8_t v = via_.read(uint8_t((address >> 9) & 0x0f));
        return uint16_t((uint16_t(v) << 8) | v);
    }
    if (address >= 0x800000 && address < 0xa00000) {
        const uint8_t v = scc_read(address);
        return uint16_t(uint16_t(v) << 8);
    }
    return uint16_t((uint16_t(read_byte(address)) << 8) | read_byte(address + 1));
}

void MacPlus::write_word(uint32_t address, uint16_t value) {
    address &= 0xfffffe;
    if (address >= 0x580000 && address < 0x600000) {
        scsi_.write(address | 1u, uint8_t(value));
        return;
    }
    if (address >= 0xc00000 && address < 0xe00000) {
        iwm_.write(uint8_t((address >> 9) & 0x0f), uint8_t(value));
        return;
    }
    if (address >= 0xe80000 && address < 0xf00000) {
        via_.write(uint8_t((address >> 9) & 0x0f), uint8_t(value >> 8));
        return;
    }
    if (address >= 0xa00000 && address < 0xc00000) {
        scc_write(address, uint8_t(value));
        return;
    }
    write_byte(address, uint8_t(value >> 8));
    write_byte(address + 1, uint8_t(value));
}

void MacPlus::render() {
    const uint32_t video_off =
        kRamSize - (screen_buffer_ ? 0x5900u : 0xD900u);
    const uint8_t* video = ram_.data() + video_off;
    uint32_t* out = framebuffer_.data();
    for (int y = 0; y < kHeight; y++) {
        for (int x = 0; x < kWidth; x += 16) {
            const uint16_t word = uint16_t((uint16_t(video[0]) << 8) | video[1]);
            video += 2;
            for (int b = 0; b < 16; b++) {
                const bool black = (word & (0x8000 >> b)) != 0;
                *out++ = black ? 0xff000000u : 0xffffffffu;
            }
        }
    }
}

void MacPlus::run_frame() {
    for (int line = 0; line < kVTotal; line++) {
        const bool vblank = line < kVBlankLines;
        via_.write_ca1(vblank);
        via_.set_pb_line(6, !vblank);
        cpu_.run(kCyclesPerLine);

        if (!vblank && snd_enable_) {
            const uint32_t snd_off = kRamSize - (main_sound_ ? 0x0300u : 0x5F00u);
            const uint8_t sample = ram_[(snd_off + uint32_t(line - kVBlankLines) * 2) & (kRamSize - 1)];
            int32_t s = (int32_t(sample) - 128) * (snd_vol_ + 1) * 32;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            audio_.push_back(int16_t(s));
        } else {
            audio_.push_back(0);
        }
    }
    render();
}

void MacPlus::set_inputs(const MachineInputs& inputs) {
    mouse_button_ = inputs.pointer_button1;
    if (!inputs.has_pointer) return;
    if (!pointer_seen_) {
        last_pointer_x_ = inputs.pointer_x;
        last_pointer_y_ = inputs.pointer_y;
        pointer_seen_ = true;
        return;
    }
    int dx = inputs.pointer_x - last_pointer_x_;
    int dy = inputs.pointer_y - last_pointer_y_;
    last_pointer_x_ = inputs.pointer_x;
    last_pointer_y_ = inputs.pointer_y;
    auto pulse = [this](int ch, int dir) {
        if (!dir) return;
        scc_dcd_[ch] = !scc_dcd_[ch];
        mouse_last_[ch] = uint8_t(scc_dcd_[ch]);
        if (dir < 0)
            mouse_bit_[ch] = uint8_t(mouse_last_[ch] ? 0 : 1);
        else
            mouse_bit_[ch] = mouse_last_[ch];
        if (scc_wr1_[ch] & 0x01) {
            scc_irq_ = true;
            update_irqs();
        }
    };
    // One quadrature step per axis per frame keeps the ROM's mouse ISR happy.
    if (dx) pulse(0, dx > 0 ? 1 : -1);
    if (dy) pulse(1, dy > 0 ? 1 : -1);
}

void MacPlus::set_dip_switch(int, uint8_t) {}

void MacPlus::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
