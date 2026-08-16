#include "cpu/tms7000.h"

#include <algorithm>

namespace dsp {
namespace {

const uint8_t kBcdOut[6] = {0x00, 0x06, 0x00, 0x66, 0x60, 0x66};

}  // namespace

Tms7000::Tms7000(uint32_t clock, Chip chip, unsigned divider)
    : clock_(clock), divider_(std::max(1u, divider)), chip_(chip) {
    switch (chip_) {
        case Chip::Tms7042:
            ram_.assign(0x100, 0);
            rom_base_ = 0xf000;
            family_70x2_ = true;
            break;
        case Chip::Tms7041:
            ram_.assign(0x80, 0);
            rom_base_ = 0xf000;
            family_70x2_ = true;
            break;
        case Chip::Tms7040:
            ram_.assign(0x80, 0);
            rom_base_ = 0xf000;
            break;
        case Chip::Tms7020:
            ram_.assign(0x80, 0);
            rom_base_ = 0xf800;
            break;
        case Chip::Tms7000:
        default:
            ram_.assign(0x80, 0);
            rom_base_ = 0;
            break;
    }
    port_ddr_[1] = 0xff;
}

void Tms7000::set_memory_handlers(ReadHandler read, WriteHandler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

void Tms7000::set_port_in(int port, PortInHandler handler) {
    if (port >= 0 && port < 4) port_in_[size_t(port)] = std::move(handler);
}

void Tms7000::set_port_out(int port, PortOutHandler handler) {
    if (port >= 0 && port < 4) port_out_[size_t(port)] = std::move(handler);
}

void Tms7000::set_internal_rom(const uint8_t* data, size_t size) {
    rom_.assign(data, data + size);
}

uint8_t Tms7000::port_in(int port) {
    if (port < 0 || port > 3) return 0xff;
    return port_in_[size_t(port)] ? port_in_[size_t(port)]() : 0xff;
}

void Tms7000::port_out(int port, uint8_t data) {
    if (port < 0 || port > 3) return;
    if (port_out_[size_t(port)]) port_out_[size_t(port)](data);
}

uint8_t Tms7000::read_mem8(uint16_t address) {
    if (address < ram_.size()) return ram_[address];
    if (address < 0x0100) return 0;
    if (address < 0x010c) return pf_read(uint8_t(address - 0x0100));
    if (family_70x2_ && address >= 0x0110 && address < 0x0118) {
        return pf_read(uint8_t(address - 0x0100));
    }
    if (!rom_.empty() && address >= rom_base_) {
        const size_t offset = size_t(address - rom_base_);
        if (offset < rom_.size()) return rom_[offset];
    }
    return read_ ? read_(address) : 0xff;
}

void Tms7000::write_mem8(uint16_t address, uint8_t data) {
    if (address < ram_.size()) {
        ram_[address] = data;
        return;
    }
    if (address < 0x0100) return;
    if (address < 0x010c) {
        pf_write(uint8_t(address - 0x0100), data);
        return;
    }
    if (family_70x2_ && address >= 0x0110 && address < 0x0118) {
        pf_write(uint8_t(address - 0x0100), data);
        return;
    }
    if (!rom_.empty() && address >= rom_base_) {
        const size_t offset = size_t(address - rom_base_);
        if (offset < rom_.size()) return;
    }
    if (write_) write_(address, data);
}

uint16_t Tms7000::read_mem16(uint16_t address) {
    const uint8_t hi = read_mem8(address);
    const uint8_t lo = read_mem8(uint16_t(address + 1));
    return uint16_t((hi << 8) | lo);
}

void Tms7000::write_mem16(uint16_t address, uint16_t data) {
    write_mem8(address, uint8_t(data >> 8));
    write_mem8(uint16_t(address + 1), uint8_t(data));
}

uint16_t Tms7000::read_r16(uint8_t address) {
    return uint16_t((read_r8(uint8_t(address - 1)) << 8) | read_r8(address));
}

void Tms7000::write_r16(uint8_t address, uint16_t data) {
    write_r8(uint8_t(address - 1), uint8_t(data >> 8));
    write_r8(address, uint8_t(data));
}

uint8_t Tms7000::imm8() { return read_mem8(pc_++); }

uint16_t Tms7000::imm16() {
    const uint8_t hi = imm8();
    return uint16_t((hi << 8) | imm8());
}

uint8_t Tms7000::pull8() { return read_mem8(sp_--); }

void Tms7000::push8(uint8_t data) { write_mem8(++sp_, data); }

uint16_t Tms7000::pull16() {
    const uint8_t lo = pull8();
    return uint16_t(lo | (pull8() << 8));
}

void Tms7000::push16(uint16_t data) {
    push8(uint8_t(data >> 8));
    push8(uint8_t(data));
}

void Tms7000::set_nz(uint16_t value) {
    sr_ = uint8_t((sr_ & 0x9f) | ((value >> 1) & 0x40) | ((value & 0xff) ? 0 : 0x20));
}

void Tms7000::set_c(uint16_t value) {
    sr_ = uint8_t((sr_ & 0x7f) | ((value >> 1) & 0x80));
}

void Tms7000::set_cnz(uint16_t value) {
    sr_ = uint8_t((sr_ & 0x1f) | ((value >> 1) & 0xc0) | ((value & 0xff) ? 0 : 0x20));
}

void Tms7000::reset() {
    if (idle_state_) {
        pc_++;
        idle_state_ = false;
    }
    irq_state_[0] = irq_state_[1] = false;
    std::fill(std::begin(io_control_), std::end(io_control_), 0);
    std::fill(std::begin(port_latch_), std::end(port_latch_), 0);
    std::fill(std::begin(port_ddr_), std::end(port_ddr_), 0);
    port_ddr_[1] = 0xff;
    std::fill(std::begin(timer_data_), std::end(timer_data_), 0);
    std::fill(std::begin(timer_control_), std::end(timer_control_), 0);
    std::fill(std::begin(timer_decrementer_), std::end(timer_decrementer_), 0);
    std::fill(std::begin(timer_prescaler_), std::end(timer_prescaler_), 0);
    std::fill(std::begin(timer_capture_latch_), std::end(timer_capture_latch_), 0);
    std::fill(std::begin(timer_crystal_acc_), std::end(timer_crystal_acc_), 0);
    std::fill(ram_.begin(), ram_.end(), 0);

    write_p(0x04, 0xff);
    write_p(0x06, 0xff);
    write_p(0x05, 0x00);
    write_p(0x09, 0x00);
    write_p(0x0b, 0x00);
    write_p(0x08, 0xff);
    write_p(0x0a, 0xff);

    sr_ = 0;
    write_p(0x00, 0x00);
    if (family_70x2_) write_p(0x10, 0x00);
    sp_ = 0xff;
    idle_state_ = false;
    execute_one(0xff);
    icount_ -= 3;
}

void Tms7000::set_input_line(int extline, IrqLine state) {
    if (extline != kInt1 && extline != kInt3) return;
    const bool pulse = (state == IrqLine::Hold || state == IrqLine::Pulse);
    const bool irqstate = (state != IrqLine::Clear);
    if (irqstate && !irq_state_[extline]) {
        irq_state_[extline] = true;
        io_control_[0] |= uint8_t(0x02 << (4 * extline));
        if (extline == kInt3) timer_capture_latch_[0] = uint16_t(timer_decrementer_[0]);
        check_interrupts();
    }
    if (!irqstate || pulse) {
        // INT1/INT3 flags are latched in IOCNT0; the pin going idle must not
        // drop a pending request the BIOS has not enabled yet.
        irq_state_[extline] = false;
    }
}

void Tms7000::flag_ext_interrupt(int extline) {
    if (extline != kInt1 && extline != kInt3) return;
    if (irq_state_[extline])
        io_control_[0] |= uint8_t(0x02 << (4 * extline));
    else
        io_control_[0] &= uint8_t(~(0x02 << (4 * extline)));
}

void Tms7000::check_interrupts() {
    if (!(sr_ & kSrI)) return;
    for (int irqline = 0; irqline < 5; irqline++) {
        const int shift = (irqline > 2) ? irqline * 2 - 6 : irqline * 2;
        const int bank = irqline > 2 ? 1 : 0;
        if (((io_control_[bank] >> shift) & 3) == 3) {
            io_control_[bank] &= uint8_t(~(0x02 << shift));
            do_interrupt(irqline);
            return;
        }
    }
}

void Tms7000::do_interrupt(int irqline) {
    if (idle_state_) {
        icount_ -= 17;
        pc_++;
        idle_state_ = false;
    } else {
        icount_ -= 19;
    }
    push8(sr_);
    push16(pc_);
    sr_ = 0;
    pc_ = read_mem16(uint16_t(0xfffc - irqline * 2));
}

void Tms7000::timer_run(int tmr) {
    timer_prescaler_[tmr] = timer_control_[tmr] & 0x1f;
    if ((timer_control_[tmr] & 0xe0) == 0x80) {
        timer_crystal_acc_[tmr] = 0;
    }
}

void Tms7000::timer_reload(int tmr) {
    timer_crystal_acc_[tmr] = 0;
    if (timer_control_[tmr] & 0x80) {
        timer_decrementer_[tmr] = timer_data_[tmr];
        timer_run(tmr);
    }
}

void Tms7000::timer_tick_low(int tmr) {
    if (--timer_decrementer_[tmr] < 0) {
        timer_reload(tmr);
        io_control_[tmr] |= 0x08;
        if (tmr == 0 && (timer_control_[1] & 0xa0) == 0xa0) {
            if (--timer_prescaler_[1] < 0) {
                timer_prescaler_[1] = timer_control_[1] & 0x1f;
                timer_tick_low(1);
            }
        }
        check_interrupts();
    }
}

void Tms7000::tick_timers(int cpu_cycles) {
    const int crystal = cpu_cycles * int(divider_);
    for (int tmr = 0; tmr < 2; tmr++) {
        if ((timer_control_[tmr] & 0xe0) != 0x80) continue;
        const int period = 16 * ((timer_control_[tmr] & 0x1f) + 1);
        timer_crystal_acc_[tmr] += crystal;
        while (timer_crystal_acc_[tmr] >= period) {
            timer_crystal_acc_[tmr] -= period;
            timer_tick_low(tmr);
        }
    }
}

uint8_t Tms7000::pf_read(uint8_t offset) {
    switch (offset) {
        case 0x00:
        case 0x10:
            return io_control_[offset >> 4];
        case 0x02:
        case 0x12:
            return uint8_t(timer_decrementer_[offset >> 4]);
        case 0x03:
            return uint8_t(timer_capture_latch_[0]);
        case 0x04:
        case 0x06:
        case 0x08:
        case 0x0a: {
            const int port = offset / 2 - 2;
            return uint8_t((port_in(port) & ~port_ddr_[port]) | (port_latch_[port] & port_ddr_[port]));
        }
        case 0x05:
        case 0x09:
        case 0x0b:
            return port_ddr_[offset / 2 - 2];
        default:
            return 0;
    }
}

void Tms7000::pf_write(uint8_t offset, uint8_t data) {
    switch (offset) {
        case 0x00:
            io_control_[0] = uint8_t((io_control_[0] & (~data & 0x2a)) | (data & 0xd5));
            if (data & 0x02) flag_ext_interrupt(kInt1);
            if (data & 0x20) flag_ext_interrupt(kInt3);
            check_interrupts();
            break;
        case 0x10:
            io_control_[1] = uint8_t((io_control_[1] & (~data & 0x0a)) | (data & 0x05));
            check_interrupts();
            break;
        case 0x02:
        case 0x12:
            timer_data_[offset >> 4] = data;
            break;
        case 0x03:
            data &= uint8_t(~0x20);
            [[fallthrough]];
        case 0x13:
            timer_control_[offset >> 4] = data;
            timer_reload(offset >> 4);
            break;
        case 0x04:
        case 0x06:
        case 0x08:
        case 0x0a: {
            if (!family_70x2_ && offset == 0x04) return;
            const int port = offset / 2 - 2;
            port_out(port, uint8_t(data & port_ddr_[port]));
            port_latch_[port] = data;
            break;
        }
        case 0x05:
        case 0x09:
        case 0x0b:
            if (!family_70x2_ && offset == 0x05) return;
            port_ddr_[offset / 2 - 2] = data;
            break;
        default:
            break;
    }
}

int Tms7000::run(int cycles) {
    if (cycles <= 0) return 0;
    icount_ = cycles;
    while (icount_ > 0) {
        check_interrupts();
        const int before = icount_;
        const uint8_t op = read_mem8(pc_++);
        execute_one(op);
        int used = before - icount_;
        if (used < 1) {
            consume(1);
            used = 1;
        }
        tick_timers(used);
        if (cycle_handler_) cycle_handler_(used);
    }
    return cycles - icount_;
}

void Tms7000::apply_write(uint16_t address, int result, bool to_p) {
    if (result > kWbNo) {
        if (to_p)
            write_p(uint8_t(address), uint8_t(result));
        else
            write_r8(uint8_t(address), uint8_t(result));
    }
}

void Tms7000::am_a(OpFunc op) {
    consume(5);
    apply_write(0, (this->*op)(read_r8(0), 0), false);
}
void Tms7000::am_b(OpFunc op) {
    consume(5);
    apply_write(1, (this->*op)(read_r8(1), 0), false);
}
void Tms7000::am_r(OpFunc op) {
    consume(7);
    const uint8_t r = imm8();
    apply_write(r, (this->*op)(read_r8(r), 0), false);
}
void Tms7000::am_a2a(OpFunc op) {
    consume(6);
    apply_write(0, (this->*op)(read_r8(0), read_r8(0)), false);
}
void Tms7000::am_a2b(OpFunc op) {
    consume(6);
    apply_write(1, (this->*op)(read_r8(1), read_r8(0)), false);
}
void Tms7000::am_a2p(OpFunc op) {
    consume(10);
    const uint8_t r = imm8();
    apply_write(r, (this->*op)(read_p(r), read_r8(0)), true);
}
void Tms7000::am_a2r(OpFunc op) {
    consume(8);
    const uint8_t r = imm8();
    apply_write(r, (this->*op)(read_r8(r), read_r8(0)), false);
}
void Tms7000::am_b2a(OpFunc op) {
    consume(5);
    apply_write(0, (this->*op)(read_r8(0), read_r8(1)), false);
}
void Tms7000::am_b2b(OpFunc op) {
    consume(6);
    apply_write(1, (this->*op)(read_r8(1), read_r8(1)), false);
}
void Tms7000::am_b2r(OpFunc op) {
    consume(7);
    const uint8_t r = imm8();
    apply_write(r, (this->*op)(read_r8(r), read_r8(1)), false);
}
void Tms7000::am_b2p(OpFunc op) {
    consume(9);
    const uint8_t r = imm8();
    apply_write(r, (this->*op)(read_p(r), read_r8(1)), true);
}
void Tms7000::am_r2a(OpFunc op) {
    consume(8);
    apply_write(0, (this->*op)(read_r8(0), read_r8(imm8())), false);
}
void Tms7000::am_r2b(OpFunc op) {
    consume(8);
    apply_write(1, (this->*op)(read_r8(1), read_r8(imm8())), false);
}
void Tms7000::am_r2r(OpFunc op) {
    consume(10);
    const uint8_t param2 = read_r8(imm8());
    const uint8_t r = imm8();
    apply_write(r, (this->*op)(read_r8(r), param2), false);
}
void Tms7000::am_i2a(OpFunc op) {
    consume(7);
    apply_write(0, (this->*op)(read_r8(0), imm8()), false);
}
void Tms7000::am_i2b(OpFunc op) {
    consume(7);
    apply_write(1, (this->*op)(read_r8(1), imm8()), false);
}
void Tms7000::am_i2r(OpFunc op) {
    consume(9);
    const uint8_t param2 = imm8();
    const uint8_t r = imm8();
    apply_write(r, (this->*op)(read_r8(r), param2), false);
}
void Tms7000::am_i2p(OpFunc op) {
    consume(11);
    const uint8_t param2 = imm8();
    const uint8_t r = imm8();
    apply_write(r, (this->*op)(read_p(r), param2), true);
}
void Tms7000::am_p2a(OpFunc op) {
    consume(9);
    apply_write(0, (this->*op)(read_r8(0), read_p(imm8())), false);
}
void Tms7000::am_p2b(OpFunc op) {
    consume(8);
    apply_write(1, (this->*op)(read_r8(1), read_p(imm8())), false);
}

int Tms7000::op_clr(uint8_t, uint8_t) {
    set_cnz(0);
    return 0;
}
int Tms7000::op_dec(uint8_t param1, uint8_t) {
    const uint16_t t = uint16_t(param1 - 1);
    set_nz(t);
    set_c(uint16_t(~t));
    return int(t);
}
int Tms7000::op_inc(uint8_t param1, uint8_t) {
    const uint16_t t = uint16_t(param1 + 1);
    set_cnz(t);
    return int(t);
}
int Tms7000::op_inv(uint8_t param1, uint8_t) {
    const uint8_t t = uint8_t(~param1);
    set_cnz(t);
    return t;
}
int Tms7000::op_rl(uint8_t param1, uint8_t) {
    const uint16_t t = uint16_t((param1 << 1) | (param1 >> 7));
    set_cnz(t);
    return int(t);
}
int Tms7000::op_rlc(uint8_t param1, uint8_t) {
    const uint16_t t = uint16_t((param1 << 1) | get_c());
    set_cnz(t);
    return int(t);
}
int Tms7000::op_rr(uint8_t param1, uint8_t) {
    const uint16_t t = uint16_t((param1 >> 1) | (param1 << 8) | (param1 << 7 & 0x80));
    set_cnz(t);
    return int(t);
}
int Tms7000::op_rrc(uint8_t param1, uint8_t) {
    const uint16_t t = uint16_t((param1 >> 1) | (param1 << 8) | (get_c() << 7));
    set_cnz(t);
    return int(t);
}
int Tms7000::op_swap(uint8_t param1, uint8_t) {
    consume(3);
    const uint16_t t = uint16_t((param1 >> 4) | (param1 << 4));
    set_cnz(t);
    return int(t);
}
int Tms7000::op_xchb(uint8_t param1, uint8_t) {
    consume(1);
    const uint8_t t = read_r8(1);
    set_cnz(t);
    write_r8(1, param1);
    return t;
}
int Tms7000::op_adc(uint8_t param1, uint8_t param2) {
    const uint16_t t = uint16_t(param1 + param2 + get_c());
    set_cnz(t);
    return int(t);
}
int Tms7000::op_add(uint8_t param1, uint8_t param2) {
    const uint16_t t = uint16_t(param1 + param2);
    set_cnz(t);
    return int(t);
}
int Tms7000::op_and(uint8_t param1, uint8_t param2) {
    const uint8_t t = uint8_t(param1 & param2);
    set_cnz(t);
    return t;
}
int Tms7000::op_cmp(uint8_t param1, uint8_t param2) {
    const uint16_t t = uint16_t(param1 - param2);
    set_nz(t);
    set_c(uint16_t(~t));
    return kWbNo;
}
int Tms7000::op_mpy(uint8_t param1, uint8_t param2) {
    consume(39);
    const uint16_t t = uint16_t(param1 * param2);
    set_cnz(uint16_t(t >> 8));
    write_mem16(0, t);
    return kWbNo;
}
int Tms7000::op_mov(uint8_t, uint8_t param2) {
    set_cnz(param2);
    return param2;
}
int Tms7000::op_or(uint8_t param1, uint8_t param2) {
    const uint8_t t = uint8_t(param1 | param2);
    set_cnz(t);
    return t;
}
int Tms7000::op_sbb(uint8_t param1, uint8_t param2) {
    const uint16_t t = uint16_t(param1 - param2 - (get_c() ? 0 : 1));
    set_nz(t);
    set_c(uint16_t(~t));
    return int(t);
}
int Tms7000::op_sub(uint8_t param1, uint8_t param2) {
    const uint16_t t = uint16_t(param1 - param2);
    set_nz(t);
    set_c(uint16_t(~t));
    return int(t);
}
int Tms7000::op_xor(uint8_t param1, uint8_t param2) {
    const uint8_t t = uint8_t(param1 ^ param2);
    set_cnz(t);
    return t;
}
int Tms7000::op_dac(uint8_t param1, uint8_t param2) {
    consume(2);
    const int c = get_c();
    const uint8_t h1 = uint8_t(param1 >> 4) & 0xf;
    const uint8_t l1 = uint8_t(param1) & 0xf;
    const uint8_t h2 = uint8_t(param2 >> 4) & 0xf;
    const uint8_t l2 = uint8_t(param2) & 0xf;
    uint8_t d = ((l1 + l2 + c) < 10) ? 0 : 1;
    if ((h1 + h2) == 9)
        d |= 2;
    else if ((h1 + h2) > 9)
        d |= 4;
    const uint8_t t = uint8_t(param1 + param2 + c + kBcdOut[d]);
    set_cnz(t);
    if (d > 2) sr_ |= kSrC;
    return t;
}
int Tms7000::op_dsb(uint8_t param1, uint8_t param2) {
    consume(2);
    const int c = get_c() ? 0 : 1;
    const uint8_t h1 = uint8_t(param1 >> 4) & 0xf;
    const uint8_t l1 = uint8_t(param1) & 0xf;
    const uint8_t h2 = uint8_t(param2 >> 4) & 0xf;
    const uint8_t l2 = uint8_t(param2) & 0xf;
    uint8_t d = ((l1 - c) >= l2) ? 0 : 1;
    if (h1 == h2)
        d |= 2;
    else if (h1 < h2)
        d |= 4;
    const uint8_t t = uint8_t(param1 - param2 - c - kBcdOut[d]);
    set_cnz(t);
    if (d <= 2) sr_ |= kSrC;
    return t;
}

void Tms7000::shortbranch(bool check) {
    consume(2);
    const int8_t d = int8_t(imm8());
    if (check) {
        pc_ = uint16_t(pc_ + d);
        consume(2);
    }
}
void Tms7000::jmp(bool check) {
    consume(3);
    shortbranch(check);
}
int Tms7000::op_djnz(uint8_t param1, uint8_t) {
    const uint16_t t = uint16_t(param1 - 1);
    shortbranch(t != 0);
    return int(t);
}
int Tms7000::op_btjo(uint8_t param1, uint8_t param2) {
    const uint8_t t = uint8_t(param1 & param2);
    set_cnz(t);
    shortbranch(t != 0);
    return kWbNo;
}
int Tms7000::op_btjz(uint8_t param1, uint8_t param2) {
    const uint8_t t = uint8_t(~param1 & param2);
    set_cnz(t);
    shortbranch(t != 0);
    return kWbNo;
}

void Tms7000::decd_a() {
    consume(9);
    const uint32_t t = uint32_t(read_r16(0) - 1);
    write_r16(0, uint16_t(t));
    set_nz(uint16_t(t >> 8));
    set_c(uint16_t(~(t >> 8)));
}
void Tms7000::decd_b() {
    consume(9);
    const uint32_t t = uint32_t(read_r16(1) - 1);
    write_r16(1, uint16_t(t));
    set_nz(uint16_t(t >> 8));
    set_c(uint16_t(~(t >> 8)));
}
void Tms7000::decd_r() {
    consume(11);
    const uint8_t r = imm8();
    const uint32_t t = uint32_t(read_r16(r) - 1);
    write_r16(r, uint16_t(t));
    set_nz(uint16_t(t >> 8));
    set_c(uint16_t(~(t >> 8)));
}

void Tms7000::cmpa_dir() {
    consume(12);
    const uint16_t t = uint16_t(read_r8(0) - read_mem8(imm16()));
    set_nz(t);
    set_c(uint16_t(~t));
}
void Tms7000::cmpa_inx() {
    consume(14);
    const uint16_t t = uint16_t(read_r8(0) - read_mem8(uint16_t(imm16() + read_r8(1))));
    set_nz(t);
    set_c(uint16_t(~t));
}
void Tms7000::cmpa_ind() {
    consume(11);
    const uint16_t t = uint16_t(read_r8(0) - read_mem8(read_r16(imm8())));
    set_nz(t);
    set_c(uint16_t(~t));
}

void Tms7000::lda_dir() {
    consume(11);
    const uint8_t t = read_mem8(imm16());
    write_r8(0, t);
    set_cnz(t);
}
void Tms7000::lda_inx() {
    consume(13);
    const uint8_t t = read_mem8(uint16_t(imm16() + read_r8(1)));
    write_r8(0, t);
    set_cnz(t);
}
void Tms7000::lda_ind() {
    consume(10);
    const uint8_t t = read_mem8(read_r16(imm8()));
    write_r8(0, t);
    set_cnz(t);
}

void Tms7000::sta_dir() {
    consume(11);
    const uint8_t t = read_r8(0);
    write_mem8(imm16(), t);
    set_cnz(t);
}
void Tms7000::sta_inx() {
    consume(13);
    const uint8_t t = read_r8(0);
    write_mem8(uint16_t(imm16() + read_r8(1)), t);
    set_cnz(t);
}
void Tms7000::sta_ind() {
    consume(10);
    const uint8_t t = read_r8(0);
    write_mem8(read_r16(imm8()), t);
    set_cnz(t);
}

void Tms7000::movd_dir() {
    consume(15);
    const uint16_t t = imm16();
    write_r16(imm8(), t);
    set_cnz(uint16_t(t >> 8));
}
void Tms7000::movd_inx() {
    consume(17);
    const uint16_t t = uint16_t(imm16() + read_r8(1));
    write_r16(imm8(), t);
    set_cnz(uint16_t(t >> 8));
}
void Tms7000::movd_ind() {
    consume(14);
    const uint16_t t = read_r16(imm8());
    write_r16(imm8(), t);
    set_cnz(uint16_t(t >> 8));
}

void Tms7000::br_dir() {
    consume(10);
    pc_ = imm16();
}
void Tms7000::br_inx() {
    consume(12);
    pc_ = uint16_t(imm16() + read_r8(1));
}
void Tms7000::br_ind() {
    consume(9);
    pc_ = read_r16(imm8());
}

void Tms7000::call_dir() {
    consume(14);
    const uint16_t t = imm16();
    push16(pc_);
    pc_ = t;
}
void Tms7000::call_inx() {
    consume(16);
    const uint16_t t = uint16_t(imm16() + read_r8(1));
    push16(pc_);
    pc_ = t;
}
void Tms7000::call_ind() {
    consume(13);
    const uint16_t t = read_r16(imm8());
    push16(pc_);
    pc_ = t;
}

void Tms7000::trap(uint8_t op) {
    consume(14);
    push16(pc_);
    pc_ = read_mem16(uint16_t(0xff00 | uint8_t(op << 1)));
}

void Tms7000::reti() {
    consume(9);
    pc_ = pull16();
    sr_ = uint8_t(pull8() & 0xf0);
    check_interrupts();
}
void Tms7000::rets() {
    consume(7);
    pc_ = pull16();
}

void Tms7000::pop_a() {
    consume(6);
    const uint8_t t = pull8();
    write_r8(0, t);
    set_cnz(t);
}
void Tms7000::pop_b() {
    consume(6);
    const uint8_t t = pull8();
    write_r8(1, t);
    set_cnz(t);
}
void Tms7000::pop_r() {
    consume(8);
    const uint8_t t = pull8();
    write_r8(imm8(), t);
    set_cnz(t);
}
void Tms7000::pop_st() {
    consume(6);
    sr_ = uint8_t(pull8() & 0xf0);
    check_interrupts();
}

void Tms7000::push_a() {
    consume(6);
    const uint8_t t = read_r8(0);
    push8(t);
    set_cnz(t);
}
void Tms7000::push_b() {
    consume(6);
    const uint8_t t = read_r8(1);
    push8(t);
    set_cnz(t);
}
void Tms7000::push_r() {
    consume(8);
    const uint8_t t = read_r8(imm8());
    push8(t);
    set_cnz(t);
}
void Tms7000::push_st() {
    consume(6);
    push8(sr_);
}

void Tms7000::nop() { consume(5); }
void Tms7000::idle_op() {
    consume(6);
    pc_--;
    idle_state_ = true;
}
void Tms7000::dint() {
    consume(5);
    sr_ &= uint8_t(~(kSrN | kSrZ | kSrC | kSrI));
}
void Tms7000::eint() {
    consume(5);
    sr_ |= uint8_t(kSrN | kSrZ | kSrC | kSrI);
    check_interrupts();
}
void Tms7000::ldsp() {
    consume(5);
    sp_ = read_r8(1);
}
void Tms7000::stsp() {
    consume(6);
    write_r8(1, sp_);
}
void Tms7000::setc() {
    consume(5);
    sr_ = uint8_t((sr_ & ~kSrN) | kSrC | kSrZ);
}
void Tms7000::illegal() { consume(5); }

void Tms7000::lvdp() {
    consume(10);
    imm8();
    (void)read_p(0x28);
    const uint8_t t = read_p(0x24);
    write_r8(0, t);
    set_cnz(t);
}

void Tms7000::execute_one(uint8_t op) {
    if (exl_lvdp_ && op == 0xd7) {
        lvdp();
        return;
    }
    switch (op) {
        case 0x00: nop(); break;
        case 0x01: idle_op(); break;
        case 0x05: eint(); break;
        case 0x06: dint(); break;
        case 0x07: setc(); break;
        case 0x08: pop_st(); break;
        case 0x09: stsp(); break;
        case 0x0a: rets(); break;
        case 0x0b: reti(); break;
        case 0x0d: ldsp(); break;
        case 0x0e: push_st(); break;

        case 0x12: am_r2a(&Tms7000::op_mov); break;
        case 0x13: am_r2a(&Tms7000::op_and); break;
        case 0x14: am_r2a(&Tms7000::op_or); break;
        case 0x15: am_r2a(&Tms7000::op_xor); break;
        case 0x16: am_r2a(&Tms7000::op_btjo); break;
        case 0x17: am_r2a(&Tms7000::op_btjz); break;
        case 0x18: am_r2a(&Tms7000::op_add); break;
        case 0x19: am_r2a(&Tms7000::op_adc); break;
        case 0x1a: am_r2a(&Tms7000::op_sub); break;
        case 0x1b: am_r2a(&Tms7000::op_sbb); break;
        case 0x1c: am_r2a(&Tms7000::op_mpy); break;
        case 0x1d: am_r2a(&Tms7000::op_cmp); break;
        case 0x1e: am_r2a(&Tms7000::op_dac); break;
        case 0x1f: am_r2a(&Tms7000::op_dsb); break;

        case 0x22: am_i2a(&Tms7000::op_mov); break;
        case 0x23: am_i2a(&Tms7000::op_and); break;
        case 0x24: am_i2a(&Tms7000::op_or); break;
        case 0x25: am_i2a(&Tms7000::op_xor); break;
        case 0x26: am_i2a(&Tms7000::op_btjo); break;
        case 0x27: am_i2a(&Tms7000::op_btjz); break;
        case 0x28: am_i2a(&Tms7000::op_add); break;
        case 0x29: am_i2a(&Tms7000::op_adc); break;
        case 0x2a: am_i2a(&Tms7000::op_sub); break;
        case 0x2b: am_i2a(&Tms7000::op_sbb); break;
        case 0x2c: am_i2a(&Tms7000::op_mpy); break;
        case 0x2d: am_i2a(&Tms7000::op_cmp); break;
        case 0x2e: am_i2a(&Tms7000::op_dac); break;
        case 0x2f: am_i2a(&Tms7000::op_dsb); break;

        case 0x32: am_r2b(&Tms7000::op_mov); break;
        case 0x33: am_r2b(&Tms7000::op_and); break;
        case 0x34: am_r2b(&Tms7000::op_or); break;
        case 0x35: am_r2b(&Tms7000::op_xor); break;
        case 0x36: am_r2b(&Tms7000::op_btjo); break;
        case 0x37: am_r2b(&Tms7000::op_btjz); break;
        case 0x38: am_r2b(&Tms7000::op_add); break;
        case 0x39: am_r2b(&Tms7000::op_adc); break;
        case 0x3a: am_r2b(&Tms7000::op_sub); break;
        case 0x3b: am_r2b(&Tms7000::op_sbb); break;
        case 0x3c: am_r2b(&Tms7000::op_mpy); break;
        case 0x3d: am_r2b(&Tms7000::op_cmp); break;
        case 0x3e: am_r2b(&Tms7000::op_dac); break;
        case 0x3f: am_r2b(&Tms7000::op_dsb); break;

        case 0x42: am_r2r(&Tms7000::op_mov); break;
        case 0x43: am_r2r(&Tms7000::op_and); break;
        case 0x44: am_r2r(&Tms7000::op_or); break;
        case 0x45: am_r2r(&Tms7000::op_xor); break;
        case 0x46: am_r2r(&Tms7000::op_btjo); break;
        case 0x47: am_r2r(&Tms7000::op_btjz); break;
        case 0x48: am_r2r(&Tms7000::op_add); break;
        case 0x49: am_r2r(&Tms7000::op_adc); break;
        case 0x4a: am_r2r(&Tms7000::op_sub); break;
        case 0x4b: am_r2r(&Tms7000::op_sbb); break;
        case 0x4c: am_r2r(&Tms7000::op_mpy); break;
        case 0x4d: am_r2r(&Tms7000::op_cmp); break;
        case 0x4e: am_r2r(&Tms7000::op_dac); break;
        case 0x4f: am_r2r(&Tms7000::op_dsb); break;

        case 0x52: am_i2b(&Tms7000::op_mov); break;
        case 0x53: am_i2b(&Tms7000::op_and); break;
        case 0x54: am_i2b(&Tms7000::op_or); break;
        case 0x55: am_i2b(&Tms7000::op_xor); break;
        case 0x56: am_i2b(&Tms7000::op_btjo); break;
        case 0x57: am_i2b(&Tms7000::op_btjz); break;
        case 0x58: am_i2b(&Tms7000::op_add); break;
        case 0x59: am_i2b(&Tms7000::op_adc); break;
        case 0x5a: am_i2b(&Tms7000::op_sub); break;
        case 0x5b: am_i2b(&Tms7000::op_sbb); break;
        case 0x5c: am_i2b(&Tms7000::op_mpy); break;
        case 0x5d: am_i2b(&Tms7000::op_cmp); break;
        case 0x5e: am_i2b(&Tms7000::op_dac); break;
        case 0x5f: am_i2b(&Tms7000::op_dsb); break;

        case 0x62: am_b2a(&Tms7000::op_mov); break;
        case 0x63: am_b2a(&Tms7000::op_and); break;
        case 0x64: am_b2a(&Tms7000::op_or); break;
        case 0x65: am_b2a(&Tms7000::op_xor); break;
        case 0x66: am_b2a(&Tms7000::op_btjo); break;
        case 0x67: am_b2a(&Tms7000::op_btjz); break;
        case 0x68: am_b2a(&Tms7000::op_add); break;
        case 0x69: am_b2a(&Tms7000::op_adc); break;
        case 0x6a: am_b2a(&Tms7000::op_sub); break;
        case 0x6b: am_b2a(&Tms7000::op_sbb); break;
        case 0x6c: am_b2a(&Tms7000::op_mpy); break;
        case 0x6d: am_b2a(&Tms7000::op_cmp); break;
        case 0x6e: am_b2a(&Tms7000::op_dac); break;
        case 0x6f: am_b2a(&Tms7000::op_dsb); break;

        case 0x72: am_i2r(&Tms7000::op_mov); break;
        case 0x73: am_i2r(&Tms7000::op_and); break;
        case 0x74: am_i2r(&Tms7000::op_or); break;
        case 0x75: am_i2r(&Tms7000::op_xor); break;
        case 0x76: am_i2r(&Tms7000::op_btjo); break;
        case 0x77: am_i2r(&Tms7000::op_btjz); break;
        case 0x78: am_i2r(&Tms7000::op_add); break;
        case 0x79: am_i2r(&Tms7000::op_adc); break;
        case 0x7a: am_i2r(&Tms7000::op_sub); break;
        case 0x7b: am_i2r(&Tms7000::op_sbb); break;
        case 0x7c: am_i2r(&Tms7000::op_mpy); break;
        case 0x7d: am_i2r(&Tms7000::op_cmp); break;
        case 0x7e: am_i2r(&Tms7000::op_dac); break;
        case 0x7f: am_i2r(&Tms7000::op_dsb); break;

        case 0x80: am_p2a(&Tms7000::op_mov); break;
        case 0x82: am_a2p(&Tms7000::op_mov); break;
        case 0x83: am_a2p(&Tms7000::op_and); break;
        case 0x84: am_a2p(&Tms7000::op_or); break;
        case 0x85: am_a2p(&Tms7000::op_xor); break;
        case 0x86: am_a2p(&Tms7000::op_btjo); break;
        case 0x87: am_a2p(&Tms7000::op_btjz); break;
        case 0x88: movd_dir(); break;
        case 0x8a: lda_dir(); break;
        case 0x8b: sta_dir(); break;
        case 0x8c: br_dir(); break;
        case 0x8d: cmpa_dir(); break;
        case 0x8e: call_dir(); break;

        case 0x91: am_p2b(&Tms7000::op_mov); break;
        case 0x92: am_b2p(&Tms7000::op_mov); break;
        case 0x93: am_b2p(&Tms7000::op_and); break;
        case 0x94: am_b2p(&Tms7000::op_or); break;
        case 0x95: am_b2p(&Tms7000::op_xor); break;
        case 0x96: am_b2p(&Tms7000::op_btjo); break;
        case 0x97: am_b2p(&Tms7000::op_btjz); break;
        case 0x98: movd_ind(); break;
        case 0x9a: lda_ind(); break;
        case 0x9b: sta_ind(); break;
        case 0x9c: br_ind(); break;
        case 0x9d: cmpa_ind(); break;
        case 0x9e: call_ind(); break;

        case 0xa2: am_i2p(&Tms7000::op_mov); break;
        case 0xa3: am_i2p(&Tms7000::op_and); break;
        case 0xa4: am_i2p(&Tms7000::op_or); break;
        case 0xa5: am_i2p(&Tms7000::op_xor); break;
        case 0xa6: am_i2p(&Tms7000::op_btjo); break;
        case 0xa7: am_i2p(&Tms7000::op_btjz); break;
        case 0xa8: movd_inx(); break;
        case 0xaa: lda_inx(); break;
        case 0xab: sta_inx(); break;
        case 0xac: br_inx(); break;
        case 0xad: cmpa_inx(); break;
        case 0xae: call_inx(); break;

        case 0xb0: am_a2a(&Tms7000::op_mov); break;
        case 0xb1: am_b2a(&Tms7000::op_mov); break;
        case 0xb2: am_a(&Tms7000::op_dec); break;
        case 0xb3: am_a(&Tms7000::op_inc); break;
        case 0xb4: am_a(&Tms7000::op_inv); break;
        case 0xb5: am_a(&Tms7000::op_clr); break;
        case 0xb6: am_a(&Tms7000::op_xchb); break;
        case 0xb7: am_a(&Tms7000::op_swap); break;
        case 0xb8: push_a(); break;
        case 0xb9: pop_a(); break;
        case 0xba: am_a(&Tms7000::op_djnz); break;
        case 0xbb: decd_a(); break;
        case 0xbc: am_a(&Tms7000::op_rr); break;
        case 0xbd: am_a(&Tms7000::op_rrc); break;
        case 0xbe: am_a(&Tms7000::op_rl); break;
        case 0xbf: am_a(&Tms7000::op_rlc); break;

        case 0xc0: am_a2b(&Tms7000::op_mov); break;
        case 0xc1: am_b2b(&Tms7000::op_mov); break;
        case 0xc2: am_b(&Tms7000::op_dec); break;
        case 0xc3: am_b(&Tms7000::op_inc); break;
        case 0xc4: am_b(&Tms7000::op_inv); break;
        case 0xc5: am_b(&Tms7000::op_clr); break;
        case 0xc6: am_b(&Tms7000::op_xchb); break;
        case 0xc7: am_b(&Tms7000::op_swap); break;
        case 0xc8: push_b(); break;
        case 0xc9: pop_b(); break;
        case 0xca: am_b(&Tms7000::op_djnz); break;
        case 0xcb: decd_b(); break;
        case 0xcc: am_b(&Tms7000::op_rr); break;
        case 0xcd: am_b(&Tms7000::op_rrc); break;
        case 0xce: am_b(&Tms7000::op_rl); break;
        case 0xcf: am_b(&Tms7000::op_rlc); break;

        case 0xd0: am_a2r(&Tms7000::op_mov); break;
        case 0xd1: am_b2r(&Tms7000::op_mov); break;
        case 0xd2: am_r(&Tms7000::op_dec); break;
        case 0xd3: am_r(&Tms7000::op_inc); break;
        case 0xd4: am_r(&Tms7000::op_inv); break;
        case 0xd5: am_r(&Tms7000::op_clr); break;
        case 0xd6: am_r(&Tms7000::op_xchb); break;
        case 0xd7: am_r(&Tms7000::op_swap); break;
        case 0xd8: push_r(); break;
        case 0xd9: pop_r(); break;
        case 0xda: am_r(&Tms7000::op_djnz); break;
        case 0xdb: decd_r(); break;
        case 0xdc: am_r(&Tms7000::op_rr); break;
        case 0xdd: am_r(&Tms7000::op_rrc); break;
        case 0xde: am_r(&Tms7000::op_rl); break;
        case 0xdf: am_r(&Tms7000::op_rlc); break;

        case 0xe0: jmp(true); break;
        case 0xe1: jmp(sr_ & kSrN); break;
        case 0xe2: jmp(sr_ & kSrZ); break;
        case 0xe3: jmp(sr_ & kSrC); break;
        case 0xe4: jmp(!(sr_ & (kSrZ | kSrN))); break;
        case 0xe5: jmp(!(sr_ & kSrN)); break;
        case 0xe6: jmp(!(sr_ & kSrZ)); break;
        case 0xe7: jmp(!(sr_ & kSrC)); break;

        case 0xe8: case 0xe9: case 0xea: case 0xeb:
        case 0xec: case 0xed: case 0xee: case 0xef:
        case 0xf0: case 0xf1: case 0xf2: case 0xf3:
        case 0xf4: case 0xf5: case 0xf6: case 0xf7:
        case 0xf8: case 0xf9: case 0xfa: case 0xfb:
        case 0xfc: case 0xfd: case 0xfe: case 0xff:
            trap(op);
            break;
        default:
            illegal();
            break;
    }
}

}  // namespace dsp
