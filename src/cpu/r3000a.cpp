// Portions derived from ProjectPSX CPU.cs (MIT License)
// Copyright (c) 2019 Pedro Cortés
// See PROJECTPSX_LICENSE / https://github.com/BluestormDNA/ProjectPSX

#include "cpu/r3000a.h"

#include <cstring>

namespace dsp {

namespace {

constexpr uint32_t kExceptionAddress[2] = {0x80000080u, 0xBFC00180u};

}  // namespace

const R3000A::OpFn R3000A::kMainTable[64] = {
    &R3000A::op_special, &R3000A::op_bcond, &R3000A::op_j,    &R3000A::op_jal,
    &R3000A::op_beq,     &R3000A::op_bne,   &R3000A::op_blez, &R3000A::op_bgtz,
    &R3000A::op_addi,    &R3000A::op_addiu, &R3000A::op_slti, &R3000A::op_sltiu,
    &R3000A::op_andi,    &R3000A::op_ori,   &R3000A::op_xori, &R3000A::op_lui,
    &R3000A::op_cop0,    &R3000A::op_nop,   &R3000A::op_cop2, &R3000A::op_nop,
    &R3000A::op_na,      &R3000A::op_na,    &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_na,      &R3000A::op_na,    &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_na,      &R3000A::op_na,    &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_lb,      &R3000A::op_lh,    &R3000A::op_lwl,  &R3000A::op_lw,
    &R3000A::op_lbu,     &R3000A::op_lhu,   &R3000A::op_lwr,  &R3000A::op_na,
    &R3000A::op_sb,      &R3000A::op_sh,    &R3000A::op_swl,  &R3000A::op_sw,
    &R3000A::op_na,      &R3000A::op_na,    &R3000A::op_swr,  &R3000A::op_na,
    &R3000A::op_nop,     &R3000A::op_nop,   &R3000A::op_lwc2, &R3000A::op_nop,
    &R3000A::op_na,      &R3000A::op_na,    &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_nop,     &R3000A::op_nop,   &R3000A::op_swc2, &R3000A::op_nop,
    &R3000A::op_na,      &R3000A::op_na,    &R3000A::op_na,   &R3000A::op_na,
};

const R3000A::OpFn R3000A::kSpecialTable[64] = {
    &R3000A::op_sll,     &R3000A::op_na,      &R3000A::op_srl,  &R3000A::op_sra,
    &R3000A::op_sllv,    &R3000A::op_na,      &R3000A::op_srlv, &R3000A::op_srav,
    &R3000A::op_jr,      &R3000A::op_jalr,    &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_syscall, &R3000A::op_break,   &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_mfhi,    &R3000A::op_mthi,    &R3000A::op_mflo, &R3000A::op_mtlo,
    &R3000A::op_na,      &R3000A::op_na,      &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_mult,    &R3000A::op_multu,   &R3000A::op_div,  &R3000A::op_divu,
    &R3000A::op_na,      &R3000A::op_na,      &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_add,     &R3000A::op_addu,    &R3000A::op_sub,  &R3000A::op_subu,
    &R3000A::op_and,     &R3000A::op_or,      &R3000A::op_xor,  &R3000A::op_nor,
    &R3000A::op_na,      &R3000A::op_na,      &R3000A::op_slt,  &R3000A::op_sltu,
    &R3000A::op_na,      &R3000A::op_na,      &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_na,      &R3000A::op_na,      &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_na,      &R3000A::op_na,      &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_na,      &R3000A::op_na,      &R3000A::op_na,   &R3000A::op_na,
    &R3000A::op_na,      &R3000A::op_na,      &R3000A::op_na,   &R3000A::op_na,
};

R3000A::R3000A() {
  reset();
}

void R3000A::set_memory_handlers(Read8 r8, Read16 r16, Read32 r32, Write8 w8, Write16 w16,
                                 Write32 w32) {
  read8_ = std::move(r8);
  read16_ = std::move(r16);
  read32_ = std::move(r32);
  write8_ = std::move(w8);
  write16_ = std::move(w16);
  write32_ = std::move(w32);
}

void R3000A::set_irq_pending(std::function<bool()> irq_pending) {
  irq_pending_ = std::move(irq_pending);
}

void R3000A::reset() {
  pc_now_ = 0;
  pc_ = 0xBFC00000u;
  pc_predictor_ = 0xBFC00004u;
  std::memset(gpr_, 0, sizeof(gpr_));
  hi_ = lo_ = 0;
  std::memset(cop0_, 0, sizeof(cop0_));
  cop0_[15] = 0x2;  // PRID
  opcode_is_branch_ = false;
  opcode_is_delay_slot_ = false;
  opcode_took_branch_ = false;
  opcode_in_delay_slot_took_branch_ = false;
  // SR.IsC cleared => loads/stores allowed (ProjectPSX zero-inits the flag
  // the other way until the first MTC0 SR; usable reset prefers IsC=0).
  dont_isolate_cache_ = true;
  instr_ = {};
  write_back_ = {};
  memory_load_ = {};
  delayed_memory_load_ = {};
  gte_.reset();
}

void R3000A::set_pc(uint32_t pc) {
  pc_ = pc;
  pc_predictor_ = pc + 4;
  pc_now_ = pc;
}

void R3000A::set_gpr(int n, uint32_t value) {
  const size_t i = static_cast<size_t>(n) & 31;
  gpr_[i] = value;
  gpr_[0] = 0;
}

int R3000A::run(int cycles) {
  int executed = 0;
  while (executed < cycles) {
    handle_interrupts();
    fetch_decode();
    if (instr_.value != 0) {
      kMainTable[instr_.opcode()](*this);
    }
    mem_access();
    write_back();
    ++executed;
  }
  return executed;
}

void R3000A::handle_interrupts() {
  // ProjectPSX peeks the next opcode and skips IRQ if it is COP2, to avoid
  // glitching titles such as Crash Bandicoot's intro.
  if (read32_) {
    const uint32_t next = read32_(pc_);
    if ((next >> 26) == 0x12u) {
      return;
    }
  }

  if (irq_pending_ && irq_pending_()) {
    cop0_[CAUSE] |= 0x400u;
  } else {
    cop0_[CAUSE] &= ~0x400u;
  }

  const bool iec = (cop0_[SR] & 0x1u) == 1;
  const uint8_t im = static_cast<uint8_t>((cop0_[SR] >> 8) & 0xFF);
  const uint8_t ip = static_cast<uint8_t>((cop0_[CAUSE] >> 8) & 0xFF);
  if (iec && (im & ip) != 0) {
    exception(*this, INTERRUPT);
  }
}

int R3000A::fetch_decode() {
  pc_now_ = pc_;
  pc_ = pc_predictor_;
  pc_predictor_ += 4;

  opcode_is_delay_slot_ = opcode_is_branch_;
  opcode_in_delay_slot_took_branch_ = opcode_took_branch_;
  opcode_is_branch_ = false;
  opcode_took_branch_ = false;

  instr_.value = load32(pc_now_);
  return 1;
}

void R3000A::mem_access() {
  // If a new load targets the same register as the pending one, the pending
  // value is overwritten / lost (amidog tests).
  if (delayed_memory_load_.register_ != memory_load_.register_) {
    gpr_[memory_load_.register_] = memory_load_.value;
  }
  memory_load_ = delayed_memory_load_;
  delayed_memory_load_.register_ = 0;
  gpr_[0] = 0;
}

void R3000A::write_back() {
  gpr_[write_back_.register_] = write_back_.value;
  write_back_.register_ = 0;
  gpr_[0] = 0;
}

void R3000A::set_gpr_delayed(uint32_t reg, uint32_t value) {
  write_back_.register_ = reg;
  write_back_.value = value;
}

void R3000A::delayed_load(R3000A& cpu, uint32_t reg, uint32_t value) {
  cpu.delayed_memory_load_.register_ = reg;
  cpu.delayed_memory_load_.value = value;
}

void R3000A::exception(R3000A& cpu, EX cause, uint32_t coprocessor) {
  const uint32_t mode = cpu.cop0_[SR] & 0x3Fu;
  cpu.cop0_[SR] &= ~0x3Fu;
  cpu.cop0_[SR] |= (mode << 2) & 0x3Fu;

  const uint32_t old_cause = cpu.cop0_[CAUSE] & 0xFF00u;
  cpu.cop0_[CAUSE] = static_cast<uint32_t>(cause) << 2;
  cpu.cop0_[CAUSE] |= old_cause;
  cpu.cop0_[CAUSE] |= coprocessor << 28;

  if (cause == INTERRUPT) {
    cpu.cop0_[EPC] = cpu.pc_;
    cpu.opcode_is_delay_slot_ = cpu.opcode_is_branch_;
    cpu.opcode_in_delay_slot_took_branch_ = cpu.opcode_took_branch_;
  } else {
    cpu.cop0_[EPC] = cpu.pc_now_;
  }

  if (cpu.opcode_is_delay_slot_) {
    cpu.cop0_[EPC] -= 4;
    cpu.cop0_[CAUSE] |= 1u << 31;
    cpu.cop0_[JUMPDEST] = cpu.pc_;
    if (cpu.opcode_in_delay_slot_took_branch_) {
      cpu.cop0_[CAUSE] |= 1u << 30;
    }
  }

  // Correct BEV select. ProjectPSX writes `SR & 0x400000 >> 22` which, due to
  // C# operator precedence, is `SR & 1` and always picks the RAM vector after
  // the SR shift above. Use the intended BEV bit (22).
  const uint32_t bev = (cpu.cop0_[SR] & 0x400000u) >> 22;
  cpu.pc_ = kExceptionAddress[bev];
  cpu.pc_predictor_ = cpu.pc_ + 4;
}

void R3000A::branch(R3000A& cpu) {
  cpu.opcode_took_branch_ = true;
  cpu.pc_predictor_ = cpu.pc_ + (cpu.instr_.imm_s() << 2);
}

uint32_t R3000A::load8(uint32_t addr) const {
  return read8_ ? read8_(addr) : 0;
}

uint32_t R3000A::load16(uint32_t addr) const {
  return read16_ ? read16_(addr) : 0;
}

uint32_t R3000A::load32(uint32_t addr) const {
  return read32_ ? read32_(addr) : 0;
}

void R3000A::store8(uint32_t addr, uint8_t value) {
  if (write8_) write8_(addr, value);
}

void R3000A::store16(uint32_t addr, uint16_t value) {
  if (write16_) write16_(addr, value);
}

void R3000A::store32(uint32_t addr, uint32_t value) {
  if (write32_) write32_(addr, value);
}

void R3000A::op_special(R3000A& cpu) {
  kSpecialTable[cpu.instr_.function()](cpu);
}

void R3000A::op_na(R3000A& cpu) {
  exception(cpu, ILLEGAL_INSTR, cpu.instr_.id());
}

void R3000A::op_bcond(R3000A& cpu) {
  cpu.opcode_is_branch_ = true;
  const uint32_t op = cpu.instr_.rt();
  const bool should_link = (op & 0x1Eu) == 0x10u;
  const bool should_branch =
      static_cast<int32_t>(cpu.gpr_[cpu.instr_.rs()] ^ (op << 31)) < 0;

  // ProjectPSX writes r31 immediately for Bcond link (bypasses writeback).
  if (should_link) cpu.gpr_[31] = cpu.pc_predictor_;
  if (should_branch) branch(cpu);
}

void R3000A::op_j(R3000A& cpu) {
  cpu.opcode_is_branch_ = true;
  cpu.opcode_took_branch_ = true;
  cpu.pc_predictor_ = (cpu.pc_predictor_ & 0xF0000000u) | (cpu.instr_.addr() << 2);
}

void R3000A::op_jal(R3000A& cpu) {
  cpu.set_gpr_delayed(31, cpu.pc_predictor_);
  op_j(cpu);
}

void R3000A::op_beq(R3000A& cpu) {
  cpu.opcode_is_branch_ = true;
  if (cpu.gpr_[cpu.instr_.rs()] == cpu.gpr_[cpu.instr_.rt()]) {
    branch(cpu);
  }
}

void R3000A::op_bne(R3000A& cpu) {
  cpu.opcode_is_branch_ = true;
  if (cpu.gpr_[cpu.instr_.rs()] != cpu.gpr_[cpu.instr_.rt()]) {
    branch(cpu);
  }
}

void R3000A::op_blez(R3000A& cpu) {
  cpu.opcode_is_branch_ = true;
  if (static_cast<int32_t>(cpu.gpr_[cpu.instr_.rs()]) <= 0) {
    branch(cpu);
  }
}

void R3000A::op_bgtz(R3000A& cpu) {
  cpu.opcode_is_branch_ = true;
  if (static_cast<int32_t>(cpu.gpr_[cpu.instr_.rs()]) > 0) {
    branch(cpu);
  }
}

void R3000A::op_addi(R3000A& cpu) {
  // CPU_EXCEPTIONS overflow check omitted (matches default ProjectPSX build).
  cpu.set_gpr_delayed(cpu.instr_.rt(),
                      cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s());
}

void R3000A::op_addiu(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rt(),
                      cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s());
}

void R3000A::op_slti(R3000A& cpu) {
  const bool condition =
      static_cast<int32_t>(cpu.gpr_[cpu.instr_.rs()]) <
      static_cast<int32_t>(cpu.instr_.imm_s());
  cpu.set_gpr_delayed(cpu.instr_.rt(), condition ? 1u : 0u);
}

void R3000A::op_sltiu(R3000A& cpu) {
  const bool condition = cpu.gpr_[cpu.instr_.rs()] < cpu.instr_.imm_s();
  cpu.set_gpr_delayed(cpu.instr_.rt(), condition ? 1u : 0u);
}

void R3000A::op_andi(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rt(), cpu.gpr_[cpu.instr_.rs()] & cpu.instr_.imm());
}

void R3000A::op_ori(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rt(), cpu.gpr_[cpu.instr_.rs()] | cpu.instr_.imm());
}

void R3000A::op_xori(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rt(), cpu.gpr_[cpu.instr_.rs()] ^ cpu.instr_.imm());
}

void R3000A::op_lui(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rt(), cpu.instr_.imm() << 16);
}

void R3000A::op_cop0(R3000A& cpu) {
  if (cpu.instr_.rs() == 0b00000) {
    mfc0(cpu);
  } else if (cpu.instr_.rs() == 0b00100) {
    mtc0(cpu);
  } else if (cpu.instr_.rs() == 0b10000) {
    rfe(cpu);
  } else {
    exception(cpu, ILLEGAL_INSTR, cpu.instr_.id());
  }
}

void R3000A::mfc0(R3000A& cpu) {
  const uint32_t mfc = cpu.instr_.rd();
  if (mfc == 3 || (mfc >= 5 && mfc <= 9) || (mfc >= 11 && mfc <= 15)) {
    delayed_load(cpu, cpu.instr_.rt(), cpu.cop0_[mfc]);
  } else {
    exception(cpu, ILLEGAL_INSTR, cpu.instr_.id());
  }
}

void R3000A::mtc0(R3000A& cpu) {
  const uint32_t value = cpu.gpr_[cpu.instr_.rt()];
  const uint32_t reg = cpu.instr_.rd();

  if (reg == CAUSE) {
    cpu.cop0_[CAUSE] &= ~0x300u;
    cpu.cop0_[CAUSE] |= value & 0x300u;
  } else if (reg == SR) {
    cpu.dont_isolate_cache_ = (value & 0x10000u) == 0;
    const bool prev_iec = (cpu.cop0_[SR] & 0x1u) == 1;
    const bool current_iec = (value & 0x1u) == 1;
    cpu.cop0_[SR] = value;

    const uint32_t im = (value >> 8) & 0x3u;
    const uint32_t ip = (cpu.cop0_[CAUSE] >> 8) & 0x3u;
    if (!prev_iec && current_iec && (im & ip) > 0) {
      cpu.pc_ = cpu.pc_predictor_;
      exception(cpu, INTERRUPT, cpu.instr_.id());
    }
  } else {
    cpu.cop0_[reg] = value;
  }
}

void R3000A::rfe(R3000A& cpu) {
  const uint32_t mode = cpu.cop0_[SR] & 0x3Fu;
  cpu.cop0_[SR] &= ~0xFu;
  cpu.cop0_[SR] |= mode >> 2;
}

void R3000A::op_cop2(R3000A& cpu) {
  if ((cpu.instr_.rs() & 0x10u) == 0) {
    switch (cpu.instr_.rs()) {
      case 0b00000:
        delayed_load(cpu, cpu.instr_.rt(), cpu.gte_.read_data(cpu.instr_.rd()));
        break;
      case 0b00010:
        delayed_load(cpu, cpu.instr_.rt(), cpu.gte_.read_control(cpu.instr_.rd()));
        break;
      case 0b00100:
        cpu.gte_.write_data(cpu.instr_.rd(), cpu.gpr_[cpu.instr_.rt()]);
        break;
      case 0b00110:
        cpu.gte_.write_control(cpu.instr_.rd(), cpu.gpr_[cpu.instr_.rt()]);
        break;
      default:
        exception(cpu, ILLEGAL_INSTR, cpu.instr_.id());
        break;
    }
  } else {
    cpu.gte_.execute(cpu.instr_.value);
  }
}

void R3000A::op_lwc2(R3000A& cpu) {
  const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
  cpu.gte_.write_data(cpu.instr_.rt(), cpu.load32(addr));
}

void R3000A::op_swc2(R3000A& cpu) {
  const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
  cpu.store32(addr, cpu.gte_.read_data(cpu.instr_.rt()));
}

void R3000A::op_lb(R3000A& cpu) {
  if (cpu.dont_isolate_cache_) {
    const uint32_t value =
        static_cast<uint32_t>(static_cast<int8_t>(cpu.load8(
            cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s())));
    delayed_load(cpu, cpu.instr_.rt(), value);
  }
}

void R3000A::op_lbu(R3000A& cpu) {
  if (cpu.dont_isolate_cache_) {
    const uint32_t value =
        cpu.load8(cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s()) & 0xFFu;
    delayed_load(cpu, cpu.instr_.rt(), value);
  }
}

void R3000A::op_lh(R3000A& cpu) {
  if (cpu.dont_isolate_cache_) {
    const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
    const uint32_t value =
        static_cast<uint32_t>(static_cast<int16_t>(cpu.load16(addr)));
    delayed_load(cpu, cpu.instr_.rt(), value);
  }
}

void R3000A::op_lhu(R3000A& cpu) {
  if (cpu.dont_isolate_cache_) {
    const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
    delayed_load(cpu, cpu.instr_.rt(), cpu.load16(addr) & 0xFFFFu);
  }
}

void R3000A::op_lw(R3000A& cpu) {
  if (cpu.dont_isolate_cache_) {
    const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
    delayed_load(cpu, cpu.instr_.rt(), cpu.load32(addr));
  }
}

void R3000A::op_lwl(R3000A& cpu) {
  const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
  const uint32_t aligned_addr = addr & 0xFFFFFFFCu;
  const uint32_t aligned_load = cpu.load32(aligned_addr);

  uint32_t lr_value = cpu.gpr_[cpu.instr_.rt()];
  if (cpu.instr_.rt() == cpu.memory_load_.register_) {
    lr_value = cpu.memory_load_.value;
  }

  const int shift = static_cast<int>((addr & 0x3u) << 3);
  const uint32_t mask = 0x00FFFFFFu >> shift;
  const uint32_t value = (lr_value & mask) | (aligned_load << (24 - shift));
  delayed_load(cpu, cpu.instr_.rt(), value);
}

void R3000A::op_lwr(R3000A& cpu) {
  const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
  const uint32_t aligned_addr = addr & 0xFFFFFFFCu;
  const uint32_t aligned_load = cpu.load32(aligned_addr);

  uint32_t lr_value = cpu.gpr_[cpu.instr_.rt()];
  if (cpu.instr_.rt() == cpu.memory_load_.register_) {
    lr_value = cpu.memory_load_.value;
  }

  const int shift = static_cast<int>((addr & 0x3u) << 3);
  const uint32_t mask = 0xFFFFFF00u << (24 - shift);
  const uint32_t value = (lr_value & mask) | (aligned_load >> shift);
  delayed_load(cpu, cpu.instr_.rt(), value);
}

void R3000A::op_sb(R3000A& cpu) {
  if (cpu.dont_isolate_cache_) {
    cpu.store8(cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s(),
               static_cast<uint8_t>(cpu.gpr_[cpu.instr_.rt()]));
  }
}

void R3000A::op_sh(R3000A& cpu) {
  if (cpu.dont_isolate_cache_) {
    const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
    cpu.store16(addr, static_cast<uint16_t>(cpu.gpr_[cpu.instr_.rt()]));
  }
}

void R3000A::op_sw(R3000A& cpu) {
  if (cpu.dont_isolate_cache_) {
    const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
    cpu.store32(addr, cpu.gpr_[cpu.instr_.rt()]);
  }
}

void R3000A::op_swr(R3000A& cpu) {
  const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
  const uint32_t aligned_addr = addr & 0xFFFFFFFCu;
  const uint32_t aligned_load = cpu.load32(aligned_addr);

  const int shift = static_cast<int>((addr & 0x3u) << 3);
  const uint32_t mask = 0x00FFFFFFu >> (24 - shift);
  const uint32_t value =
      (aligned_load & mask) | (cpu.gpr_[cpu.instr_.rt()] << shift);
  cpu.store32(aligned_addr, value);
}

void R3000A::op_swl(R3000A& cpu) {
  const uint32_t addr = cpu.gpr_[cpu.instr_.rs()] + cpu.instr_.imm_s();
  const uint32_t aligned_addr = addr & 0xFFFFFFFCu;
  const uint32_t aligned_load = cpu.load32(aligned_addr);

  const int shift = static_cast<int>((addr & 0x3u) << 3);
  const uint32_t mask = 0xFFFFFF00u << shift;
  const uint32_t value =
      (aligned_load & mask) | (cpu.gpr_[cpu.instr_.rt()] >> (24 - shift));
  cpu.store32(aligned_addr, value);
}

void R3000A::op_sll(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(),
                      cpu.gpr_[cpu.instr_.rt()] << static_cast<int>(cpu.instr_.sa()));
}

void R3000A::op_srl(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(),
                      cpu.gpr_[cpu.instr_.rt()] >> static_cast<int>(cpu.instr_.sa()));
}

void R3000A::op_sra(R3000A& cpu) {
  cpu.set_gpr_delayed(
      cpu.instr_.rd(),
      static_cast<uint32_t>(static_cast<int32_t>(cpu.gpr_[cpu.instr_.rt()]) >>
                            static_cast<int>(cpu.instr_.sa())));
}

void R3000A::op_sllv(R3000A& cpu) {
  cpu.set_gpr_delayed(
      cpu.instr_.rd(),
      cpu.gpr_[cpu.instr_.rt()] << static_cast<int>(cpu.gpr_[cpu.instr_.rs()] & 0x1Fu));
}

void R3000A::op_srlv(R3000A& cpu) {
  cpu.set_gpr_delayed(
      cpu.instr_.rd(),
      cpu.gpr_[cpu.instr_.rt()] >> static_cast<int>(cpu.gpr_[cpu.instr_.rs()] & 0x1Fu));
}

void R3000A::op_srav(R3000A& cpu) {
  cpu.set_gpr_delayed(
      cpu.instr_.rd(),
      static_cast<uint32_t>(
          static_cast<int32_t>(cpu.gpr_[cpu.instr_.rt()]) >>
          static_cast<int>(cpu.gpr_[cpu.instr_.rs()] & 0x1Fu)));
}

void R3000A::op_jr(R3000A& cpu) {
  cpu.opcode_is_branch_ = true;
  cpu.opcode_took_branch_ = true;
  cpu.pc_predictor_ = cpu.gpr_[cpu.instr_.rs()];
}

void R3000A::op_jalr(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(), cpu.pc_predictor_);
  op_jr(cpu);
}

void R3000A::op_syscall(R3000A& cpu) {
  exception(cpu, SYSCALL, cpu.instr_.id());
}

void R3000A::op_break(R3000A& cpu) {
  exception(cpu, BREAK);
}

void R3000A::op_mfhi(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(), cpu.hi_);
}

void R3000A::op_mthi(R3000A& cpu) {
  cpu.hi_ = cpu.gpr_[cpu.instr_.rs()];
}

void R3000A::op_mflo(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(), cpu.lo_);
}

void R3000A::op_mtlo(R3000A& cpu) {
  cpu.lo_ = cpu.gpr_[cpu.instr_.rs()];
}

void R3000A::op_mult(R3000A& cpu) {
  const int64_t value = static_cast<int64_t>(static_cast<int32_t>(cpu.gpr_[cpu.instr_.rs()])) *
                        static_cast<int64_t>(static_cast<int32_t>(cpu.gpr_[cpu.instr_.rt()]));
  cpu.hi_ = static_cast<uint32_t>(value >> 32);
  cpu.lo_ = static_cast<uint32_t>(value);
}

void R3000A::op_multu(R3000A& cpu) {
  const uint64_t value = static_cast<uint64_t>(cpu.gpr_[cpu.instr_.rs()]) *
                         static_cast<uint64_t>(cpu.gpr_[cpu.instr_.rt()]);
  cpu.hi_ = static_cast<uint32_t>(value >> 32);
  cpu.lo_ = static_cast<uint32_t>(value);
}

void R3000A::op_div(R3000A& cpu) {
  const int32_t n = static_cast<int32_t>(cpu.gpr_[cpu.instr_.rs()]);
  const int32_t d = static_cast<int32_t>(cpu.gpr_[cpu.instr_.rt()]);

  if (d == 0) {
    cpu.hi_ = static_cast<uint32_t>(n);
    cpu.lo_ = (n >= 0) ? 0xFFFFFFFFu : 1u;
  } else if (static_cast<uint32_t>(n) == 0x80000000u && d == -1) {
    cpu.hi_ = 0;
    cpu.lo_ = 0x80000000u;
  } else {
    cpu.hi_ = static_cast<uint32_t>(n % d);
    cpu.lo_ = static_cast<uint32_t>(n / d);
  }
}

void R3000A::op_divu(R3000A& cpu) {
  const uint32_t n = cpu.gpr_[cpu.instr_.rs()];
  const uint32_t d = cpu.gpr_[cpu.instr_.rt()];
  if (d == 0) {
    cpu.hi_ = n;
    cpu.lo_ = 0xFFFFFFFFu;
  } else {
    cpu.hi_ = n % d;
    cpu.lo_ = n / d;
  }
}

void R3000A::op_add(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(),
                      cpu.gpr_[cpu.instr_.rs()] + cpu.gpr_[cpu.instr_.rt()]);
}

void R3000A::op_addu(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(),
                      cpu.gpr_[cpu.instr_.rs()] + cpu.gpr_[cpu.instr_.rt()]);
}

void R3000A::op_sub(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(),
                      cpu.gpr_[cpu.instr_.rs()] - cpu.gpr_[cpu.instr_.rt()]);
}

void R3000A::op_subu(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(),
                      cpu.gpr_[cpu.instr_.rs()] - cpu.gpr_[cpu.instr_.rt()]);
}

void R3000A::op_and(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(),
                      cpu.gpr_[cpu.instr_.rs()] & cpu.gpr_[cpu.instr_.rt()]);
}

void R3000A::op_or(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(),
                      cpu.gpr_[cpu.instr_.rs()] | cpu.gpr_[cpu.instr_.rt()]);
}

void R3000A::op_xor(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(),
                      cpu.gpr_[cpu.instr_.rs()] ^ cpu.gpr_[cpu.instr_.rt()]);
}

void R3000A::op_nor(R3000A& cpu) {
  cpu.set_gpr_delayed(cpu.instr_.rd(),
                      ~(cpu.gpr_[cpu.instr_.rs()] | cpu.gpr_[cpu.instr_.rt()]));
}

void R3000A::op_slt(R3000A& cpu) {
  const bool condition = static_cast<int32_t>(cpu.gpr_[cpu.instr_.rs()]) <
                         static_cast<int32_t>(cpu.gpr_[cpu.instr_.rt()]);
  cpu.set_gpr_delayed(cpu.instr_.rd(), condition ? 1u : 0u);
}

void R3000A::op_sltu(R3000A& cpu) {
  const bool condition = cpu.gpr_[cpu.instr_.rs()] < cpu.gpr_[cpu.instr_.rt()];
  cpu.set_gpr_delayed(cpu.instr_.rd(), condition ? 1u : 0u);
}

}  // namespace dsp
