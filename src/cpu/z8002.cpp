// Zilog Z8002 core, adapted from MAME src/devices/cpu/z8000
// (BSD-3-Clause; Juergen Buchmueller, Ernesto Corvi).

#include "cpu/z8002.h"

#include <cassert>
#include <cstdio>
#include <cstring>

// Little-endian host: overlay of B/W/L/Q matches MAME's BYTE*_XOR_BE mapping.
#define BYTE8_XOR_BE(a) ((a) ^ 1)
#define BYTE4_XOR_BE(a) ((a) ^ 1)
#define BYTE_XOR_BE(a) ((a) ^ 1)

#include "cpu/z8000/z8000cpu.h"

namespace dsp {
namespace {

constexpr int kClearLine = 0;
constexpr int kAssertLine = 1;

}  // namespace

#define CLEAR_LINE kClearLine
#define ASSERT_LINE kAssertLine
#define LOG(...) ((void)0)
#define logerror(...) ((void)0)
#define BIT(x, n) (((x) >> (n)) & 1)

#include "cpu/z8000/z8000dab.h"

uint16_t z8002_device::z8000_exec[0x10000];
uint8_t z8002_device::z8000_zsp[256];
bool z8002_device::tables_ready_ = false;

z8002_device::z8002_device(uint32_t clock) : clock_(clock) {
    init_tables();
}

void z8002_device::set_memory_handlers(Read8Handler read, Write8Handler write) {
    read_ = std::move(read);
    write_ = std::move(write);
}

uint16_t z8002_device::rw(int n) const {
    return m_regs.W[BYTE4_XOR_BE(n & 15)];
}

void z8002_device::init_tables() {
    if (tables_ready_) return;
    for (int i = 0; i < 256; i++) {
        z8000_zsp[i] = uint8_t(((i == 0) ? F_Z : 0) | ((i & 128) ? F_S : 0) |
                               (((((i >> 7) ^ (i >> 6) ^ (i >> 5) ^ (i >> 4) ^ (i >> 3) ^
                                   (i >> 2) ^ (i >> 1) ^ i) &
                                  1)
                                     ? 0
                                     : F_PV)));
    }
    for (const Z8000_init* opc = table; opc->size; opc++) {
        for (uint32_t val = uint32_t(opc->beg); val <= uint32_t(opc->end); val += uint32_t(opc->step)) {
            z8000_exec[val] = uint16_t(opc - table);
        }
    }
    tables_ready_ = true;
}

void z8002_device::clear_internal_state() {
    m_op[0] = m_op[1] = m_op[2] = m_op[3] = 0;
    m_ppc = 0;
    m_pc = 0;
    m_psapseg = 0;
    m_psapoff = 0;
    m_fcw = 0;
    m_refresh = 0;
    m_nspseg = 0;
    m_nspoff = 0;
    m_irq_req = 0;
    m_irq_vec = 0;
    m_op_valid = 0;
    m_regs.Q[0] = m_regs.Q[1] = m_regs.Q[2] = m_regs.Q[3] = 0;
    m_nmi_state = 0;
    m_irq_state[0] = m_irq_state[1] = m_irq_state[2] = 0;
    m_mi = 0;
    m_halt = false;
}

void z8002_device::reset() {
    clear_internal_state();
    m_irq_req |= Z8000_RESET;
    m_refresh &= 0x7fff;
    m_halt = false;
}

void z8002_device::set_reset_line(IrqLine state) {
    const bool held = state != IrqLine::Clear;
    if (held && !reset_held_) {
        reset();
    }
    reset_held_ = held;
}

void z8002_device::set_nvi(IrqLine state) {
    const int line = (state == IrqLine::Clear) ? CLEAR_LINE : ASSERT_LINE;
    // MAME only calls execute_set_input when the line changes. Re-asserting an
    // already-high NVI must not queue another request every vblank.
    if (m_irq_state[NVI_LINE] == line) return;
    m_irq_state[NVI_LINE] = line;
    if (line == CLEAR_LINE) {
        if (!(m_fcw & F_NVIE)) m_irq_req &= uint8_t(~Z8000_NVI);
    } else {
        if (m_fcw & F_NVIE) m_irq_req |= Z8000_NVI;
    }
}

uint32_t z8002_device::addr_add(uint32_t addr, uint32_t addend) {
    return (addr & 0xffff0000u) | ((addr + addend) & 0xffffu);
}

uint32_t z8002_device::addr_sub(uint32_t addr, uint32_t subtrahend) {
    return (addr & 0xffff0000u) | ((addr - subtrahend) & 0xffffu);
}

uint16_t z8002_device::RDOP() {
    uint16_t res = RDMEM_W(m_program, m_pc);
    m_pc += 2;
    return res;
}

uint32_t z8002_device::get_operand(int opnum) {
    for (int i = 0; i < opnum; i++) {
        assert(m_op_valid & (1u << i));
    }
    if (!(m_op_valid & (1u << opnum))) {
        m_op[opnum] = RDMEM_W(m_program, m_pc);
        m_pc += 2;
        m_op_valid |= (1u << opnum);
    }
    return m_op[opnum];
}

uint32_t z8002_device::get_addr_operand(int opnum) {
    for (int i = 0; i < opnum; i++) {
        assert(m_op_valid & (1u << i));
    }
    if (!(m_op_valid & (1u << opnum))) {
        uint32_t seg = RDMEM_W(m_program, m_pc);
        m_pc += 2;
        m_op[opnum] = seg;
        m_op_valid |= (1u << opnum);
    }
    return m_op[opnum];
}

uint32_t z8002_device::get_raw_addr_operand(int opnum) { return get_addr_operand(opnum); }

uint8_t z8002_device::RDMEM_B(int /*space*/, uint32_t addr) {
    addr = adjust_addr_for_nonseg_mode(addr);
    return read_ ? read_(uint16_t(addr)) : 0xff;
}

uint16_t z8002_device::RDMEM_W(int /*space*/, uint32_t addr) {
    addr = adjust_addr_for_nonseg_mode(addr) & ~1u;
    const uint8_t hi = read_ ? read_(uint16_t(addr)) : 0xff;
    const uint8_t lo = read_ ? read_(uint16_t(addr + 1)) : 0xff;
    return uint16_t((uint16_t(hi) << 8) | lo);
}

uint32_t z8002_device::RDMEM_L(int space, uint32_t addr) {
    addr = adjust_addr_for_nonseg_mode(addr) & ~1u;
    uint32_t result = uint32_t(RDMEM_W(space, addr)) << 16;
    return result + RDMEM_W(space, addr_add(addr, 2));
}

void z8002_device::WRMEM_B(int /*space*/, uint32_t addr, uint8_t value) {
    addr = adjust_addr_for_nonseg_mode(addr);
    if (write_) write_(uint16_t(addr), value);
}

void z8002_device::WRMEM_W(int /*space*/, uint32_t addr, uint16_t value) {
    addr = adjust_addr_for_nonseg_mode(addr) & ~1u;
    if (!write_) return;
    write_(uint16_t(addr), uint8_t(value >> 8));
    write_(uint16_t(addr + 1), uint8_t(value));
}

void z8002_device::WRMEM_L(int space, uint32_t addr, uint32_t value) {
    addr = adjust_addr_for_nonseg_mode(addr) & ~1u;
    WRMEM_W(space, addr, uint16_t(value >> 16));
    WRMEM_W(space, addr_add(addr, 2), uint16_t(value));
}

uint8_t z8002_device::RDPORT_B(int /*mode*/, uint16_t /*addr*/) { return 0xff; }
uint16_t z8002_device::RDPORT_W(int /*mode*/, uint16_t /*addr*/) { return 0xffff; }
void z8002_device::WRPORT_B(int /*mode*/, uint16_t /*addr*/, uint8_t /*value*/) {}
void z8002_device::WRPORT_W(int /*mode*/, uint16_t /*addr*/, uint16_t /*value*/) {}

void z8002_device::cycles(int cycles) {
    m_icount -= cycles;
    if (cycle_handler_) cycle_handler_(cycles);
}

void z8002_device::PUSH_PC() { PUSHW(SP, uint16_t(m_pc)); }

uint32_t z8002_device::GET_PC(uint32_t VEC) { return RDMEM_W(m_data, VEC + 2); }
uint32_t z8002_device::get_reset_pc() { return RDMEM_W(m_program, 4); }
uint16_t z8002_device::GET_FCW(uint32_t VEC) { return RDMEM_W(m_data, VEC); }
uint32_t z8002_device::F_SEG_Z8001() { return 0; }
uint32_t z8002_device::PSA_ADDR() { return m_psapoff; }
uint32_t z8002_device::read_irq_vector() {
    return RDMEM_W(m_data, VEC00 + 2 * (m_irq_vec & 0xff));
}

void z8002_device::Interrupt() {
    uint16_t fcw = m_fcw;

    if (m_irq_req & Z8000_RESET) {
        m_pc = get_reset_pc();
        m_irq_req &= uint8_t(Z8000_NVI | Z8000_VI);
        CHANGE_FCW(RDMEM_W(m_program, 2));
        return;
    }
    if (m_irq_req & Z8000_EPU) {
        CHANGE_FCW(uint16_t(fcw | F_S_N | F_SEG_Z8001()));
        PUSH_PC();
        PUSHW(SP, fcw);
        PUSHW(SP, uint16_t(m_op[0]));
        m_pc = GET_PC(EPU);
        m_irq_req &= uint8_t(~Z8000_EPU);
        CHANGE_FCW(GET_FCW(EPU));
        return;
    }
    if (m_irq_req & Z8000_TRAP) {
        CHANGE_FCW(uint16_t(fcw | F_S_N | F_SEG_Z8001()));
        PUSH_PC();
        PUSHW(SP, fcw);
        PUSHW(SP, uint16_t(m_op[0]));
        m_pc = GET_PC(TRAP);
        m_irq_req &= uint8_t(~Z8000_TRAP);
        CHANGE_FCW(GET_FCW(TRAP));
        return;
    }
    if (m_irq_req & Z8000_SYSCALL) {
        CHANGE_FCW(uint16_t(fcw | F_S_N | F_SEG_Z8001()));
        PUSH_PC();
        PUSHW(SP, fcw);
        PUSHW(SP, uint16_t(m_op[0]));
        m_pc = GET_PC(SYSCALL);
        m_irq_req &= uint8_t(~Z8000_SYSCALL);
        CHANGE_FCW(GET_FCW(SYSCALL));
        return;
    }
    if (m_irq_req & Z8000_NMI) {
        m_irq_vec = 0xffff;
        m_halt = false;
        CHANGE_FCW(uint16_t(fcw | F_S_N | F_SEG_Z8001()));
        PUSH_PC();
        PUSHW(SP, fcw);
        PUSHW(SP, m_irq_vec);
        m_pc = GET_PC(NMI);
        m_irq_req &= uint8_t(~Z8000_NMI);
        CHANGE_FCW(GET_FCW(NMI));
        return;
    }
    if (m_irq_req & Z8000_SEGTRAP) {
        m_irq_vec = 0xffff;
        CHANGE_FCW(uint16_t(fcw | F_S_N | F_SEG_Z8001()));
        PUSH_PC();
        PUSHW(SP, fcw);
        PUSHW(SP, m_irq_vec);
        m_pc = GET_PC(SEGTRAP);
        m_irq_req &= uint8_t(~Z8000_SEGTRAP);
        CHANGE_FCW(GET_FCW(SEGTRAP));
        return;
    }
    if ((m_irq_req & Z8000_VI) && (m_fcw & F_VIE)) {
        m_irq_vec = 0xffff;
        m_halt = false;
        CHANGE_FCW(uint16_t(fcw | F_S_N | F_SEG_Z8001()));
        PUSH_PC();
        PUSHW(SP, fcw);
        PUSHW(SP, m_irq_vec);
        m_pc = read_irq_vector();
        m_irq_req &= uint8_t(~Z8000_VI);
        CHANGE_FCW(GET_FCW(VI));
        return;
    }
    if ((m_irq_req & Z8000_NVI) && (m_fcw & F_NVIE)) {
        m_irq_vec = 0xffff;
        m_halt = false;
        CHANGE_FCW(uint16_t(fcw | F_S_N | F_SEG_Z8001()));
        PUSH_PC();
        PUSHW(SP, fcw);
        PUSHW(SP, m_irq_vec);
        m_pc = GET_PC(NVI);
        m_irq_req &= uint8_t(~Z8000_NVI);
        CHANGE_FCW(GET_FCW(NVI));
    }
}

int z8002_device::run(int cycles) {
    if (reset_held_ || !read_) return cycles;
    m_icount = cycles;
    const int start = m_icount;
    do {
        if (m_irq_req) Interrupt();
        m_ppc = m_pc;
        if (m_halt) {
            m_icount = 0;
            break;
        }
        m_op[0] = RDOP();
        m_op_valid = 1;
        const Z8000_init& exec = table[z8000_exec[m_op[0]]];
        m_icount -= exec.cycles;
        if (cycle_handler_) cycle_handler_(exec.cycles);
        (this->*exec.opcode)();
        m_op_valid = 0;
    } while (m_icount > 0);
    return start - m_icount;
}

#include "cpu/z8000/z8000ops.hxx"
#include "cpu/z8000/z8000tbl.hxx"

}  // namespace dsp
