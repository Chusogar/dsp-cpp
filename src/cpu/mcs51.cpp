#include "cpu/mcs51.h"

namespace dsp {
namespace {

constexpr uint8_t kCycles[256] = {
    1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 4, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 1, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

constexpr uint8_t kAddrP0 = 0x80;
constexpr uint8_t kAddrSp = 0x81;
constexpr uint8_t kAddrDpl = 0x82;
constexpr uint8_t kAddrDph = 0x83;
constexpr uint8_t kAddrTcon = 0x88;
constexpr uint8_t kAddrTmod = 0x89;
constexpr uint8_t kAddrTl0 = 0x8a;
constexpr uint8_t kAddrTl1 = 0x8b;
constexpr uint8_t kAddrTh0 = 0x8c;
constexpr uint8_t kAddrTh1 = 0x8d;
constexpr uint8_t kAddrP1 = 0x90;
constexpr uint8_t kAddrP2 = 0xa0;
constexpr uint8_t kAddrIe = 0xa8;
constexpr uint8_t kAddrP3 = 0xb0;
constexpr uint8_t kAddrIp = 0xb8;
constexpr uint8_t kAddrPsw = 0xd0;
constexpr uint8_t kAddrAcc = 0xe0;
constexpr uint8_t kAddrB = 0xf0;

constexpr uint16_t kVectorIe0 = 0x003;
constexpr uint16_t kVectorTf0 = 0x00b;
constexpr uint16_t kVectorIe1 = 0x013;
constexpr uint16_t kVectorTf1 = 0x01b;

uint8_t get_bit(uint8_t value, int bit) { return uint8_t((value >> bit) & 1); }

void set_bit(uint8_t& value, int bit, uint8_t state) {
    value = uint8_t((value & ~(1 << bit)) | (state << bit));
}

}  // namespace

Mcs51::Mcs51(uint32_t clock) : clock_(clock / 12) {}

void Mcs51::set_port_read_handler(int port, PortReadHandler handler) {
    port_read_[size_t(port)] = std::move(handler);
}

void Mcs51::set_port_write_handler(int port, PortWriteHandler handler) {
    port_write_[size_t(port)] = std::move(handler);
}

void Mcs51::set_external_handlers(ReadHandler read, WriteHandler write) {
    read_external_ = std::move(read);
    write_external_ = std::move(write);
}

void Mcs51::reset() {
    ram_.fill(0);
    sfr_.fill(0);
    forced_input_.fill(0);
    pc_ = 0;
    sfr_[kAddrSp] = 0x07;
    set_psw(0);
    calc_parity_ = false;
    rwm_ = false;
    for (int port = 3; port >= 0; port--) {
        const uint8_t address = uint8_t(kAddrP0 + port * 0x10);
        sfr_[address] = 0xff;
        if (port_write_[size_t(port)]) port_write_[size_t(port)](0xff);
    }
    irq0_ = IrqLine::Clear;
    irq1_ = IrqLine::Clear;
    last_line_state_ = 0;
    irq_active_ = 0;
    cur_irq_prio_ = -1;
    t0_count_ = 0;
    t1_count_ = 0;
}

uint8_t Mcs51::get_psw() {
    uint8_t value = 0;
    if (psw_.c) value |= 0x80;
    if (psw_.ac) value |= 0x40;
    if (psw_.u) value |= 0x20;
    if (psw_.bank0) value |= 0x10;
    if (psw_.bank1) value |= 0x08;
    if (psw_.o) value |= 0x04;
    if (psw_.p) value |= 0x01;
    calc_parity_ = true;
    return value;
}

void Mcs51::set_psw(uint8_t value) {
    psw_.c = (value & 0x80) != 0;
    psw_.ac = (value & 0x40) != 0;
    psw_.u = (value & 0x20) != 0;
    psw_.bank0 = (value & 0x10) != 0;
    psw_.bank1 = (value & 0x08) != 0;
    psw_.o = (value & 0x04) != 0;
    psw_.p = (value & 0x01) != 0;
    sfr_[kAddrPsw] = value;
}

void Mcs51::add_flags(uint8_t a, uint8_t data, uint8_t carry) {
    const uint16_t result = uint16_t(a + data + carry);
    const int16_t signed_result = int16_t(int8_t(a) + int8_t(data) + carry);
    psw_.c = (result & 0x100) != 0;
    psw_.ac = (((a & 0x0f) + (data & 0x0f) + carry) & 0x10) != 0;
    psw_.o = signed_result < -128 || signed_result > 127;
    sfr_[kAddrPsw] = get_psw();
}

void Mcs51::sub_flags(uint8_t a, uint8_t data, uint8_t carry) {
    const uint16_t result = uint16_t(a - (data + carry));
    const int16_t signed_result = int16_t(int8_t(a) - int8_t(uint8_t(data + carry)));
    psw_.c = (result & 0x100) != 0;
    psw_.ac = (((a & 0x0f) - (data & 0x0f) + carry) & 0x10) != 0;
    psw_.o = signed_result < -128 || signed_result > 127;
    sfr_[kAddrPsw] = get_psw();
}

void Mcs51::update_irq_prio(uint8_t ipl, uint8_t iph) {
    for (int i = 0; i < 8; i++) {
        irq_prio_[size_t(i)] = uint8_t(((ipl >> i) & 1) | (((iph >> i) & 1) << 1));
    }
}

void Mcs51::iram_w(uint8_t pos, uint8_t value) {
    if (pos < 0x80) {
        ram_[pos] = value;
        return;
    }
    switch (pos) {
        case kAddrAcc:
            calc_parity_ = true;
            break;
        case kAddrPsw:
            set_psw(value);
            calc_parity_ = true;
            break;
        case kAddrP0:
        case kAddrP1:
        case kAddrP2:
        case kAddrP3: {
            const size_t port = size_t((pos - kAddrP0) >> 4);
            if (port_write_[port]) port_write_[port](value);
            break;
        }
        case kAddrIp:
            update_irq_prio(value, 0);
            break;
        default:
            break;
    }
    sfr_[pos] = value;
}

uint8_t Mcs51::iram_r(uint8_t pos) {
    if (pos < 0x80) return ram_[pos];
    switch (pos) {
        case kAddrP0:
        case kAddrP1:
        case kAddrP2:
        case kAddrP3: {
            const size_t port = size_t((pos - kAddrP0) >> 4);
            if (rwm_) return sfr_[pos];
            uint8_t result = 0xff;
            if (port_read_[port]) {
                result = uint8_t((sfr_[pos] | forced_input_[port]) & port_read_[port]());
            }
            if (pos == kAddrP3) {
                if (irq0_ != IrqLine::Clear) result &= 0xfb;
                if (irq1_ != IrqLine::Clear) result &= 0xf7;
            }
            return result;
        }
        default:
            return sfr_[pos];
    }
}

uint8_t Mcs51::iram_ir(uint8_t pos) const { return pos < 0x80 ? ram_[pos] : 0xff; }

void Mcs51::iram_iw(uint8_t pos, uint8_t value) {
    if (pos < 0x80) ram_[pos] = value;
}

uint8_t Mcs51::bit_address_r(uint8_t pos) {
    const uint8_t address =
        pos < 0x80 ? uint8_t(((pos & 0x78) >> 3) + 0x20) : uint8_t(((pos & 0x78) >> 3) * 8 + 0x80);
    const int bit = pos & 0x7;
    return uint8_t((iram_r(address) >> bit) & 1);
}

void Mcs51::bit_address_w(uint8_t pos, uint8_t bit) {
    const uint8_t address =
        pos < 0x80 ? uint8_t(((pos & 0x78) >> 3) + 0x20) : uint8_t(((pos & 0x78) >> 3) * 8 + 0x80);
    const int bit_pos = pos & 0x7;
    uint8_t value = uint8_t(iram_r(address) & ~(1 << bit_pos));
    value |= uint8_t((bit & 1) << bit_pos);
    iram_w(address, value);
}

void Mcs51::push_pc() {
    uint8_t sp = uint8_t(sfr_[kAddrSp] + 1);
    iram_iw(sp, uint8_t(pc_ & 0xff));
    sp++;
    iram_iw(sp, uint8_t(pc_ >> 8));
    sfr_[kAddrSp] = sp;
}

void Mcs51::pop_pc() {
    uint8_t sp = sfr_[kAddrSp];
    pc_ = uint16_t(iram_ir(sp) << 8);
    sp--;
    pc_ = uint16_t(pc_ | iram_ir(sp));
    sfr_[kAddrSp] = uint8_t(sp - 1);
}

void Mcs51::clear_irqs() {
    if (cur_irq_prio_ >= 0) irq_active_ = uint8_t(irq_active_ & ~(1 << cur_irq_prio_));
    if (irq_active_ & 4) {
        cur_irq_prio_ = 2;
    } else if (irq_active_ & 2) {
        cur_irq_prio_ = 1;
    } else if (irq_active_ & 1) {
        cur_irq_prio_ = 0;
    } else {
        cur_irq_prio_ = -1;
    }
}

int Mcs51::evaluate_irq() {
    uint8_t ints = uint8_t(get_bit(sfr_[kAddrTcon], 1) | (get_bit(sfr_[kAddrTcon], 5) << 1) |
                           (get_bit(sfr_[kAddrTcon], 3) << 2) | (get_bit(sfr_[kAddrTcon], 7) << 3));
    const uint8_t int_mask = get_bit(sfr_[kAddrIe], 7) != 0 ? sfr_[kAddrIe] : 0;
    ints = uint8_t(ints & int_mask);

    const auto release_hold_lines = [this]() {
        if (irq0_ == IrqLine::Hold) last_line_state_ &= 0xfffffffe;
        if (irq1_ == IrqLine::Hold) last_line_state_ &= 0xfffffffd;
    };

    if (ints == 0) {
        release_hold_lines();
        return 0;
    }

    int priority_request = -1;
    uint16_t vector = 0;
    for (int i = 0; i < num_interrupts_; i++) {
        if ((ints & (1 << i)) == 0) continue;
        if (irq_prio_[size_t(i)] > priority_request) {
            priority_request = irq_prio_[size_t(i)];
            vector = uint16_t((i << 3) | 3);
        }
    }
    if (irq_active_ != 0 && priority_request <= cur_irq_prio_) {
        release_hold_lines();
        return 0;
    }
    // Work around the polling latency of "jb int0/int1" loops.
    if (vector == kVectorIe0 && peek(pc_) == 0x20 && peek(uint16_t(pc_ + 1)) == 0xb2) pc_ += 3;
    if (vector == kVectorIe1 && peek(pc_) == 0x20 && peek(uint16_t(pc_ + 1)) == 0xb3) pc_ += 3;

    push_pc();
    pc_ = vector;
    cur_irq_prio_ = priority_request;
    irq_active_ = uint8_t(irq_active_ | (1 << priority_request));
    switch (vector) {
        case kVectorIe0:
            if (get_bit(sfr_[kAddrTcon], 0) != 0) set_bit(sfr_[kAddrTcon], 1, 0);
            if (irq0_ == IrqLine::Hold) set_irq0_line(IrqLine::Clear);
            break;
        case kVectorTf0:
            set_bit(sfr_[kAddrTcon], 5, 0);
            break;
        case kVectorIe1:
            if (get_bit(sfr_[kAddrTcon], 2) != 0) set_bit(sfr_[kAddrTcon], 3, 0);
            if (irq1_ == IrqLine::Hold) set_irq1_line(IrqLine::Clear);
            break;
        case kVectorTf1:
            set_bit(sfr_[kAddrTcon], 7, 0);
            break;
        default:
            break;
    }
    return 2;
}

void Mcs51::set_irq0_line(IrqLine state) {
    const uint32_t new_state = (last_line_state_ & 0xfffffffe) | (state != IrqLine::Clear ? 1 : 0);
    const uint32_t transition = ~last_line_state_ & new_state;
    if (state != IrqLine::Clear) {
        if (get_bit(sfr_[kAddrTcon], 0) != 0) {
            if ((transition & 1) != 0) set_bit(sfr_[kAddrTcon], 1, 1);
        } else {
            set_bit(sfr_[kAddrTcon], 1, 1);
        }
    } else if (get_bit(sfr_[kAddrTcon], 0) == 0) {
        set_bit(sfr_[kAddrTcon], 1, 0);
    }
    last_line_state_ = new_state;
    irq0_ = state;
}

void Mcs51::set_irq1_line(IrqLine state) {
    const uint32_t new_state =
        (last_line_state_ & 0xfffffffd) | uint32_t((state != IrqLine::Clear ? 1 : 0) << 1);
    const uint32_t transition = ~last_line_state_ & new_state;
    if (state != IrqLine::Clear) {
        if (get_bit(sfr_[kAddrTcon], 2) != 0) {
            if ((transition & 2) != 0) set_bit(sfr_[kAddrTcon], 3, 1);
        } else {
            set_bit(sfr_[kAddrTcon], 3, 1);
        }
    } else if (get_bit(sfr_[kAddrTcon], 2) == 0) {
        set_bit(sfr_[kAddrTcon], 3, 0);
    }
    last_line_state_ = new_state;
    irq1_ = state;
}

void Mcs51::update_timer_t0(int cycles) {
    const uint8_t mode = uint8_t((get_bit(sfr_[kAddrTmod], 1) << 1) | get_bit(sfr_[kAddrTmod], 0));
    if (get_bit(sfr_[kAddrTcon], 4) != 0) {
        uint32_t delta = get_bit(sfr_[kAddrTmod], 2) != 0 ? t0_count_ : uint32_t(cycles);
        t0_count_ = 0;
        if (get_bit(sfr_[kAddrTmod], 3) != 0 && get_bit(sfr_[kAddrTcon], 1) == 0) delta = 0;
        uint32_t count = 0;
        switch (mode) {
            case 0:
                count = uint32_t((sfr_[kAddrTh0] << 5) | (sfr_[kAddrTl0] & 0x1f)) + delta;
                if ((count & 0xffffe000) != 0) set_bit(sfr_[kAddrTcon], 5, 1);
                sfr_[kAddrTh0] = uint8_t((count >> 5) & 0xff);
                sfr_[kAddrTl0] = uint8_t(count & 0x1f);
                break;
            case 1:
                count = uint32_t((sfr_[kAddrTh0] << 8) | sfr_[kAddrTl0]) + delta;
                if ((count & 0xffff0000) != 0) set_bit(sfr_[kAddrTcon], 5, 1);
                sfr_[kAddrTh0] = uint8_t((count >> 8) & 0xff);
                sfr_[kAddrTl0] = uint8_t(count & 0xff);
                break;
            case 2:
                count = sfr_[kAddrTl0] + delta;
                if ((count & 0xffffff00) != 0) {
                    set_bit(sfr_[kAddrTcon], 5, 1);
                    count += sfr_[kAddrTh0];
                }
                sfr_[kAddrTl0] = uint8_t(count & 0xff);
                break;
            default:
                count = sfr_[kAddrTl0] + delta;
                if ((count & 0xffffff00) != 0) set_bit(sfr_[kAddrTcon], 5, 1);
                sfr_[kAddrTl0] = uint8_t(count & 0xff);
                break;
        }
    }
    if (mode == 3 && get_bit(sfr_[kAddrTcon], 6) != 0) {
        const uint32_t count = sfr_[kAddrTh0] + uint32_t(cycles);
        if ((count & 0xffffff00) != 0) set_bit(sfr_[kAddrTcon], 7, 1);
        sfr_[kAddrTh0] = uint8_t(count & 0xff);
    }
}

void Mcs51::update_timer_t1(int cycles) {
    const uint8_t mode = uint8_t((get_bit(sfr_[kAddrTmod], 5) << 1) | get_bit(sfr_[kAddrTmod], 4));
    const uint8_t mode_t0 =
        uint8_t((get_bit(sfr_[kAddrTmod], 1) << 1) | get_bit(sfr_[kAddrTmod], 0));
    uint32_t delta = uint32_t(cycles);
    if (mode_t0 != 3) {
        if (get_bit(sfr_[kAddrTcon], 6) == 0) return;
        if (get_bit(sfr_[kAddrTmod], 6) != 0) delta = t1_count_;
        if (get_bit(sfr_[kAddrTmod], 7) != 0 && get_bit(sfr_[kAddrTcon], 3) == 0) delta = 0;
    }
    t1_count_ = 0;
    uint32_t count = 0;
    uint32_t overflow = 0;
    switch (mode) {
        case 0:
            count = uint32_t((sfr_[kAddrTh1] << 5) | (sfr_[kAddrTl1] & 0x1f)) + delta;
            overflow = count & 0xffffe000;
            sfr_[kAddrTh1] = uint8_t((count >> 5) & 0xff);
            sfr_[kAddrTl1] = uint8_t(count & 0x1f);
            break;
        case 1:
            count = uint32_t((sfr_[kAddrTh1] << 8) | sfr_[kAddrTl1]) + delta;
            overflow = count & 0xffff0000;
            sfr_[kAddrTh1] = uint8_t((count >> 8) & 0xff);
            sfr_[kAddrTl1] = uint8_t(count & 0xff);
            break;
        case 2:
            count = sfr_[kAddrTl1] + delta;
            overflow = count & 0xffffff00;
            if (overflow != 0) count += sfr_[kAddrTh1];
            sfr_[kAddrTl1] = uint8_t(count & 0xff);
            break;
        default:
            break;
    }
    if (overflow != 0 && mode_t0 != 3) set_bit(sfr_[kAddrTcon], 7, 1);
}

int Mcs51::run(int cycles) {
    int executed = 0;
    while (executed < cycles) {
        if (calc_parity_) {
            uint8_t parity = 0;
            uint8_t value = acc();
            for (int i = 0; i < 8; i++) {
                parity = uint8_t(parity ^ (value & 1));
                value = uint8_t(value >> 1);
            }
            psw_.p = (parity & 1) != 0;
            sfr_[kAddrPsw] = get_psw();
            calc_parity_ = false;
        }
        const int irq_cycles = evaluate_irq();
        const uint8_t opcode = fetch();
        switch (opcode) {
            case 0x00:  // nop
                break;
            case 0x01:
            case 0x21:
            case 0x41:
            case 0x61:
            case 0x81:
            case 0xa1:
            case 0xc1:
            case 0xe1: {  // ajmp
                const uint8_t low = fetch();
                pc_ = uint16_t((pc_ & 0xf800) | ((opcode & 0xe0) << 3) | low);
                break;
            }
            case 0x02: {  // ljmp
                const uint8_t high = fetch();
                const uint8_t low = fetch();
                pc_ = uint16_t((high << 8) | low);
                break;
            }
            case 0x03:  // rr a
                set_acc(uint8_t((acc() >> 1) | ((acc() & 1) << 7)));
                break;
            case 0x04:  // inc a
                set_acc(uint8_t(acc() + 1));
                break;
            case 0x05: {  // inc mem
                rwm_ = true;
                const uint8_t pos = fetch();
                iram_w(pos, uint8_t(iram_r(pos) + 1));
                rwm_ = false;
                break;
            }
            case 0x06:
            case 0x07: {  // inc @ri
                const uint8_t pos = r_reg(uint8_t(opcode & 1));
                iram_iw(pos, uint8_t(iram_ir(pos) + 1));
                break;
            }
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
            case 0x0d:
            case 0x0e:
            case 0x0f:  // inc rn
                set_reg(uint8_t(opcode & 7), uint8_t(r_reg(uint8_t(opcode & 7)) + 1));
                break;
            case 0x10: {  // jbc
                rwm_ = true;
                const uint8_t pos = fetch();
                const uint8_t offset = fetch();
                if (bit_address_r(pos) != 0) {
                    pc_ = uint16_t(pc_ + int8_t(offset));
                    bit_address_w(pos, 0);
                }
                rwm_ = false;
                break;
            }
            case 0x11:
            case 0x31:
            case 0x51:
            case 0x71:
            case 0x91:
            case 0xb1:
            case 0xd1:
            case 0xf1: {  // acall
                const uint8_t low = fetch();
                push_pc();
                pc_ = uint16_t((pc_ & 0xf800) | ((opcode & 0xe0) << 3) | low);
                break;
            }
            case 0x12: {  // lcall
                const uint8_t high = fetch();
                const uint8_t low = fetch();
                push_pc();
                pc_ = uint16_t((high << 8) | low);
                break;
            }
            case 0x13: {  // rrc a
                const uint8_t value = acc();
                set_acc(uint8_t((value >> 1) | (psw_.c ? 0x80 : 0)));
                psw_.c = (value & 1) != 0;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0x14:  // dec a
                set_acc(uint8_t(acc() - 1));
                break;
            case 0x15: {  // dec mem
                rwm_ = true;
                const uint8_t pos = fetch();
                iram_w(pos, uint8_t(iram_r(pos) - 1));
                rwm_ = false;
                break;
            }
            case 0x16:
            case 0x17: {  // dec @ri
                const uint8_t pos = r_reg(uint8_t(opcode & 1));
                iram_iw(pos, uint8_t(iram_ir(pos) - 1));
                break;
            }
            case 0x18:
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x1d:
            case 0x1e:
            case 0x1f:  // dec rn
                set_reg(uint8_t(opcode & 7), uint8_t(r_reg(uint8_t(opcode & 7)) - 1));
                break;
            case 0x20: {  // jb
                const uint8_t pos = fetch();
                const uint8_t offset = fetch();
                if (bit_address_r(pos) != 0) pc_ = uint16_t(pc_ + int8_t(offset));
                break;
            }
            case 0x22:  // ret
                pop_pc();
                break;
            case 0x23:  // rl a
                set_acc(uint8_t((acc() << 1) | ((acc() & 0x80) >> 7)));
                break;
            case 0x24: {  // add a,#byte
                const uint8_t data = fetch();
                const uint8_t result = uint8_t(acc() + data);
                add_flags(acc(), data, 0);
                set_acc(result);
                break;
            }
            case 0x25: {  // add a,mem
                const uint8_t data = iram_r(fetch());
                const uint8_t result = uint8_t(acc() + data);
                add_flags(acc(), data, 0);
                set_acc(result);
                break;
            }
            case 0x26:
            case 0x27: {  // add a,@ri
                const uint8_t data = iram_ir(r_reg(uint8_t(opcode & 1)));
                const uint8_t result = uint8_t(acc() + data);
                add_flags(acc(), data, 0);
                set_acc(result);
                break;
            }
            case 0x28:
            case 0x29:
            case 0x2a:
            case 0x2b:
            case 0x2c:
            case 0x2d:
            case 0x2e:
            case 0x2f: {  // add a,rn
                const uint8_t data = r_reg(uint8_t(opcode & 7));
                const uint8_t result = uint8_t(acc() + data);
                add_flags(acc(), data, 0);
                set_acc(result);
                break;
            }
            case 0x30: {  // jnb
                const uint8_t pos = fetch();
                const uint8_t offset = fetch();
                if (bit_address_r(pos) == 0) pc_ = uint16_t(pc_ + int8_t(offset));
                break;
            }
            case 0x32:  // reti
                pop_pc();
                clear_irqs();
                break;
            case 0x33: {  // rlc a
                const uint8_t value = acc();
                set_acc(uint8_t((value << 1) | (psw_.c ? 1 : 0)));
                psw_.c = (value & 0x80) != 0;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0x34: {  // addc a,#byte
                const uint8_t data = fetch();
                const uint8_t carry = psw_.c ? 1 : 0;
                const uint8_t result = uint8_t(acc() + data + carry);
                add_flags(acc(), data, carry);
                set_acc(result);
                break;
            }
            case 0x35: {  // addc a,mem
                const uint8_t data = iram_r(fetch());
                const uint8_t carry = psw_.c ? 1 : 0;
                const uint8_t result = uint8_t(acc() + data + carry);
                add_flags(acc(), data, carry);
                set_acc(result);
                break;
            }
            case 0x36:
            case 0x37: {  // addc a,@ri
                const uint8_t data = iram_ir(r_reg(uint8_t(opcode & 1)));
                const uint8_t carry = psw_.c ? 1 : 0;
                const uint8_t result = uint8_t(acc() + data + carry);
                add_flags(acc(), data, carry);
                set_acc(result);
                break;
            }
            case 0x38:
            case 0x39:
            case 0x3a:
            case 0x3b:
            case 0x3c:
            case 0x3d:
            case 0x3e:
            case 0x3f: {  // addc a,rn
                const uint8_t data = r_reg(uint8_t(opcode & 7));
                const uint8_t carry = psw_.c ? 1 : 0;
                const uint8_t result = uint8_t(acc() + data + carry);
                add_flags(acc(), data, carry);
                set_acc(result);
                break;
            }
            case 0x40: {  // jc
                const uint8_t offset = fetch();
                if (psw_.c) pc_ = uint16_t(pc_ + int8_t(offset));
                break;
            }
            case 0x42: {  // orl mem,a
                rwm_ = true;
                const uint8_t pos = fetch();
                iram_w(pos, uint8_t(iram_r(pos) | acc()));
                rwm_ = false;
                break;
            }
            case 0x43: {  // orl mem,#byte
                rwm_ = true;
                const uint8_t pos = fetch();
                const uint8_t data = fetch();
                iram_w(pos, uint8_t(iram_r(pos) | data));
                rwm_ = false;
                break;
            }
            case 0x44:  // orl a,#byte
                set_acc(uint8_t(acc() | fetch()));
                break;
            case 0x45:  // orl a,mem
                set_acc(uint8_t(acc() | iram_r(fetch())));
                break;
            case 0x46:
            case 0x47:  // orl a,@ri
                set_acc(uint8_t(acc() | iram_ir(r_reg(uint8_t(opcode & 1)))));
                break;
            case 0x48:
            case 0x49:
            case 0x4a:
            case 0x4b:
            case 0x4c:
            case 0x4d:
            case 0x4e:
            case 0x4f:  // orl a,rn
                set_acc(uint8_t(acc() | r_reg(uint8_t(opcode & 7))));
                break;
            case 0x50: {  // jnc
                const uint8_t offset = fetch();
                if (!psw_.c) pc_ = uint16_t(pc_ + int8_t(offset));
                break;
            }
            case 0x52: {  // anl mem,a
                rwm_ = true;
                const uint8_t pos = fetch();
                iram_w(pos, uint8_t(iram_r(pos) & acc()));
                rwm_ = false;
                break;
            }
            case 0x53: {  // anl mem,#byte
                rwm_ = true;
                const uint8_t pos = fetch();
                const uint8_t data = fetch();
                iram_w(pos, uint8_t(iram_r(pos) & data));
                rwm_ = false;
                break;
            }
            case 0x54:  // anl a,#byte
                set_acc(uint8_t(acc() & fetch()));
                break;
            case 0x55:  // anl a,mem
                set_acc(uint8_t(acc() & iram_r(fetch())));
                break;
            case 0x56:
            case 0x57:  // anl a,@ri
                set_acc(uint8_t(acc() & iram_ir(r_reg(uint8_t(opcode & 1)))));
                break;
            case 0x58:
            case 0x59:
            case 0x5a:
            case 0x5b:
            case 0x5c:
            case 0x5d:
            case 0x5e:
            case 0x5f:  // anl a,rn
                set_acc(uint8_t(acc() & r_reg(uint8_t(opcode & 7))));
                break;
            case 0x60: {  // jz
                const uint8_t offset = fetch();
                if (acc() == 0) pc_ = uint16_t(pc_ + int8_t(offset));
                break;
            }
            case 0x62: {  // xrl mem,a
                rwm_ = true;
                const uint8_t pos = fetch();
                iram_w(pos, uint8_t(iram_r(pos) ^ acc()));
                rwm_ = false;
                break;
            }
            case 0x63: {  // xrl mem,#byte
                rwm_ = true;
                const uint8_t pos = fetch();
                const uint8_t data = fetch();
                iram_w(pos, uint8_t(iram_r(pos) ^ data));
                rwm_ = false;
                break;
            }
            case 0x64:  // xrl a,#byte
                set_acc(uint8_t(acc() ^ fetch()));
                break;
            case 0x65:  // xrl a,mem
                set_acc(uint8_t(acc() ^ iram_r(fetch())));
                break;
            case 0x66:
            case 0x67:  // xrl a,@ri
                set_acc(uint8_t(acc() ^ iram_ir(r_reg(uint8_t(opcode & 1)))));
                break;
            case 0x68:
            case 0x69:
            case 0x6a:
            case 0x6b:
            case 0x6c:
            case 0x6d:
            case 0x6e:
            case 0x6f:  // xrl a,rn
                set_acc(uint8_t(acc() ^ r_reg(uint8_t(opcode & 7))));
                break;
            case 0x70: {  // jnz
                const uint8_t offset = fetch();
                if (acc() != 0) pc_ = uint16_t(pc_ + int8_t(offset));
                break;
            }
            case 0x72: {  // orl c,bit
                const uint8_t pos = fetch();
                psw_.c = psw_.c || bit_address_r(pos) != 0;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0x73:  // jmp @a+dptr
                pc_ = uint16_t(((sfr_[kAddrDph] << 8) | sfr_[kAddrDpl]) + acc());
                break;
            case 0x74:  // mov a,#byte
                set_acc(fetch());
                break;
            case 0x75: {  // mov mem,#byte
                const uint8_t pos = fetch();
                iram_w(pos, fetch());
                break;
            }
            case 0x76:
            case 0x77:  // mov @ri,#byte
                iram_iw(r_reg(uint8_t(opcode & 1)), fetch());
                break;
            case 0x78:
            case 0x79:
            case 0x7a:
            case 0x7b:
            case 0x7c:
            case 0x7d:
            case 0x7e:
            case 0x7f:  // mov rn,#byte
                set_reg(uint8_t(opcode & 7), fetch());
                break;
            case 0x80: {  // sjmp
                const uint8_t offset = fetch();
                pc_ = uint16_t(pc_ + int8_t(offset));
                break;
            }
            case 0x82: {  // anl c,bit
                const uint8_t pos = fetch();
                psw_.c = psw_.c && bit_address_r(pos) != 0;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0x83:  // movc a,@a+pc
                set_acc(peek(uint16_t(pc_ + acc())));
                break;
            case 0x84:  // div ab
                if (sfr_[kAddrB] == 0) {
                    psw_.o = true;
                } else {
                    const uint8_t quotient = uint8_t(acc() / sfr_[kAddrB]);
                    const uint8_t remainder = uint8_t(acc() % sfr_[kAddrB]);
                    sfr_[kAddrAcc] = quotient;
                    sfr_[kAddrB] = remainder;
                    psw_.o = false;
                }
                psw_.c = false;
                sfr_[kAddrPsw] = get_psw();
                break;
            case 0x85: {  // mov mem,mem
                const uint8_t source = fetch();
                const uint8_t target = fetch();
                iram_w(target, iram_r(source));
                break;
            }
            case 0x86:
            case 0x87: {  // mov mem,@ri
                const uint8_t pos = fetch();
                iram_w(pos, iram_ir(r_reg(uint8_t(opcode & 1))));
                break;
            }
            case 0x88:
            case 0x89:
            case 0x8a:
            case 0x8b:
            case 0x8c:
            case 0x8d:
            case 0x8e:
            case 0x8f: {  // mov mem,rn
                const uint8_t pos = fetch();
                iram_w(pos, r_reg(uint8_t(opcode & 7)));
                break;
            }
            case 0x90:  // mov dptr,#word
                sfr_[kAddrDph] = fetch();
                sfr_[kAddrDpl] = fetch();
                break;
            case 0x92: {  // mov bit,c
                rwm_ = true;
                const uint8_t pos = fetch();
                bit_address_w(pos, psw_.c ? 1 : 0);
                rwm_ = false;
                break;
            }
            case 0x93:  // movc a,@a+dptr
                set_acc(peek(uint16_t(((sfr_[kAddrDph] << 8) | sfr_[kAddrDpl]) + acc())));
                break;
            case 0x94: {  // subb a,#byte
                const uint8_t data = fetch();
                const uint8_t carry = psw_.c ? 1 : 0;
                const uint8_t result = uint8_t(acc() - data - carry);
                sub_flags(acc(), data, carry);
                set_acc(result);
                break;
            }
            case 0x95: {  // subb a,mem
                const uint8_t data = iram_r(fetch());
                const uint8_t carry = psw_.c ? 1 : 0;
                const uint8_t result = uint8_t(acc() - data - carry);
                sub_flags(acc(), data, carry);
                set_acc(result);
                break;
            }
            case 0x96:
            case 0x97: {  // subb a,@ri
                const uint8_t data = iram_ir(r_reg(uint8_t(opcode & 1)));
                const uint8_t carry = psw_.c ? 1 : 0;
                const uint8_t result = uint8_t(acc() - data - carry);
                sub_flags(acc(), data, carry);
                set_acc(result);
                break;
            }
            case 0x98:
            case 0x99:
            case 0x9a:
            case 0x9b:
            case 0x9c:
            case 0x9d:
            case 0x9e:
            case 0x9f: {  // subb a,rn
                const uint8_t data = r_reg(uint8_t(opcode & 7));
                const uint8_t carry = psw_.c ? 1 : 0;
                const uint8_t result = uint8_t(acc() - data - carry);
                sub_flags(acc(), data, carry);
                set_acc(result);
                break;
            }
            case 0xa0: {  // orl c,/bit
                const uint8_t pos = fetch();
                psw_.c = psw_.c || bit_address_r(pos) == 0;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0xa2: {  // mov c,bit
                const uint8_t pos = fetch();
                psw_.c = bit_address_r(pos) != 0;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0xa3: {  // inc dptr
                const uint16_t dptr = uint16_t(((sfr_[kAddrDph] << 8) | sfr_[kAddrDpl]) + 1);
                sfr_[kAddrDph] = uint8_t(dptr >> 8);
                sfr_[kAddrDpl] = uint8_t(dptr & 0xff);
                break;
            }
            case 0xa4: {  // mul ab
                const uint16_t result = uint16_t(acc() * sfr_[kAddrB]);
                sfr_[kAddrB] = uint8_t(result >> 8);
                sfr_[kAddrAcc] = uint8_t(result & 0xff);
                calc_parity_ = true;
                psw_.o = (result & 0xff00) != 0;
                psw_.c = false;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0xa6:
            case 0xa7: {  // mov @ri,mem
                const uint8_t pos = fetch();
                iram_iw(r_reg(uint8_t(opcode & 1)), iram_r(pos));
                break;
            }
            case 0xa8:
            case 0xa9:
            case 0xaa:
            case 0xab:
            case 0xac:
            case 0xad:
            case 0xae:
            case 0xaf:  // mov rn,mem
                set_reg(uint8_t(opcode & 7), iram_r(fetch()));
                break;
            case 0xb0: {  // anl c,/bit
                const uint8_t pos = fetch();
                psw_.c = psw_.c && bit_address_r(pos) == 0;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0xb2: {  // cpl bit
                rwm_ = true;
                const uint8_t pos = fetch();
                bit_address_w(pos, uint8_t(~bit_address_r(pos) & 1));
                rwm_ = false;
                break;
            }
            case 0xb3:  // cpl c
                psw_.c = !psw_.c;
                sfr_[kAddrPsw] = get_psw();
                break;
            case 0xb4: {  // cjne a,#byte,rel
                const uint8_t data = fetch();
                const uint8_t offset = fetch();
                if (acc() != data) pc_ = uint16_t(pc_ + int8_t(offset));
                psw_.c = acc() < data;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0xb5: {  // cjne a,mem,rel
                const uint8_t data = iram_r(fetch());
                const uint8_t offset = fetch();
                if (acc() != data) pc_ = uint16_t(pc_ + int8_t(offset));
                psw_.c = acc() < data;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0xb6:
            case 0xb7: {  // cjne @ri,#byte,rel
                const uint8_t data = fetch();
                const uint8_t offset = fetch();
                const uint8_t value = iram_ir(r_reg(uint8_t(opcode & 1)));
                if (value != data) pc_ = uint16_t(pc_ + int8_t(offset));
                psw_.c = value < data;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0xb8:
            case 0xb9:
            case 0xba:
            case 0xbb:
            case 0xbc:
            case 0xbd:
            case 0xbe:
            case 0xbf: {  // cjne rn,#byte,rel
                const uint8_t data = fetch();
                const uint8_t offset = fetch();
                const uint8_t value = r_reg(uint8_t(opcode & 7));
                if (value != data) pc_ = uint16_t(pc_ + int8_t(offset));
                psw_.c = value < data;
                sfr_[kAddrPsw] = get_psw();
                break;
            }
            case 0xc0: {  // push
                const uint8_t pos = fetch();
                const uint8_t sp = uint8_t(sfr_[kAddrSp] + 1);
                sfr_[kAddrSp] = sp;
                iram_iw(sp, iram_r(pos));
                break;
            }
            case 0xc2: {  // clr bit
                const uint8_t pos = fetch();
                rwm_ = true;
                bit_address_w(pos, 0);
                rwm_ = false;
                break;
            }
            case 0xc3:  // clr c
                psw_.c = false;
                sfr_[kAddrPsw] = get_psw();
                break;
            case 0xc4:  // swap a
                set_acc(uint8_t(((acc() & 0x0f) << 4) | ((acc() & 0xf0) >> 4)));
                break;
            case 0xc5: {  // xch a,mem
                const uint8_t pos = fetch();
                const uint8_t value = iram_r(pos);
                const uint8_t old = acc();
                set_acc(value);
                iram_w(pos, old);
                break;
            }
            case 0xc6:
            case 0xc7: {  // xch a,@ri
                const uint8_t pos = r_reg(uint8_t(opcode & 1));
                const uint8_t value = iram_ir(pos);
                const uint8_t old = acc();
                set_acc(value);
                iram_iw(pos, old);
                break;
            }
            case 0xc8:
            case 0xc9:
            case 0xca:
            case 0xcb:
            case 0xcc:
            case 0xcd:
            case 0xce:
            case 0xcf: {  // xch a,rn
                const uint8_t value = r_reg(uint8_t(opcode & 7));
                const uint8_t old = acc();
                set_acc(value);
                set_reg(uint8_t(opcode & 7), old);
                break;
            }
            case 0xd0: {  // pop
                const uint8_t pos = fetch();
                iram_w(pos, iram_ir(sfr_[kAddrSp]));
                sfr_[kAddrSp] = uint8_t(sfr_[kAddrSp] - 1);
                break;
            }
            case 0xd2: {  // setb bit
                const uint8_t pos = fetch();
                rwm_ = true;
                bit_address_w(pos, 1);
                rwm_ = false;
                break;
            }
            case 0xd3:  // setb c
                psw_.c = true;
                sfr_[kAddrPsw] = get_psw();
                break;
            case 0xd4: {  // da a
                uint16_t value = acc();
                if (psw_.ac || (value & 0x0f) > 0x09) value = uint16_t(value + 0x06);
                if (psw_.c || (value & 0xf0) > 0x90 || (value & 0xff00) != 0) {
                    value = uint16_t(value + 0x60);
                }
                set_acc(uint8_t(value & 0xff));
                if ((value & 0xff00) != 0) {
                    psw_.c = true;
                    sfr_[kAddrPsw] = get_psw();
                }
                break;
            }
            case 0xd5: {  // djnz mem,rel
                rwm_ = true;
                const uint8_t pos = fetch();
                const uint8_t offset = fetch();
                iram_w(pos, uint8_t(iram_r(pos) - 1));
                if (iram_r(pos) != 0) pc_ = uint16_t(pc_ + int8_t(offset));
                rwm_ = false;
                break;
            }
            case 0xd6:
            case 0xd7: {  // xchd a,@ri
                const uint8_t pos = r_reg(uint8_t(opcode & 1));
                const uint8_t value = iram_ir(pos);
                const uint8_t old = acc();
                set_acc(uint8_t((old & 0xf0) | (value & 0x0f)));
                iram_iw(pos, uint8_t((value & 0xf0) | (old & 0x0f)));
                break;
            }
            case 0xd8:
            case 0xd9:
            case 0xda:
            case 0xdb:
            case 0xdc:
            case 0xdd:
            case 0xde:
            case 0xdf: {  // djnz rn,rel
                const uint8_t offset = fetch();
                const uint8_t value = uint8_t(r_reg(uint8_t(opcode & 7)) - 1);
                set_reg(uint8_t(opcode & 7), value);
                if (value != 0) pc_ = uint16_t(pc_ + int8_t(offset));
                break;
            }
            case 0xe0: {  // movx a,@dptr
                const uint16_t address = uint16_t((sfr_[kAddrDph] << 8) | sfr_[kAddrDpl]);
                set_acc(read_external_ ? read_external_(address) : 0xff);
                break;
            }
            case 0xe2:
            case 0xe3: {  // movx a,@ri
                const uint16_t address =
                    uint16_t((sfr_[kAddrP2] << 8) | r_reg(uint8_t(opcode & 1)));
                set_acc(read_external_ ? read_external_(address) : 0xff);
                break;
            }
            case 0xe4:  // clr a
                set_acc(0);
                break;
            case 0xe5:  // mov a,mem
                set_acc(iram_r(fetch()));
                break;
            case 0xe6:
            case 0xe7:  // mov a,@ri
                set_acc(iram_ir(r_reg(uint8_t(opcode & 1))));
                break;
            case 0xe8:
            case 0xe9:
            case 0xea:
            case 0xeb:
            case 0xec:
            case 0xed:
            case 0xee:
            case 0xef:  // mov a,rn
                set_acc(r_reg(uint8_t(opcode & 7)));
                break;
            case 0xf0: {  // movx @dptr,a
                const uint16_t address = uint16_t((sfr_[kAddrDph] << 8) | sfr_[kAddrDpl]);
                if (write_external_) write_external_(address, acc());
                break;
            }
            case 0xf2:
            case 0xf3: {  // movx @ri,a
                const uint16_t address =
                    uint16_t((sfr_[kAddrP2] << 8) | r_reg(uint8_t(opcode & 1)));
                if (write_external_) write_external_(address, acc());
                break;
            }
            case 0xf4:  // cpl a
                set_acc(uint8_t(~acc()));
                break;
            case 0xf5:  // mov mem,a
                iram_w(fetch(), acc());
                break;
            case 0xf6:
            case 0xf7:  // mov @ri,a
                iram_iw(r_reg(uint8_t(opcode & 1)), acc());
                break;
            default:  // 0xf8-0xff: mov rn,a
                set_reg(uint8_t(opcode & 7), acc());
                break;
        }
        const int spent = kCycles[opcode] + irq_cycles;
        executed += spent;
        update_timer_t0(spent);
        update_timer_t1(spent);
    }
    return executed;
}

}  // namespace dsp
