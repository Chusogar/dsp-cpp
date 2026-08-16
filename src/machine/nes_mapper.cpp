#include "machine/nes_mapper.h"

#include <cstring>

namespace dsp {

void NesMapper::attach(uint8_t* cpu_mem, NesPpu* ppu, std::function<void(IrqLine)> irq) {
    cpu_mem_ = cpu_mem;
    ppu_ = ppu;
    irq_ = std::move(irq);
    if (ppu_) ppu_->set_chr_map(chr_map.data());
}

void NesMapper::add_cycles(int cycles) {
    // Pascal only hooks mapper_1_delay as the post-instruction callback.
    if (mapper == 1) counter_ += cycles;
}

void NesMapper::set_prg_16(uint16_t pos, int bank) {
    if (!cpu_mem_ || last_prg <= 0) return;
    const int tempb = bank % last_prg;
    std::memcpy(cpu_mem_ + pos, prg[size_t(tempb)].data(), 0x4000);
}

void NesMapper::set_prg_32(int bank) {
    if (last_prg <= 1) {
        set_prg_16(0x8000, bank);
        return;
    }
    const int tempb = (bank % (last_prg >> 1)) << 1;
    set_prg_16(0x8000, tempb);
    set_prg_16(0xc000, tempb | 1);
}

void NesMapper::set_prg_8(uint16_t pos, int bank) {
    if (!cpu_mem_ || last_prg <= 0) return;
    const int tempb = bank % (last_prg << 1);
    std::memcpy(cpu_mem_ + pos, &prg[size_t(tempb >> 1)][0x2000 * (tempb & 1)], 0x2000);
}

void NesMapper::set_chr_8(int bank) {
    if (!ppu_ || last_chr <= 0) return;
    const int tempb = bank % last_chr;
    std::memcpy(ppu_->chr_bank(chr_map[0] & 3), chr[size_t(tempb)].data(), 0x1000);
    std::memcpy(ppu_->chr_bank(chr_map[1] & 3), chr[size_t(tempb)].data() + 0x1000, 0x1000);
}

void NesMapper::set_chr_4(uint16_t pos, int bank) {
    if (!ppu_ || last_chr <= 0) return;
    const int tempb = bank % (last_chr << 1);
    std::memcpy(ppu_->chr_bank(chr_map[(pos >> 12) & 1] & 3),
                &chr[size_t(tempb >> 1)][0x1000 * (tempb & 1)], 0x1000);
}

void NesMapper::set_chr_2(uint16_t pos, int bank) {
    if (!ppu_ || last_chr <= 0) return;
    const int tempb = bank % (last_chr << 2);
    std::memcpy(ppu_->chr_bank(chr_map[(pos >> 12) & 1] & 3) + 0x800 * ((pos >> 11) & 1),
                &chr[size_t(tempb >> 2)][0x800 * (tempb & 3)], 0x800);
}

void NesMapper::set_chr_1(uint16_t pos, int bank) {
    if (!ppu_ || last_chr <= 0) return;
    const int tempb = bank % (last_chr << 3);
    std::memcpy(ppu_->chr_bank(chr_map[(pos >> 12) & 1] & 3) + 0x400 * ((pos >> 10) & 3),
                &chr[size_t(tempb >> 3)][0x400 * (tempb & 7)], 0x400);
}

uint8_t NesMapper::default_prg_ram_read(uint16_t address) const {
    if (!prg_ram_enable || !cpu_mem_) return ppu_ ? ppu_->open_bus : 0;
    return cpu_mem_[address];
}

void NesMapper::default_prg_ram_write(uint16_t address, uint8_t value) {
    if (!prg_ram_writable || !prg_ram_enable || !cpu_mem_) return;
    cpu_mem_[address] = value;
}

uint8_t NesMapper::read_prg_ram(uint16_t address) const {
    if (read_prg_ram_) return (this->*read_prg_ram_)(address);
    return default_prg_ram_read(address);
}

void NesMapper::write_prg_ram(uint16_t address, uint8_t value) {
    if (write_prg_ram_) {
        (this->*write_prg_ram_)(address, value);
        return;
    }
    default_prg_ram_write(address, value);
}

uint8_t NesMapper::read_expansion(uint16_t address) const {
    if (read_expansion_) return (this->*read_expansion_)(address);
    return ppu_ ? ppu_->open_bus : 0;
}

void NesMapper::write_expansion(uint16_t address, uint8_t value) {
    if (write_expansion_) (this->*write_expansion_)(address, value);
}

uint8_t NesMapper::read_rom(uint16_t address) const {
    if (read_rom_) return (this->*read_rom_)(address);
    return cpu_mem_ ? cpu_mem_[address] : 0;
}

void NesMapper::write_rom(uint16_t address, uint8_t value) {
    if (write_rom_) (this->*write_rom_)(address, value);
}

void NesMapper::line_ack(bool force) {
    if (line_ack_) (this->*line_ack_)(force);
}

void NesMapper::ppu_read(uint16_t address) {
    if (ppu_read_) (this->*ppu_read_)(address);
}

void NesMapper::mapper1_chr() {
    if (!ppu_) return;
    if (regs_[0] & 0x10) {
        if (ppu_->write_chr) {
            chr_map[0] = regs_[1];
            chr_map[1] = regs_[2];
        } else {
            set_chr_4(0x0000, regs_[1]);
            set_chr_4(0x1000, regs_[2]);
        }
    } else if (ppu_->write_chr) {
        chr_map[0] = 0;
        chr_map[1] = 1;
    } else {
        set_chr_8((regs_[1] & 0x1f) >> 1);
    }
}

void NesMapper::mapper1_prg() {
    if (last_prg <= 0) return;
    const int extra = last_prg > 16 ? (regs_[1] & 0x10) : 0;
    const int tempb = ((regs_[3] & 0x0f) | extra) % last_prg;
    switch ((regs_[0] >> 2) & 3) {
        case 0:
        case 1:
            set_prg_16(0x8000, tempb & 0xfe);
            set_prg_16(0xc000, tempb | 1);
            break;
        case 2:
            set_prg_16(0x8000, extra);
            set_prg_16(0xc000, tempb);
            break;
        case 3:
            set_prg_16(0x8000, tempb);
            set_prg_16(0xc000, (0x0f | extra) % last_prg);
            break;
    }
    if (submapper == 0) prg_ram_enable = (regs_[3] & 0x10) == 0;
}

void NesMapper::mapper1_write(uint16_t address, uint8_t value) {
    if (counter_ < 2 && serial_cnt_ == 0) return;
    if (value & 0x80) {
        serial_cnt_ = 0;
        valor_map_ = 0;
        regs_[0] |= 0x0c;
        counter_ = 0;
        if (last_prg > 0) set_prg_16(0xc000, 0x0f % last_prg);
        return;
    }
    valor_map_ = uint8_t(valor_map_ | ((value & 1) << serial_cnt_));
    ++serial_cnt_;
    counter_ = 0;
    if (serial_cnt_ != 5) return;
    regs_[(address >> 13) & 3] = valor_map_;
    if (ppu_) {
        switch (regs_[0] & 3) {
            case 0: ppu_->mirror = NesPpu::Low; break;
            case 1: ppu_->mirror = NesPpu::High; break;
            case 2: ppu_->mirror = NesPpu::Vertical; break;
            case 3: ppu_->mirror = NesPpu::Horizontal; break;
        }
    }
    mapper1_chr();
    mapper1_prg();
    valor_map_ = 0;
    serial_cnt_ = 0;
}

void NesMapper::mapper2_write(uint16_t, uint8_t value) { set_prg_16(0x8000, value); }

void NesMapper::mapper3_write(uint16_t, uint8_t value) { set_chr_8(value & 0x03); }

void NesMapper::mapper4_update_chr(uint8_t value) {
    if (!ppu_ || ppu_->write_chr) return;
    const uint16_t base = uint16_t((value & 0x80) << 5);
    set_chr_1(uint16_t(0x0000 ^ base), dregs_[0] & 0xfe);
    set_chr_1(uint16_t(0x0400 ^ base), dregs_[0] | 1);
    set_chr_1(uint16_t(0x0800 ^ base), dregs_[1] & 0xfe);
    set_chr_1(uint16_t(0x0c00 ^ base), dregs_[1] | 1);
    set_chr_1(uint16_t(0x1000 ^ base), dregs_[2]);
    set_chr_1(uint16_t(0x1400 ^ base), dregs_[3]);
    set_chr_1(uint16_t(0x1800 ^ base), dregs_[4]);
    set_chr_1(uint16_t(0x1c00 ^ base), dregs_[5]);
}

void NesMapper::mapper4_update_prg(uint8_t value) {
    if (last_prg <= 0) return;
    const int temp1 = dregs_[6] % (last_prg << 1);
    const int temp2 = dregs_[7] % (last_prg << 1);
    set_prg_8(0xa000, temp2);
    if ((value & 0x40) == 0) {
        set_prg_8(0x8000, temp1);
        set_prg_8(0xc000, (last_prg << 1) - 2);
    } else {
        set_prg_8(0x8000, (last_prg << 1) - 2);
        set_prg_8(0xc000, temp1);
    }
}

void NesMapper::mapper4_write(uint16_t address, uint8_t value) {
    switch (address & 0xe001) {
        case 0x8000:
            if ((value & 0x40) != (regs_[0] & 0x40)) mapper4_update_prg(value);
            if ((value & 0x80) != (regs_[0] & 0x80)) mapper4_update_chr(value);
            regs_[0] = value;
            break;
        case 0x8001:
            if ((regs_[0] & 7) < 2) value &= 0xfe;
            dregs_[regs_[0] & 7] = value;
            mapper4_update_prg(regs_[0]);
            mapper4_update_chr(regs_[0]);
            break;
        case 0xa000:
            if (ppu_ && ppu_->mirror != NesPpu::FourScreen) {
                ppu_->mirror = (value & 1) == 0 ? NesPpu::Vertical : NesPpu::Horizontal;
            }
            break;
        case 0xa001:
            prg_ram_enable = (value & 0x80) != 0;
            prg_ram_writable = (value & 0x40) == 0;
            regs_[3] = value;
            break;
        case 0xc000:
            regs_[2] = value;
            break;
        case 0xc001:
            reload_ = true;
            break;
        case 0xe000:
            irq_ena_ = false;
            if (irq_) irq_(IrqLine::Clear);
            break;
        case 0xe001:
            irq_ena_ = true;
            break;
        default:
            break;
    }
}

void NesMapper::mapper4_line(bool) {
    if (!ppu_ || (ppu_->control2 & 0x18) == 0) return;
    if (counter_ == 0 || reload_) {
        counter_ = regs_[2];
        reload_ = false;
    } else {
        --counter_;
    }
    if (counter_ == 0) {
        reload_ = true;
        if (irq_ena_ && irq_) irq_(IrqLine::Assert);
    }
}

void NesMapper::mapper7_write(uint16_t, uint8_t value) {
    set_prg_32(value & 0x0f);
    if (ppu_) ppu_->mirror = (value & 0x10) == 0 ? NesPpu::Low : NesPpu::High;
}

void NesMapper::mapper9_write(uint16_t address, uint8_t value) {
    switch (address >> 12) {
        case 0x0a:
            set_prg_8(0x8000, value & 0x0f);
            break;
        case 0x0b:
            regs_[0] = value & 0x1f;
            if (latch0_ == 0xfd) set_chr_4(0x0000, value & 0x1f);
            break;
        case 0x0c:
            regs_[1] = value & 0x1f;
            if (latch0_ == 0xfe) set_chr_4(0x0000, value & 0x1f);
            break;
        case 0x0d:
            regs_[2] = value & 0x1f;
            if (latch1_ == 0xfd) set_chr_4(0x1000, value & 0x1f);
            break;
        case 0x0e:
            regs_[3] = value & 0x1f;
            if (latch1_ == 0xfe) set_chr_4(0x1000, value & 0x1f);
            break;
        case 0x0f:
            if (ppu_) ppu_->mirror = (value & 1) ? NesPpu::Horizontal : NesPpu::Vertical;
            break;
        default:
            break;
    }
}

void NesMapper::mapper9_ppu(uint16_t address) {
    switch (address & 0x3ff0) {
        case 0x0fd0:
            latch0_ = 0xfd;
            set_chr_4(0x0000, regs_[0]);
            break;
        case 0x0fe0:
            latch0_ = 0xfe;
            set_chr_4(0x0000, regs_[1]);
            break;
        case 0x1fd0:
            latch1_ = 0xfd;
            set_chr_4(0x1000, regs_[2]);
            break;
        case 0x1fe0:
            latch1_ = 0xfe;
            set_chr_4(0x1000, regs_[3]);
            break;
        default:
            break;
    }
}

void NesMapper::mapper10_write(uint16_t address, uint8_t value) {
    switch ((address >> 12) & 7) {
        case 2:
            set_prg_16(0x8000, value & 0x0f);
            break;
        case 3:
            regs_[0] = value & 0x1f;
            if (latch0_ == 0xfd) set_chr_4(0x0000, value & 0x1f);
            break;
        case 4:
            regs_[1] = value & 0x1f;
            if (latch0_ == 0xfe) set_chr_4(0x0000, value & 0x1f);
            break;
        case 5:
            regs_[2] = value & 0x1f;
            if (latch1_ == 0xfd) set_chr_4(0x1000, value & 0x1f);
            break;
        case 6:
            regs_[3] = value & 0x1f;
            if (latch1_ == 0xfe) set_chr_4(0x1000, value & 0x1f);
            break;
        case 7:
            if (ppu_) ppu_->mirror = (value & 1) ? NesPpu::Horizontal : NesPpu::Vertical;
            break;
        default:
            break;
    }
}

void NesMapper::mapper11_write(uint16_t, uint8_t value) {
    set_prg_32(value & 0x03);
    if (last_chr != 0) set_chr_8(value >> 4);
}

void NesMapper::mapper66_write(uint16_t, uint8_t value) {
    set_chr_8(value & 0x03);
    set_prg_32((value & 0x30) >> 4);
}

void NesMapper::mapper71_write(uint16_t address, uint8_t value) {
    if (address >= 0x9000 && address <= 0x9fff) {
        if (ppu_) ppu_->mirror = (value & 0x10) ? NesPpu::High : NesPpu::Low;
    } else if (address >= 0xc000) {
        set_prg_16(0x8000, value & 0x0f);
    }
}

void NesMapper::mapper87_write(uint16_t, uint8_t value) {
    value = uint8_t((value >> 1) | ((value & 1) << 1));
    set_chr_8(value);
}

void NesMapper::mapper185_write(uint16_t, uint8_t value) {
    if (!ppu_) return;
    ppu_->disable_chr = true;
    if ((((value & 0x0f) != 0) && value != 0x13) || latch1_ == 0x21) {
        if (!((value == 0x21) && (latch1_ != 0x13))) {
            ppu_->disable_chr = false;
            set_chr_8(value & 0x03);
        }
    }
    latch1_ = value;
}

void NesMapper::mapper206_write(uint16_t address, uint8_t value) {
    address &= 0x8001;
    if (address == 0x8000) {
        regs_[0] = value & 7;
        return;
    }
    if (address != 0x8001) return;
    switch (regs_[0]) {
        case 0: set_chr_2(0x0000, (value >> 1) & 0x1f); break;
        case 1: set_chr_2(0x0800, (value >> 1) & 0x1f); break;
        case 2: set_chr_1(0x1000, value & 0x3f); break;
        case 3: set_chr_1(0x1400, value & 0x3f); break;
        case 4: set_chr_1(0x1800, value & 0x3f); break;
        case 5: set_chr_1(0x1c00, value & 0x3f); break;
        case 6: set_prg_8(0x8000, value & 0x0f); break;
        case 7: set_prg_8(0xa000, value & 0x0f); break;
        default: break;
    }
}

bool NesMapper::set_mapper(int mapper_num, int sub) {
    mapper = mapper_num;
    submapper = sub;
    write_rom_ = nullptr;
    read_rom_ = nullptr;
    write_prg_ram_ = nullptr;
    read_prg_ram_ = nullptr;
    write_expansion_ = nullptr;
    read_expansion_ = nullptr;
    line_ack_ = nullptr;
    ppu_read_ = nullptr;
    switch (mapper_num) {
        case 0:
            break;
        case 1:
            write_rom_ = &NesMapper::mapper1_write;
            break;
        case 2:
            write_rom_ = &NesMapper::mapper2_write;
            break;
        case 3:
            write_rom_ = &NesMapper::mapper3_write;
            break;
        case 4:
            write_rom_ = &NesMapper::mapper4_write;
            line_ack_ = &NesMapper::mapper4_line;
            break;
        case 7:
            write_rom_ = &NesMapper::mapper7_write;
            break;
        case 9:
            write_rom_ = &NesMapper::mapper9_write;
            ppu_read_ = &NesMapper::mapper9_ppu;
            break;
        case 10:
            write_rom_ = &NesMapper::mapper10_write;
            ppu_read_ = &NesMapper::mapper9_ppu;
            break;
        case 11:
            write_rom_ = &NesMapper::mapper11_write;
            break;
        case 66:
            write_rom_ = &NesMapper::mapper66_write;
            break;
        case 71:
            write_rom_ = &NesMapper::mapper71_write;
            break;
        case 87:
            write_prg_ram_ = &NesMapper::mapper87_write;
            break;
        case 185:
            write_rom_ = &NesMapper::mapper185_write;
            break;
        case 206:
            write_rom_ = &NesMapper::mapper206_write;
            break;
        default:
            return false;
    }
    return true;
}

void NesMapper::reset() {
    prg_ram_writable = false;
    prg_ram_enable = false;
    latch0_ = 0;
    latch1_ = 0;
    reload_ = false;
    counter_ = 0;
    irq_ena_ = false;
    serial_cnt_ = 0;
    valor_map_ = 0;
    dregs_.fill(0);
    regs_.fill(0);
    chr_map[0] = 0;
    chr_map[1] = 1;
    switch (mapper) {
        case 1:
            prg_ram_writable = true;
            regs_[0] = 0x0c;
            set_prg_16(0x8000, 0);
            set_prg_16(0xc000, last_prg - 1);
            break;
        case 2:
            set_prg_16(0x8000, 0);
            set_prg_16(0xc000, last_prg - 1);
            break;
        case 4:
            prg_ram_writable = true;
            prg_ram_enable = true;
            dregs_[1] = 2;
            dregs_[2] = 4;
            dregs_[3] = 5;
            dregs_[4] = 6;
            dregs_[5] = 7;
            dregs_[7] = 1;
            mapper4_update_chr(0);
            mapper4_update_prg(0);
            set_prg_16(0x8000, last_prg - 1);
            set_prg_16(0xc000, last_prg - 1);
            break;
        case 7:
            set_prg_32(0);
            break;
        case 9:
        case 10:
            set_prg_8(0x8000, 0);
            set_prg_8(0xa000, (last_prg << 1) - 3);
            set_prg_8(0xc000, (last_prg << 1) - 2);
            set_prg_8(0xe000, (last_prg << 1) - 1);
            set_chr_8(0);
            latch0_ = 0xfe;
            latch1_ = 0xfe;
            break;
        case 11:
            set_prg_32(0);
            if (last_chr != 0) set_chr_8(0);
            break;
        case 66:
            set_prg_32(0);
            if (last_chr != 0) set_chr_8(0);
            break;
        case 71:
            if (last_prg > 0) set_prg_16(0xc000, last_prg - 1);
            break;
        case 206:
            set_prg_16(0x8000, 0);
            set_prg_16(0xc000, last_prg - 1);
            break;
        default:
            break;
    }
}

}  // namespace dsp
