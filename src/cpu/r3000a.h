// Portions derived from ProjectPSX CPU.cs (MIT License)
// Copyright (c) 2019 Pedro Cortés
// See PROJECTPSX_LICENSE / https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include "cpu/psx_gte.h"

#include <cstdint>
#include <functional>

namespace dsp {

// MIPS R3000A interpreter matching ProjectPSX's CPU.cs: branch delay slots,
// load delay, COP0 (MFC0/MTC0/RFE), COP2 via PsxGte, cache-isolation bit,
// and the full primary / SPECIAL opcode tables.
class R3000A {
public:
  using Read8 = std::function<uint8_t(uint32_t)>;
  using Read16 = std::function<uint16_t(uint32_t)>;
  using Read32 = std::function<uint32_t(uint32_t)>;
  using Write8 = std::function<void(uint32_t, uint8_t)>;
  using Write16 = std::function<void(uint32_t, uint16_t)>;
  using Write32 = std::function<void(uint32_t, uint32_t)>;

  R3000A();

  void set_memory_handlers(Read8 r8, Read16 r16, Read32 r32, Write8 w8, Write16 w16,
                           Write32 w32);
  void set_irq_pending(std::function<bool()> irq_pending);

  void reset();  // PC = 0xBFC00000
  // Execute ~cycles instructions (1 cycle each). Returns cycles consumed.
  int run(int cycles);
  // ProjectPSX handleInterrupts(): update CAUSE.IP2 from irq_pending and
  // take an interrupt exception when IEC && (IM & IP).
  void handle_interrupts();

  uint32_t pc() const { return pc_; }
  void set_pc(uint32_t pc);
  uint32_t gpr(int n) const { return gpr_[static_cast<size_t>(n) & 31]; }
  void set_gpr(int n, uint32_t value);
  PsxGte& gte() { return gte_; }
  const PsxGte& gte() const { return gte_; }
  uint32_t cop0_sr() const { return cop0_[SR]; }
  uint32_t hi() const { return hi_; }
  uint32_t lo() const { return lo_; }

private:
  enum EX : uint32_t {
    INTERRUPT = 0x0,
    LOAD_ADDRESS_ERROR = 0x4,
    STORE_ADDRESS_ERROR = 0x5,
    BUS_ERROR_FETCH = 0x6,
    SYSCALL = 0x8,
    BREAK = 0x9,
    ILLEGAL_INSTR = 0xA,
    COPROCESSOR_ERROR = 0xB,
    OVERFLOW = 0xC,
  };

  static constexpr int SR = 12;
  static constexpr int CAUSE = 13;
  static constexpr int EPC = 14;
  static constexpr int BADA = 8;
  static constexpr int JUMPDEST = 6;

  struct Instr {
    uint32_t value = 0;
    uint32_t opcode() const { return value >> 26; }
    uint32_t rs() const { return (value >> 21) & 0x1F; }
    uint32_t rt() const { return (value >> 16) & 0x1F; }
    uint32_t imm() const { return value & 0xFFFFu; }
    uint32_t imm_s() const { return static_cast<uint32_t>(static_cast<int16_t>(value)); }
    uint32_t rd() const { return (value >> 11) & 0x1F; }
    uint32_t sa() const { return (value >> 6) & 0x1F; }
    uint32_t function() const { return value & 0x3F; }
    uint32_t addr() const { return value & 0x3FFFFFFu; }
    uint32_t id() const { return opcode() & 0x3; }
  };

  struct MemSlot {
    uint32_t register_ = 0;
    uint32_t value = 0;
  };

  using OpFn = void (*)(R3000A&);

  int fetch_decode();
  void mem_access();
  void write_back();
  void set_gpr_delayed(uint32_t reg, uint32_t value);
  static void delayed_load(R3000A& cpu, uint32_t reg, uint32_t value);
  static void exception(R3000A& cpu, EX cause, uint32_t coprocessor = 0);
  static void branch(R3000A& cpu);

  static void op_special(R3000A& cpu);
  static void op_bcond(R3000A& cpu);
  static void op_j(R3000A& cpu);
  static void op_jal(R3000A& cpu);
  static void op_beq(R3000A& cpu);
  static void op_bne(R3000A& cpu);
  static void op_blez(R3000A& cpu);
  static void op_bgtz(R3000A& cpu);
  static void op_addi(R3000A& cpu);
  static void op_addiu(R3000A& cpu);
  static void op_slti(R3000A& cpu);
  static void op_sltiu(R3000A& cpu);
  static void op_andi(R3000A& cpu);
  static void op_ori(R3000A& cpu);
  static void op_xori(R3000A& cpu);
  static void op_lui(R3000A& cpu);
  static void op_cop0(R3000A& cpu);
  static void op_cop2(R3000A& cpu);
  static void op_lb(R3000A& cpu);
  static void op_lh(R3000A& cpu);
  static void op_lwl(R3000A& cpu);
  static void op_lw(R3000A& cpu);
  static void op_lbu(R3000A& cpu);
  static void op_lhu(R3000A& cpu);
  static void op_lwr(R3000A& cpu);
  static void op_sb(R3000A& cpu);
  static void op_sh(R3000A& cpu);
  static void op_swl(R3000A& cpu);
  static void op_sw(R3000A& cpu);
  static void op_swr(R3000A& cpu);
  static void op_lwc2(R3000A& cpu);
  static void op_swc2(R3000A& cpu);
  static void op_nop(R3000A& /*cpu*/) {}
  static void op_na(R3000A& cpu);

  static void op_sll(R3000A& cpu);
  static void op_srl(R3000A& cpu);
  static void op_sra(R3000A& cpu);
  static void op_sllv(R3000A& cpu);
  static void op_srlv(R3000A& cpu);
  static void op_srav(R3000A& cpu);
  static void op_jr(R3000A& cpu);
  static void op_jalr(R3000A& cpu);
  static void op_syscall(R3000A& cpu);
  static void op_break(R3000A& cpu);
  static void op_mfhi(R3000A& cpu);
  static void op_mthi(R3000A& cpu);
  static void op_mflo(R3000A& cpu);
  static void op_mtlo(R3000A& cpu);
  static void op_mult(R3000A& cpu);
  static void op_multu(R3000A& cpu);
  static void op_div(R3000A& cpu);
  static void op_divu(R3000A& cpu);
  static void op_add(R3000A& cpu);
  static void op_addu(R3000A& cpu);
  static void op_sub(R3000A& cpu);
  static void op_subu(R3000A& cpu);
  static void op_and(R3000A& cpu);
  static void op_or(R3000A& cpu);
  static void op_xor(R3000A& cpu);
  static void op_nor(R3000A& cpu);
  static void op_slt(R3000A& cpu);
  static void op_sltu(R3000A& cpu);

  static void mfc0(R3000A& cpu);
  static void mtc0(R3000A& cpu);
  static void rfe(R3000A& cpu);

  uint32_t load8(uint32_t addr) const;
  uint32_t load16(uint32_t addr) const;
  uint32_t load32(uint32_t addr) const;
  void store8(uint32_t addr, uint8_t value);
  void store16(uint32_t addr, uint16_t value);
  void store32(uint32_t addr, uint32_t value);

  static const OpFn kMainTable[64];
  static const OpFn kSpecialTable[64];

  uint32_t pc_now_ = 0;
  uint32_t pc_ = 0xBFC00000u;
  uint32_t pc_predictor_ = 0xBFC00004u;
  uint32_t gpr_[32]{};
  uint32_t hi_ = 0;
  uint32_t lo_ = 0;
  uint32_t cop0_[16]{};

  bool opcode_is_branch_ = false;
  bool opcode_is_delay_slot_ = false;
  bool opcode_took_branch_ = false;
  bool opcode_in_delay_slot_took_branch_ = false;
  bool dont_isolate_cache_ = true;

  Instr instr_{};
  MemSlot write_back_{};
  MemSlot memory_load_{};
  MemSlot delayed_memory_load_{};

  PsxGte gte_{};

  Read8 read8_;
  Read16 read16_;
  Read32 read32_;
  Write8 write8_;
  Write16 write16_;
  Write32 write32_;
  std::function<bool()> irq_pending_;
};

}  // namespace dsp
