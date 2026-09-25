#include "drivers/computers/amiga.h"

#include <algorithm>
#include <cstring>

#include "core/rom_loader.h"

namespace dsp {
namespace {

const std::vector<RomEntry> kKick13 = {{"315093-02.u2", 0x40000, 0x0000, 0xc4f0f55f}};
const std::vector<RomEntry> kKick12 = {{"315093-01.u2", 0x40000, 0x0000, 0xa6ce1636}};
const std::vector<RomEntry> kKick204 = {{"390979-01.u2", 0x80000, 0x0000, 0xc3bdb240}};
const std::vector<RomEntry> kKick31 = {{"kick40063.u2", 0x80000, 0x0000, 0xfc24ae0d}};
const std::vector<RomEntry> kKickRom = {{"kick.rom", 0x40000, 0x0000, 0}};
const std::vector<RomEntry> kKickstart = {{"kickstart.rom", 0x40000, 0x0000, 0}};

}  // namespace

Amiga500::Amiga500() : cpu_(kCpuClock) {
    chip_.assign(kChipSize, 0);
    rom_.assign(0x40000, 0xFF);
    cpu_.set_memory_handlers([this](uint32_t a) { return read_word(a); },
                             [this](uint32_t a, uint16_t v) { write_word(a, v); });
    cpu_.set_byte_handlers([this](uint32_t a) { return read_byte(a); },
                           [this](uint32_t a, uint8_t v) { write_byte(a, v); });
    cpu_.set_cycle_handler([this](int c) { on_cpu_cycles(c); });
    cpu_.set_reset_instruction_handler([this]() {
        ciaa_.reset();
        ciab_.reset();
        chipset_.reset();
        prev_prb_ = 0xFF;
        motor_ = false;
        selected_ = false;
        update_ipl();
    });
    chipset_.set_chip_handlers([this](uint32_t a) { return chip_word(a); },
                               [this](uint32_t a, uint16_t v) { poke_chip_word(a, v); });
    chipset_.set_track_mfm([this]() -> std::vector<uint16_t> {
        if (!floppy_.loaded() || !selected_ || !motor_) return {};
        int c = cyl_;
        if (c < 0) c = 0;
        if (c >= floppy_.tracks()) c = floppy_.tracks() - 1;
        return floppy_.encode_track(c, side_);
    });
    ciaa_.set_port_a([this]() { return cia_a_pra_in(); }, nullptr);
    chipset_.set_joytest_handler([this](uint16_t v) {
        mouse_x_ = uint8_t((mouse_x_ & 0x03) | (v & 0xfc));
        mouse_y_ = uint8_t((mouse_y_ & 0x03) | ((v >> 8) & 0xfc));
        update_joy0();
    });
    ciaa_.set_irq_handler([this](bool v) {
        chipset_.set_ciaa_irq(v);
        update_ipl();
    });
    ciab_.set_port_b(nullptr, [this](uint8_t v) { cia_b_floppy(v); });
    ciab_.set_irq_handler([this](bool v) {
        chipset_.set_ciab_irq(v);
        update_ipl();
    });
}

bool Amiga500::init(const std::string& rom_path, std::string* error) {
    RomLoader loader;
    if (!loader.open(rom_path, error)) return false;
    std::string ignored;
    rom_.assign(0x80000, 0xFF);
    bool ok = loader.load(kKick13, rom_, &ignored);
    if (ok) rom_.resize(0x40000);
    if (!ok) {
        rom_.assign(0x40000, 0xFF);
        ok = loader.load(kKick12, rom_, &ignored);
    }
    if (!ok) {
        rom_.assign(0x40000, 0xFF);
        ok = loader.load(kKickRom, rom_, &ignored) || loader.load(kKickstart, rom_, &ignored);
        if (ok && rom_[0] == 0x11 && rom_[1] == 0x11) {
            // Kickstart 1.x ident
        } else if (ok && rom_.size() == 0x40000 && rom_[0] != 0x11) {
            ok = false;
        }
    }
    if (!ok) {
        rom_.assign(0x80000, 0xFF);
        ok = loader.load(kKick204, rom_, &ignored) || loader.load(kKick31, rom_, error);
        if (!ok && error && error->empty()) *error = "Amiga Kickstart ROM not found in " + rom_path;
        if (!ok) return false;
    }
    warnings_.insert(warnings_.end(), loader.warnings().begin(), loader.warnings().end());
    reset();
    return true;
}

void Amiga500::reset() {
    std::fill(chip_.begin(), chip_.end(), 0);
    framebuffer_.fill(0);
    cyl_ = 0;
    side_ = 0;
    motor_ = false;
    selected_ = false;
    disk_changed_ = true;
    prev_prb_ = 0xFF;
    prb_writes_ = 0;
    step_in_ = 0;
    step_out_ = 0;
    max_cyl_ = 0;
    cia_acc_ = 0;
    index_div_ = 0;
    audio_acc_ = 0;
    audio_.clear();
    ciaa_.reset();
    ciab_.reset();
    chipset_.reset();
    cpu_.reset();
    update_ipl();
}

bool Amiga500::load_media(const std::string& path, std::string* error) {
    if (!floppy_.load_file(path, error)) return false;
    disk_changed_ = true;
    return true;
}

bool Amiga500::overlay() const {
    return !((ciaa_.ddra() & 1) && !(ciaa_.pra() & 1));
}

uint8_t Amiga500::cia_a_pra_in() const {
    // bit2 /CHNG, bit3 /WPRO, bit4 /TK0, bit5 /RDY  (active low)
    uint8_t v = 0xFF;
    if (lmb_) v = uint8_t(v & ~0x40);    // /FIR0: left mouse button
    if (fire1_) v = uint8_t(v & ~0x80);  // /FIR1: joystick fire (port 1)
    if (!floppy_.loaded() || disk_changed_) v = uint8_t(v & ~0x04);
    if (cyl_ == 0) v = uint8_t(v & ~0x10);
    // /RDY is only driven while the drive is selected. With /MTR high
    // (motor latched off) Kickstart 1.3 disk.resource bit-bangs a 32-bit
    // GetUnitID on this pin; a 3.5" DD drive shifts all zeros, so RDY
    // stays low. With the motor latched on, RDY means "disk spinning" and
    // is independent of the /CHNG latch.
    if (selected_) {
        const bool ready = motor_ ? floppy_.loaded() : true;
        if (ready) v = uint8_t(v & ~0x20);
    }
    return v;
}

void Amiga500::cia_b_floppy(uint8_t prb) {
    // bit0 /STEP, bit1 DIR, bit2 /SIDE, bit3 /SEL0, bit7 /MTR
    // /MTR is sampled and latched on the falling edge of /SELx — it is not
    // combinational. Deselecting must not stop a spinning drive.
    ++prb_writes_;
    const bool sel0 = (prb & 0x08) == 0;
    const bool mtr_line = (prb & 0x80) == 0;
    side_ = (prb & 0x04) ? 0 : 1;
    if (sel0 && !selected_) {
        motor_ = mtr_line;
    }
    selected_ = sel0;
    if (selected_ && ((prev_prb_ & 1) != 0) && ((prb & 1) == 0)) {
        // HRM: DSKDIREC 0 = towards the spindle (higher cylinders); 1 = towards track 0.
        if (prb & 2) {
            if (cyl_ > 0) cyl_--;
            ++step_in_;
        } else {
            if (cyl_ < 82) cyl_++;
            ++step_out_;
        }
        if (cyl_ > max_cyl_) max_cyl_ = cyl_;
        if (floppy_.loaded()) disk_changed_ = false;
    }
    prev_prb_ = prb;
}

uint16_t Amiga500::chip_word(uint32_t address) const {
    address &= kChipSize - 1;
    return uint16_t((chip_[address] << 8) | chip_[(address + 1) & (kChipSize - 1)]);
}

void Amiga500::poke_chip_word(uint32_t address, uint16_t value) {
    address &= kChipSize - 1;
    chip_[address] = uint8_t(value >> 8);
    chip_[(address + 1) & (kChipSize - 1)] = uint8_t(value);
}

uint8_t Amiga500::read_byte(uint32_t address) {
    address &= 0x00FFFFFFu;
    if (address < 0x200000u) {
        if (overlay() && address < rom_.size()) return rom_[address];
        return chip_[address & (kChipSize - 1)];
    }
    if (address >= 0x00BF0000u && address <= 0x00BFFFFFu) {
        // CIA-A is selected by A12 low and sits on the odd (low) byte, CIA-B
        // by A13 low on the even byte. A long read of $BFDD00 (CIA-B ICR)
        // must not touch CIA-A's ICR at $BFDD01: A12 is high there.
        const uint8_t reg = uint8_t((address >> 8) & 0x0F);
        if (address & 1) return (address & 0x1000) ? uint8_t(0xFF) : ciaa_.read(reg);
        return (address & 0x2000) ? uint8_t(0xFF) : ciab_.read(reg);
    }
    if (address >= 0x00DFF000u && address <= 0x00DFFFFFu) {
        const uint16_t w = chipset_.read(uint16_t(address & 0x1FE));
        return (address & 1) ? uint8_t(w) : uint8_t(w >> 8);
    }
    if (address >= 0x00F80000u) {
        const uint32_t off = address - 0x00F80000u;
        if (rom_.size() == 0x40000) {
            if (address >= 0x00FC0000u) return rom_[address - 0x00FC0000u];
            return rom_[off & 0x3FFFFu];
        }
        return rom_[off & (rom_.size() - 1)];
    }
    if (address >= 0x00E80000u && address < 0x00F80000u) {
        return 0xFF;  // no Zorro board / empty diagnostic slot
    }
    return 0xFF;
}

void Amiga500::write_byte(uint32_t address, uint8_t value) {
    address &= 0x00FFFFFFu;
    if (address < 0x200000u) {
        chip_[address & (kChipSize - 1)] = value;
        return;
    }
    if (address >= 0x00BF0000u && address <= 0x00BFFFFFu) {
        const uint8_t reg = uint8_t((address >> 8) & 0x0F);
        if (address & 1) {
            if (!(address & 0x1000)) ciaa_.write(reg, value);
        } else if (!(address & 0x2000)) {
            ciab_.write(reg, value);
        }
        return;
    }
    if (address >= 0x00DFF000u && address <= 0x00DFFFFFu) {
        const uint16_t r = uint16_t(address & 0x1FE);
        uint16_t old = chipset_.read(r);
        if (address & 1)
            chipset_.write(r, uint16_t((old & 0xFF00) | value));
        else
            chipset_.write(r, uint16_t((uint16_t(value) << 8) | (old & 0x00FF)));
        update_ipl();
    }
}

uint16_t Amiga500::read_word(uint32_t address) {
    address &= 0x00FFFFFEu;
    if (address >= 0x00DFF000u && address <= 0x00DFFFFFu) {
        return chipset_.read(uint16_t(address & 0x1FE));
    }
    return uint16_t((read_byte(address) << 8) | read_byte(address + 1));
}

void Amiga500::write_word(uint32_t address, uint16_t value) {
    address &= 0x00FFFFFEu;
    if (address >= 0x00DFF000u && address <= 0x00DFFFFFu) {
        chipset_.write(uint16_t(address & 0x1FE), value);
        update_ipl();
        return;
    }
    write_byte(address, uint8_t(value >> 8));
    write_byte(address + 1, uint8_t(value));
}

void Amiga500::on_cpu_cycles(int cycles) {
    cia_acc_ += cycles;
    while (cia_acc_ >= 10) {
        ciaa_.tick(1);
        ciab_.tick(1);
        cia_acc_ -= 10;
    }
    audio_acc_ += int64_t(cycles) * kSampleRate;
    while (audio_acc_ >= kCpuClock) {
        audio_.push_back(0);
        audio_acc_ -= kCpuClock;
    }
}

void Amiga500::update_ipl() {
    chipset_.set_ciaa_irq(ciaa_.irq());
    chipset_.set_ciab_irq(ciab_.irq());
    const int ipl = chipset_.ipl();
    for (int level = 1; level <= 7; level++) {
        cpu_.set_irq(level, level == ipl ? IrqLine::Assert : IrqLine::Clear);
    }
}

void Amiga500::run_frame() {
    chipset_.begin_frame();
    update_ipl();
    ciaa_.tod_tick();
    // Index is generated by a spinning drive, even while /SEL is high.
    if (motor_) {
        index_div_++;
        if (index_div_ >= 10) {
            index_div_ = 0;
            ciaa_.pulse_flag();
        }
    }
    for (int line = 0; line < kLines; line++) {
        chipset_.set_vpos(line);
        chipset_.copper_line(line);
        chipset_.render_line(framebuffer_.data(), line);
        ciab_.tod_tick();  // CIA-B TOD is HSYNC
        cpu_.run(kCyclesPerLine);
        update_ipl();
    }
    chipset_.render(framebuffer_.data());
}

void Amiga500::update_joy0() { chipset_.set_joy0dat(uint16_t((mouse_y_ << 8) | mouse_x_)); }

void Amiga500::set_inputs(const MachineInputs& inputs) {
    // Joystick in port 1 (JOY1DAT: right = bit1, left = bit9, up/down are
    // XORed with them into bits 8/0).
    {
        const bool r = inputs.player1.right, l = inputs.player1.left;
        const bool u = inputs.player1.up, d = inputs.player1.down;
        uint16_t j = 0;
        if (r) j = uint16_t(j | 0x0002);
        if (l) j = uint16_t(j | 0x0200);
        if (d != r) j = uint16_t(j | 0x0001);
        if (u != l) j = uint16_t(j | 0x0100);
        chipset_.set_joy1dat(j);
        fire1_ = inputs.player1.button1;
    }
    if (!inputs.has_pointer) {
        pointer_seen_ = false;  // re-seed when the pointer comes back
        seed_valid_ = false;
        sync_frames_ = 0;
        lmb_ = false;
        chipset_.set_right_button(false);
        return;
    }
    lmb_ = inputs.pointer_button1;
    chipset_.set_right_button(inputs.pointer_button2);
    // One mouse count per lores pixel on both axes (the pointer sprite moves
    // one lores pixel per count).
    if (inputs.pointer_resync) {
        // The mouse came back into the window: line up again on its next move.
        pointer_seen_ = false;
        seed_valid_ = false;
        sync_frames_ = 0;
    }
    int dx = 0, dy = 0;
    if (sync_frames_ > 0) {
        // Lining the pointer up: a few frames of full-speed motion up and
        // left pin it in the corner (programs clamp there), then it moves to
        // where the host pointer is.
        mouse_x_ = uint8_t(mouse_x_ - kMaxCountsPerFrame);
        mouse_y_ = uint8_t(mouse_y_ - kMaxCountsPerFrame);
        update_joy0();
        if (--sync_frames_ == 0) {
            pend_x_ = inputs.pointer_x;
            pend_y_ = inputs.pointer_y;
            last_px_ = inputs.pointer_x;
            last_py_ = inputs.pointer_y;
        }
        return;
    }
    if (inputs.pointer_relative) {
        dx = inputs.pointer_dx;
        dy = inputs.pointer_dy;
    } else {
        const int x = inputs.pointer_x, y = inputs.pointer_y;
        if (!pointer_seen_) {
            // Wait for the first real movement (the OS or game may still be
            // starting), then line the pointer up with the host one.
            if (!seed_valid_ || (x == seed_x_ && y == seed_y_)) {
                seed_valid_ = true;
                seed_x_ = x;
                seed_y_ = y;
                return;
            }
            pointer_seen_ = true;
            pend_x_ = pend_y_ = 0;
            sync_frames_ = 12;  // 12 x 60 counts: past any edge
            return;
        }
        dx = x - last_px_;
        dy = y - last_py_;
        last_px_ = x;
        last_py_ = y;
        // The host pointer stops at the window edge; keep pushing so the
        // Amiga pointer (clamped by the OS or the game) stops on that edge
        // too and both line up again.
        if (x <= 0) dx -= 16;
        if (x >= AmigaChipset::kWidth - 1) dx += 16;
        if (y <= 0) dy -= 16;
        if (y >= AmigaChipset::kHeight - 1) dy += 16;
    }
    // The counters are 8 bits: more than 127 counts between two reads wrap
    // into the opposite direction. Games often read them only every other
    // frame (25 Hz), so at most 60 counts are added per frame and a fast
    // flick is spread over the following frames.
    pend_x_ += dx;
    pend_y_ += dy;
    const int sx = std::clamp(pend_x_, -kMaxCountsPerFrame, kMaxCountsPerFrame);
    const int sy = std::clamp(pend_y_, -kMaxCountsPerFrame, kMaxCountsPerFrame);
    pend_x_ -= sx;
    pend_y_ -= sy;
    mouse_x_ = uint8_t(mouse_x_ + sx);
    mouse_y_ = uint8_t(mouse_y_ + sy);
    update_joy0();
}

void Amiga500::set_dip_switch(int, uint8_t) {}

void Amiga500::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
