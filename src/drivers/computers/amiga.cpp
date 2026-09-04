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
    if (!floppy_.loaded() || disk_changed_) v = uint8_t(v & ~0x04);
    if (cyl_ == 0) v = uint8_t(v & ~0x10);
    const bool ready = floppy_.loaded() && motor_ && selected_ && !disk_changed_;
    if (ready) v = uint8_t(v & ~0x20);
    return v;
}

void Amiga500::cia_b_floppy(uint8_t prb) {
    // bit0 /STEP, bit1 DIR, bit2 /SIDE, bit3 /SEL0, bit7 /MTR
    selected_ = (prb & 0x08) == 0;
    motor_ = (prb & 0x80) == 0;
    side_ = (prb & 0x04) ? 0 : 1;
    if (selected_ && ((prev_prb_ & 1) != 0) && ((prb & 1) == 0)) {
        if (prb & 2) {
            if (cyl_ < 82) cyl_++;
        } else {
            if (cyl_ > 0) cyl_--;
        }
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
        const uint8_t reg = uint8_t((address >> 8) & 0x0F);
        if (address & 1) return ciaa_.read(reg);
        return ciab_.read(reg);
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
        if (address & 1)
            ciaa_.write(reg, value);
        else
            ciab_.write(reg, value);
        return;
    }
    if (address >= 0x00DFF000u && address <= 0x00DFFFFFu) {
        const uint16_t r = uint16_t(address & 0x1FE);
        uint16_t old = chipset_.read(r);
        if (address & 1)
            chipset_.write(r, uint16_t((old & 0xFF00) | value));
        else
            chipset_.write(r, uint16_t((uint16_t(value) << 8) | (old & 0x00FF)));
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
    if (motor_ && selected_) {
        index_div_++;
        if (index_div_ >= 10) {
            index_div_ = 0;
            ciaa_.pulse_flag();
        }
    }
    for (int line = 0; line < kLines; line++) {
        chipset_.set_vpos(line);
        chipset_.copper_line(line);
        ciab_.tod_tick();  // CIA-B TOD is HSYNC
        cpu_.run(kCyclesPerLine);
        update_ipl();
    }
    chipset_.render(framebuffer_.data());
}

void Amiga500::set_inputs(const MachineInputs&) {}

void Amiga500::set_dip_switch(int, uint8_t) {}

void Amiga500::drain_audio(std::vector<int16_t>& out) {
    out.insert(out.end(), audio_.begin(), audio_.end());
    audio_.clear();
}

}  // namespace dsp
