// DEC T-11 opcode implementations, ported from MAME's t11ops.hxx
// (license:BSD-3-Clause, copyright-holders:Aaron Giles).



/* given a register index 'r', this computes the effective address for a byte-sized operation
   and puts the result in 'ea' */
#define MAKE_EAB_RGD(r) ea = REGD(r)
#define MAKE_EAB_IN(r)  ea = REGD(r); REGW(r) += ((r) < 6 ? 1 : 2)
#define MAKE_EAB_INS(r) ea = REGD(r); REGW(r) += ((r) < 6 ? 1 : 2)
#define MAKE_EAB_IND(r) ea = REGD(r); REGW(r) += 2; ea = RWORD(ea)
#define MAKE_EAB_DE(r)  REGW(r) -= ((r) < 6 ? 1 : 2); ea = REGD(r)
#define MAKE_EAB_DED(r) REGW(r) -= 2; ea = REGD(r); ea = RWORD(ea)
#define MAKE_EAB_IX(r)  ea = ROPCODE(); ea = (ea + REGD(r)) & 0xffff
#define MAKE_EAB_IXD(r) ea = ROPCODE(); ea = (ea + REGD(r)) & 0xffff; ea = RWORD(ea)

/* given a register index 'r', this computes the effective address for a word-sized operation
   and puts the result in 'ea' */
/* note that word accesses ignore the low bit!! this fixes APB! */
#define MAKE_EAW_RGD(r) MAKE_EAB_RGD(r)
#define MAKE_EAW_IN(r)  ea = REGD(r); REGW(r) += 2
#define MAKE_EAW_IND(r) MAKE_EAB_IND(r)
#define MAKE_EAW_DE(r)  REGW(r) -= 2; ea = REGD(r)
#define MAKE_EAW_DED(r) MAKE_EAB_DED(r)
#define MAKE_EAW_IX(r)  MAKE_EAB_IX(r)
#define MAKE_EAW_IXD(r) MAKE_EAB_IXD(r)

/* extracts the source/destination register index from the opcode into 'sreg' or 'dreg' */
#define GET_SREG sreg = (op >> 6) & 7
#define GET_DREG dreg = op & 7

/* for a byte-sized source operand: extracts 'sreg', computes 'ea', and loads the value into 'source' */
#define GET_SB_RG  GET_SREG; source = REGB(sreg)
#define GET_SB_RGD GET_SREG; MAKE_EAB_RGD(sreg); source = RBYTE(ea)
#define GET_SB_IN  GET_SREG; if (sreg == 7) { source = ROPCODE(); } else { MAKE_EAB_IN(sreg); source = RBYTE(ea); }
#define GET_SB_IND GET_SREG; if (sreg == 7) { ea = ROPCODE(); } else { MAKE_EAB_IND(sreg); } source = RBYTE(ea)
#define GET_SB_DE  GET_SREG; MAKE_EAB_DE(sreg); source = RBYTE(ea)
#define GET_SB_DED GET_SREG; MAKE_EAB_DED(sreg); source = RBYTE(ea)
#define GET_SB_IX  GET_SREG; MAKE_EAB_IX(sreg); source = RBYTE(ea)
#define GET_SB_IXD GET_SREG; MAKE_EAB_IXD(sreg); source = RBYTE(ea)

/* for a word-sized source operand: extracts 'sreg', computes 'ea', and loads the value into 'source' */
#define GET_SW_RG  GET_SREG; source = REGD(sreg)
#define GET_SW_RGD GET_SREG; MAKE_EAW_RGD(sreg); source = RWORD(ea)
#define GET_SW_IN  GET_SREG; if (sreg == 7) { source = ROPCODE(); } else { MAKE_EAW_IN(sreg); source = RWORD(ea); }
#define GET_SW_IND GET_SREG; if (sreg == 7) { ea = ROPCODE(); } else { MAKE_EAW_IND(sreg); } source = RWORD(ea)
#define GET_SW_DE  GET_SREG; MAKE_EAW_DE(sreg); source = RWORD(ea)
#define GET_SW_DED GET_SREG; MAKE_EAW_DED(sreg); source = RWORD(ea)
#define GET_SW_IX  GET_SREG; MAKE_EAW_IX(sreg); source = RWORD(ea)
#define GET_SW_IXD GET_SREG; MAKE_EAW_IXD(sreg); source = RWORD(ea)

/* for a byte-sized destination operand: extracts 'dreg', computes 'ea', and loads the value into 'dest' */
#define GET_DB_RG  GET_DREG; dest = REGB(dreg)
#define GET_DB_RGD GET_DREG; MAKE_EAB_RGD(dreg); dest = RBYTE(ea)
#define GET_DB_IN  GET_DREG; MAKE_EAB_IN(dreg); dest = RBYTE(ea)
#define GET_DB_IND GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAB_IND(dreg); } dest = RBYTE(ea)
#define GET_DB_DE  GET_DREG; MAKE_EAB_DE(dreg); dest = RBYTE(ea)
#define GET_DB_DED GET_DREG; MAKE_EAB_DED(dreg); dest = RBYTE(ea)
#define GET_DB_IX  GET_DREG; MAKE_EAB_IX(dreg); dest = RBYTE(ea)
#define GET_DB_IXD GET_DREG; MAKE_EAB_IXD(dreg); dest = RBYTE(ea)

/* for a word-sized destination operand: extracts 'dreg', computes 'ea', and loads the value into 'dest' */
#define GET_DW_RG  GET_DREG; dest = REGD(dreg)
#define GET_DW_RGD GET_DREG; MAKE_EAW_RGD(dreg); dest = RWORD(ea)
#define GET_DW_IN  GET_DREG; MAKE_EAW_IN(dreg); dest = RWORD(ea)
#define GET_DW_IND GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAW_IND(dreg); } dest = RWORD(ea)
#define GET_DW_DE  GET_DREG; MAKE_EAW_DE(dreg); dest = RWORD(ea)
#define GET_DW_DED GET_DREG; MAKE_EAW_DED(dreg); dest = RWORD(ea)
#define GET_DW_IX  GET_DREG; MAKE_EAW_IX(dreg); dest = RWORD(ea)
#define GET_DW_IXD GET_DREG; MAKE_EAW_IXD(dreg); dest = RWORD(ea)

/* writes a value to a previously computed 'ea' */
#define PUT_DB_EA(v) WBYTE(ea, (v))
#define PUT_DW_EA(v) WWORD(ea, (v))

/* writes a value to a previously computed 'dreg' register */
#define PUT_DB_DREG(v) REGB(dreg) = (v)
#define PUT_DW_DREG(v) REGW(dreg) = (v)

/* for a byte-sized destination operand: extracts 'dreg', computes 'ea', and writes 'v' to it */
#define PUT_DB_RG(v)  GET_DREG; REGB(dreg) = (v)
#define PUT_DB_RGD(v) GET_DREG; MAKE_EAB_RGD(dreg); WBYTE(ea, (v))
#define PUT_DB_IN(v)  GET_DREG; MAKE_EAB_IN(dreg); WBYTE(ea, (v))
#define PUT_DB_IND(v) GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAB_IND(dreg); } WBYTE(ea, (v))
#define PUT_DB_DE(v)  GET_DREG; MAKE_EAB_DE(dreg); WBYTE(ea, (v))
#define PUT_DB_DED(v) GET_DREG; MAKE_EAB_DED(dreg); WBYTE(ea, (v))
#define PUT_DB_IX(v)  GET_DREG; MAKE_EAB_IX(dreg); WBYTE(ea, (v))
#define PUT_DB_IXD(v) GET_DREG; MAKE_EAB_IXD(dreg); WBYTE(ea, (v))

/* special bus sequence for MOV, CLR, SXT */
#define PUT_DBT_RGD(v) GET_DREG; MAKE_EAB_RGD(dreg); RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_IN(v)  GET_DREG; MAKE_EAB_IN(dreg); RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_IND(v) GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAB_IND(dreg); } RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_DE(v)  GET_DREG; MAKE_EAB_DE(dreg); RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_DED(v) GET_DREG; MAKE_EAB_DED(dreg); RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_IX(v)  GET_DREG; MAKE_EAB_IX(dreg); RBYTE(ea); WBYTE(ea, (v))
#define PUT_DBT_IXD(v) GET_DREG; MAKE_EAB_IXD(dreg); RBYTE(ea); WBYTE(ea, (v))

/* for a word-sized destination operand: extracts 'dreg', computes 'ea', and writes 'v' to it */
#define PUT_DW_RG(v)  GET_DREG; REGW(dreg) = (v)
#define PUT_DW_RGD(v) GET_DREG; MAKE_EAW_RGD(dreg); WWORD(ea, (v))
#define PUT_DW_IN(v)  GET_DREG; MAKE_EAW_IN(dreg); WWORD(ea, (v))
#define PUT_DW_IND(v) GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAW_IND(dreg); } WWORD(ea, (v))
#define PUT_DW_DE(v)  GET_DREG; MAKE_EAW_DE(dreg); WWORD(ea, (v))
#define PUT_DW_DED(v) GET_DREG; MAKE_EAW_DED(dreg); WWORD(ea, (v))
#define PUT_DW_IX(v)  GET_DREG; MAKE_EAW_IX(dreg); WWORD(ea, (v))
#define PUT_DW_IXD(v) GET_DREG; MAKE_EAW_IXD(dreg); WWORD(ea, (v))

/* special bus sequence for MOV, CLR, SXT */
#define PUT_DWT_RG(v)  PUT_DW_RG(v)
#define PUT_DWT_RGD(v) GET_DREG; MAKE_EAW_RGD(dreg); RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_IN(v)  GET_DREG; MAKE_EAW_IN(dreg); RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_IND(v) GET_DREG; if (dreg == 7) { ea = ROPCODE(); } else { MAKE_EAW_IND(dreg); } RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_DE(v)  GET_DREG; MAKE_EAW_DE(dreg); RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_DED(v) GET_DREG; MAKE_EAW_DED(dreg); RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_IX(v)  GET_DREG; MAKE_EAW_IX(dreg); RWORD(ea); WWORD(ea, (v))
#define PUT_DWT_IXD(v) GET_DREG; MAKE_EAW_IXD(dreg); RWORD(ea); WWORD(ea, (v))

/* flag clearing; must be done before setting */
#define CLR_ZV   (PSW &= ~(ZFLAG | VFLAG))
#define CLR_NZV  (PSW &= ~(NFLAG | ZFLAG | VFLAG))
#define CLR_NZVC (PSW &= ~(NFLAG | ZFLAG | VFLAG | CFLAG))

/* set individual flags byte-sized */
#define SETB_N (PSW |= (result >> 4) & 0x08)
#define SETB_Z (PSW |= ((result & 0xff) == 0) << 2)
#define SETB_V (PSW |= ((source ^ dest ^ result ^ (result >> 1)) >> 6) & 0x02)
#define SETB_C (PSW |= (result >> 8) & 0x01)
#define SETB_NZ SETB_N; SETB_Z
#define SETB_NZV SETB_N; SETB_Z; SETB_V
#define SETB_NZVC SETB_N; SETB_Z; SETB_V; SETB_C

/* set individual flags word-sized */
#define SETW_N (PSW |= (result >> 12) & 0x08)
#define SETW_Z (PSW |= ((result & 0xffff) == 0) << 2)
#define SETW_V (PSW |= ((source ^ dest ^ result ^ (result >> 1)) >> 14) & 0x02)
#define SETW_C (PSW |= (result >> 16) & 0x01)
#define SETW_NZ SETW_N; SETW_Z
#define SETW_NZV SETW_N; SETW_Z; SETW_V
#define SETW_NZVC SETW_N; SETW_Z; SETW_V; SETW_C

/* operations */
/* ADC: dst += C */
#define ADC_R(d)    int dreg, source, dest, result;     source = GET_C; GET_DW_##d; CLR_NZVC; result = dest + source; SETW_NZVC; PUT_DW_DREG(result)
#define ADC_M(d)    int dreg, source, dest, result, ea; source = GET_C; GET_DW_##d; CLR_NZVC; result = dest + source; SETW_NZVC; PUT_DW_EA(result)
#define ADCB_R(d)   int dreg, source, dest, result;     source = GET_C; GET_DB_##d; CLR_NZVC; result = dest + source; SETB_NZVC; PUT_DB_DREG(result)
#define ADCB_M(d)   int dreg, source, dest, result, ea; source = GET_C; GET_DB_##d; CLR_NZVC; result = dest + source; SETB_NZVC; PUT_DB_EA(result)
/* ADD: dst += src */
#define ADD_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest + source; SETW_NZVC; PUT_DW_DREG(result)
#define ADD_X(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest + source; SETW_NZVC; PUT_DW_DREG(result)
#define ADD_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest + source; SETW_NZVC; PUT_DW_EA(result)
/* ASL: dst = (dst << 1); C = (dst >> 7) */
#define ASL_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = dest << 1; SETW_NZ; PSW |= (dest >> 15) & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_DREG(result)
#define ASL_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = dest << 1; SETW_NZ; PSW |= (dest >> 15) & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_EA(result)
#define ASLB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZVC; result = dest << 1; SETB_NZ; PSW |= (dest >> 7) & 1;  PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_DREG(result)
#define ASLB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZVC; result = dest << 1; SETB_NZ; PSW |= (dest >> 7) & 1;  PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_EA(result)
/* ASR: dst = (dst << 1); C = (dst >> 7) */
#define ASR_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = (dest >> 1) | (dest & 0x8000); SETW_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_DREG(result)
#define ASR_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = (dest >> 1) | (dest & 0x8000); SETW_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_EA(result)
#define ASRB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZVC; result = (dest >> 1) | (dest & 0x80);   SETB_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_DREG(result)
#define ASRB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZVC; result = (dest >> 1) | (dest & 0x80);   SETB_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_EA(result)
/* BIC: dst &= ~src */
#define BIC_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest & ~source; SETW_NZ; PUT_DW_DREG(result)
#define BIC_X(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest & ~source; SETW_NZ; PUT_DW_DREG(result)
#define BIC_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest & ~source; SETW_NZ; PUT_DW_EA(result)
#define BICB_R(s,d) int sreg, dreg, source, dest, result;     GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest & ~source; SETB_NZ; PUT_DB_DREG(result)
#define BICB_X(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest & ~source; SETB_NZ; PUT_DB_DREG(result)
#define BICB_M(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest & ~source; SETB_NZ; PUT_DB_EA(result)
/* BIS: dst |= src */
#define BIS_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest | source; SETW_NZ; PUT_DW_DREG(result)
#define BIS_X(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest | source; SETW_NZ; PUT_DW_DREG(result)
#define BIS_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest | source; SETW_NZ; PUT_DW_EA(result)
#define BISB_R(s,d) int sreg, dreg, source, dest, result;     GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest | source; SETB_NZ; PUT_DB_DREG(result)
#define BISB_X(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest | source; SETB_NZ; PUT_DB_DREG(result)
#define BISB_M(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest | source; SETB_NZ; PUT_DB_EA(result)
/* BIT: flags = dst & src */
#define BIT_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest & source; SETW_NZ;
#define BIT_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZV; result = dest & source; SETW_NZ;
#define BITB_R(s,d) int sreg, dreg, source, dest, result;     GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest & source; SETB_NZ;
#define BITB_M(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZV; result = dest & source; SETB_NZ;
/* BR: if (condition) branch */
#define BR(c)       if (c) { PC += 2 * (signed char)(op & 0xff); }
/* CLR: dst = 0 */
#define CLR_R(d)    int dreg;     PUT_DW_##d(0); CLR_NZVC; SET_Z
#define CLR_M(d)    int dreg, ea; PUT_DWT_##d(0); CLR_NZVC; SET_Z
#define CLRB_R(d)   int dreg;     PUT_DB_##d(0); CLR_NZVC; SET_Z
#define CLRB_M(d)   int dreg, ea; PUT_DBT_##d(0); CLR_NZVC; SET_Z
/* CMP: flags = src - dst */
#define CMP_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZVC; result = source - dest; SETW_NZVC;
#define CMP_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZVC; result = source - dest; SETW_NZVC;
#define CMPB_R(s,d) int sreg, dreg, source, dest, result;     GET_SB_##s; GET_DB_##d; CLR_NZVC; result = source - dest; SETB_NZVC;
#define CMPB_M(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZVC; result = source - dest; SETB_NZVC;
/* COM: dst = ~dst */
#define COM_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = ~dest; SETW_NZ; SET_C; PUT_DW_DREG(result)
#define COM_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = ~dest; SETW_NZ; SET_C; PUT_DW_EA(result)
#define COMB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZVC; result = ~dest; SETB_NZ; SET_C; PUT_DB_DREG(result)
#define COMB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZVC; result = ~dest; SETB_NZ; SET_C; PUT_DB_EA(result)
/* DEC: dst -= 1 */
#define DEC_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZV; result = dest - 1; SETW_NZ; if (dest == 0x8000) SET_V; PUT_DW_DREG(result)
#define DEC_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZV; result = dest - 1; SETW_NZ; if (dest == 0x8000) SET_V; PUT_DW_EA(result)
#define DECB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZV; result = dest - 1; SETB_NZ; if (dest == 0x80)   SET_V; PUT_DB_DREG(result)
#define DECB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZV; result = dest - 1; SETB_NZ; if (dest == 0x80)   SET_V; PUT_DB_EA(result)
/* INC: dst += 1 */
#define INC_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZV; result = dest + 1; SETW_NZ; if (dest == 0x7fff) SET_V; PUT_DW_DREG(result)
#define INC_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZV; result = dest + 1; SETW_NZ; if (dest == 0x7fff) SET_V; PUT_DW_EA(result)
#define INCB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZV; result = dest + 1; SETB_NZ; if (dest == 0x7f)   SET_V; PUT_DB_DREG(result)
#define INCB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZV; result = dest + 1; SETB_NZ; if (dest == 0x7f)   SET_V; PUT_DB_EA(result)
/* JMP: PC = ea */
#define JMP(d)      int dreg, ea; GET_DREG; MAKE_EAW_##d(dreg); PC = ea
/* JSR: PUSH src, src = PC, PC = ea */
#define JSR(d)      int sreg, dreg, ea; GET_SREG; GET_DREG; MAKE_EAW_##d(dreg); PUSH(REGW(sreg)); REGW(sreg) = PC; PC = ea
/* MFPS: dst = flags */
#define MFPS_R(d)   int dreg, result;     result = PSW; CLR_NZV; SETB_NZ; PUT_DW_##d((signed char)result)
#define MFPS_M(d)   int dreg, result, ea; result = PSW; CLR_NZV; SETB_NZ; PUT_DB_##d(result)
/* MOV: dst = src */
#define MOV_R(s,d)  int sreg, dreg, source, result;     GET_SW_##s; CLR_NZV; result = source; SETW_NZ; PUT_DW_##d(result)
#define MOV_M(s,d)  int sreg, dreg, source, result, ea; GET_SW_##s; CLR_NZV; result = source; SETW_NZ; PUT_DWT_##d(result)
#define MOVB_R(s,d) int sreg, dreg, source, result;     GET_SB_##s; CLR_NZV; result = source; SETB_NZ; PUT_DW_##d((signed char)result)
#define MOVB_X(s,d) int sreg, dreg, source, result, ea; GET_SB_##s; CLR_NZV; result = source; SETB_NZ; PUT_DW_##d((signed char)result)
#define MOVB_M(s,d) int sreg, dreg, source, result, ea; GET_SB_##s; CLR_NZV; result = source; SETB_NZ; PUT_DBT_##d(result)
/* MTPS: flags = src */
#define MTPS_R(d)   int dreg, dest;     GET_DW_##d; PSW = (PSW & ~0xef) | (dest & 0xef); t11_check_irqs()
#define MTPS_M(d)   int dreg, dest, ea; GET_DW_##d; PSW = (PSW & ~0xef) | (dest & 0xef); t11_check_irqs()
/* NEG: dst = -dst */
#define NEG_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = -dest; SETW_NZ; if (dest == 0x8000) SET_V; if (result) SET_C; PUT_DW_DREG(result)
#define NEG_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = -dest; SETW_NZ; if (dest == 0x8000) SET_V; if (result) SET_C; PUT_DW_EA(result)
#define NEGB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZVC; result = -dest; SETB_NZ; if (dest == 0x80)   SET_V; if (result) SET_C; PUT_DB_DREG(result)
#define NEGB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZVC; result = -dest; SETB_NZ; if (dest == 0x80)   SET_V; if (result) SET_C; PUT_DB_EA(result)
/* ROL: dst = (dst << 1) | C; C = (dst >> 7) */
#define ROL_R(d)    int dreg, dest, result;     GET_DW_##d; result = (dest << 1) | GET_C; CLR_NZVC; SETW_NZ; PSW |= (dest >> 15) & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_DREG(result)
#define ROL_M(d)    int dreg, dest, result, ea; GET_DW_##d; result = (dest << 1) | GET_C; CLR_NZVC; SETW_NZ; PSW |= (dest >> 15) & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_EA(result)
#define ROLB_R(d)   int dreg, dest, result;     GET_DB_##d; result = (dest << 1) | GET_C; CLR_NZVC; SETB_NZ; PSW |= (dest >> 7) & 1;  PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_DREG(result)
#define ROLB_M(d)   int dreg, dest, result, ea; GET_DB_##d; result = (dest << 1) | GET_C; CLR_NZVC; SETB_NZ; PSW |= (dest >> 7) & 1;  PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_EA(result)
/* ROR: dst = (dst >> 1) | (C << 7); C = dst & 1 */
#define ROR_R(d)    int dreg, dest, result;     GET_DW_##d; result = (dest >> 1) | (GET_C << 15); CLR_NZVC; SETW_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_DREG(result)
#define ROR_M(d)    int dreg, dest, result, ea; GET_DW_##d; result = (dest >> 1) | (GET_C << 15); CLR_NZVC; SETW_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DW_EA(result)
#define RORB_R(d)   int dreg, dest, result;     GET_DB_##d; result = (dest >> 1) | (GET_C << 7);  CLR_NZVC; SETB_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_DREG(result)
#define RORB_M(d)   int dreg, dest, result, ea; GET_DB_##d; result = (dest >> 1) | (GET_C << 7);  CLR_NZVC; SETB_NZ; PSW |= dest & 1; PSW |= ((PSW << 1) ^ (PSW >> 2)) & 2; PUT_DB_EA(result)
/* SBC: dst -= C */
#define SBC_R(d)    int dreg, source, dest, result;     source = GET_C; GET_DW_##d; CLR_NZVC; result = dest - source; SETW_NZVC; PUT_DW_DREG(result)
#define SBC_M(d)    int dreg, source, dest, result, ea; source = GET_C; GET_DW_##d; CLR_NZVC; result = dest - source; SETW_NZVC; PUT_DW_EA(result)
#define SBCB_R(d)   int dreg, source, dest, result;     source = GET_C; GET_DB_##d; CLR_NZVC; result = dest - source; SETB_NZVC; PUT_DB_DREG(result)
#define SBCB_M(d)   int dreg, source, dest, result, ea; source = GET_C; GET_DB_##d; CLR_NZVC; result = dest - source; SETB_NZVC; PUT_DB_EA(result)
/* SUB: dst -= src */
#define SUB_R(s,d)  int sreg, dreg, source, dest, result;     GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest - source; SETW_NZVC; PUT_DW_DREG(result)
#define SUB_X(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest - source; SETW_NZVC; PUT_DW_DREG(result)
#define SUB_M(s,d)  int sreg, dreg, source, dest, result, ea; GET_SW_##s; GET_DW_##d; CLR_NZVC; result = dest - source; SETW_NZVC; PUT_DW_EA(result)
#define SUBB_R(s,d) int sreg, dreg, source, dest, result;     GET_SB_##s; GET_DB_##d; CLR_NZVC; result = dest - source; SETB_NZVC; PUT_DB_DREG(result)
#define SUBB_X(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZVC; result = dest - source; SETB_NZVC; PUT_DB_DREG(result)
#define SUBB_M(s,d) int sreg, dreg, source, dest, result, ea; GET_SB_##s; GET_DB_##d; CLR_NZVC; result = dest - source; SETB_NZVC; PUT_DB_EA(result)
/* SWAB: dst = (dst >> 8) + (dst << 8) */
#define SWAB_R(d)   int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = ((dest >> 8) & 0xff) + (dest << 8); SETB_NZ; PUT_DW_DREG(result)
#define SWAB_M(d)   int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = ((dest >> 8) & 0xff) + (dest << 8); SETB_NZ; PUT_DW_EA(result)
/* SXT: dst = sign-extend dst */
#define SXT_R(d)    int dreg, result;     CLR_ZV; if (GET_N) result = -1; else { result = 0; SET_Z; } PUT_DW_##d(result)
#define SXT_M(d)    int dreg, result, ea; CLR_ZV; if (GET_N) result = -1; else { result = 0; SET_Z; } PUT_DWT_##d(result)
/* TST: dst = ~dst */
#define TST_R(d)    int dreg, dest, result;     GET_DW_##d; CLR_NZVC; result = dest; SETW_NZ;
#define TST_M(d)    int dreg, dest, result, ea; GET_DW_##d; CLR_NZVC; result = dest; SETW_NZ;
#define TSTB_R(d)   int dreg, dest, result;     GET_DB_##d; CLR_NZVC; result = dest; SETB_NZ;
#define TSTB_M(d)   int dreg, dest, result, ea; GET_DB_##d; CLR_NZVC; result = dest; SETB_NZ;
/* XOR: dst ^= src */
#define XOR_R(d)    int sreg, dreg, source, dest, result;     GET_SREG; source = REGW(sreg); GET_DW_##d; CLR_NZV; result = dest ^ source; SETW_NZ; PUT_DW_DREG(result)
#define XOR_M(d)    int sreg, dreg, source, dest, result, ea; GET_SREG; source = REGW(sreg); GET_DW_##d; CLR_NZV; result = dest ^ source; SETW_NZ; PUT_DW_EA(result)


void T11::trap_to(uint16_t vector)
{
	PUSH(PSW);
	PUSH(PC);
	PC = RWORD(vector);
	PSW = RWORD(vector + 2);
	t11_check_irqs();
}

void T11::op_0000(uint16_t op)
{
	switch (op & 0x3f)
	{
		case 0x00:  /* HALT  */ halt(op); break;
		case 0x01:  /* WAIT  */ icount_ = 0; wait_state_ = 1; break;
		case 0x02:  /* RTI   */ icount_ -= 24; PC = POP(); PSW = POP(); t11_check_irqs(); break;
		case 0x03:  /* BPT   */ icount_ -= 48; trap_to(0x0c); break;
		case 0x04:  /* IOT   */ icount_ -= 48; trap_to(0x10); break;
		case 0x05:  /* RESET */ if (reset_handler_) reset_handler_(); icount_ -= 110; break;
		case 0x06:  /* RTT   */ icount_ -= 33; PC = POP(); PSW = POP(); t11_check_irqs(); break;
		case 0x07:  /* MFPT  */ REGB(0) = 4; break;

		default:    illegal(op); break;
	}
}

void T11::halt(uint16_t op)
{
	icount_ -= 48;
	PUSH(PSW);
	PUSH(PC);
	PC = initial_pc_ + 4;
	PSW = 0340;
	t11_check_irqs();
}

void T11::illegal(uint16_t op)
{
	icount_ -= 48;
	trap_to(0x08);
}

void T11::illegal4(uint16_t op)
{
	icount_ -= 48;
	trap_to(0x04);
}

void T11::mark(uint16_t op)
{
	icount_ -= 36;

	SP = PC + 2 * (op & 0x3f);
	PC = REGW(5);
	REGW(5) = POP();
}

void T11::jmp_rgd(uint16_t op)       { icount_ -= 15; { JMP(RGD); } }
void T11::jmp_in(uint16_t op)        { icount_ -= 18; { JMP(IN);  } }
void T11::jmp_ind(uint16_t op)       { icount_ -= 18; { JMP(IND); } }
void T11::jmp_de(uint16_t op)        { icount_ -= 18; { JMP(DE);  } }
void T11::jmp_ded(uint16_t op)       { icount_ -= 21; { JMP(DED); } }
void T11::jmp_ix(uint16_t op)        { icount_ -= 21; { JMP(IX);  } }
void T11::jmp_ixd(uint16_t op)       { icount_ -= 27; { JMP(IXD); } }

void T11::rts(uint16_t op)
{
	int dreg;
	icount_ -= 21;
	GET_DREG;
	PC = REGD(dreg);
	REGW(dreg) = POP();
}

void T11::ccc(uint16_t op)         { icount_ -= 18; { PSW &= ~(op & 15); } }
void T11::scc(uint16_t op)         { icount_ -= 18; { PSW |=  (op & 15); } }

void T11::swab_rg(uint16_t op)       { icount_ -= 12; { SWAB_R(RG); } }
void T11::swab_rgd(uint16_t op)      { icount_ -= 21; { SWAB_M(RGD); } }
void T11::swab_in(uint16_t op)       { icount_ -= 21; { SWAB_M(IN); } }
void T11::swab_ind(uint16_t op)      { icount_ -= 27; { SWAB_M(IND); } }
void T11::swab_de(uint16_t op)       { icount_ -= 24; { SWAB_M(DE); } }
void T11::swab_ded(uint16_t op)      { icount_ -= 30; { SWAB_M(DED); } }
void T11::swab_ix(uint16_t op)       { icount_ -= 30; { SWAB_M(IX); } }
void T11::swab_ixd(uint16_t op)      { icount_ -= 36; { SWAB_M(IXD); } }

void T11::br(uint16_t op)            { icount_ -= 12; { BR(1); } }
void T11::bne(uint16_t op)           { icount_ -= 12; { BR(!GET_Z); } }
void T11::beq(uint16_t op)           { icount_ -= 12; { BR( GET_Z); } }
void T11::bge(uint16_t op)           { icount_ -= 12; { BR(!((GET_N >> 2) ^ GET_V)); } }
void T11::blt(uint16_t op)           { icount_ -= 12; { BR(((GET_N >> 2) ^ GET_V)); } }
void T11::bgt(uint16_t op)           { icount_ -= 12; { BR(!GET_Z && !((GET_N >> 2) ^ GET_V)); } }
void T11::ble(uint16_t op)           { icount_ -= 12; { BR( GET_Z || ((GET_N >> 2) ^ GET_V)); } }

void T11::jsr_rgd(uint16_t op)       { icount_ -= 27; { JSR(RGD); } }
void T11::jsr_in(uint16_t op)        { icount_ -= 30; { JSR(IN);  } }
void T11::jsr_ind(uint16_t op)       { icount_ -= 30; { JSR(IND); } }
void T11::jsr_de(uint16_t op)        { icount_ -= 30; { JSR(DE);  } }
void T11::jsr_ded(uint16_t op)       { icount_ -= 33; { JSR(DED); } }
void T11::jsr_ix(uint16_t op)        { icount_ -= 33; { JSR(IX);  } }
void T11::jsr_ixd(uint16_t op)       { icount_ -= 39; { JSR(IXD); } }

void T11::clr_rg(uint16_t op)        { icount_ -= 12; { CLR_R(RG);  } }
void T11::clr_rgd(uint16_t op)       { icount_ -= 21; { CLR_M(RGD); } }
void T11::clr_in(uint16_t op)        { icount_ -= 21; { CLR_M(IN);  } }
void T11::clr_ind(uint16_t op)       { icount_ -= 27; { CLR_M(IND); } }
void T11::clr_de(uint16_t op)        { icount_ -= 24; { CLR_M(DE);  } }
void T11::clr_ded(uint16_t op)       { icount_ -= 30; { CLR_M(DED); } }
void T11::clr_ix(uint16_t op)        { icount_ -= 30; { CLR_M(IX);  } }
void T11::clr_ixd(uint16_t op)       { icount_ -= 36; { CLR_M(IXD); } }

void T11::com_rg(uint16_t op)        { icount_ -= 12; { COM_R(RG);  } }
void T11::com_rgd(uint16_t op)       { icount_ -= 21; { COM_M(RGD); } }
void T11::com_in(uint16_t op)        { icount_ -= 21; { COM_M(IN);  } }
void T11::com_ind(uint16_t op)       { icount_ -= 27; { COM_M(IND); } }
void T11::com_de(uint16_t op)        { icount_ -= 24; { COM_M(DE);  } }
void T11::com_ded(uint16_t op)       { icount_ -= 30; { COM_M(DED); } }
void T11::com_ix(uint16_t op)        { icount_ -= 30; { COM_M(IX);  } }
void T11::com_ixd(uint16_t op)       { icount_ -= 36; { COM_M(IXD); } }

void T11::inc_rg(uint16_t op)        { icount_ -= 12; { INC_R(RG);  } }
void T11::inc_rgd(uint16_t op)       { icount_ -= 21; { INC_M(RGD); } }
void T11::inc_in(uint16_t op)        { icount_ -= 21; { INC_M(IN);  } }
void T11::inc_ind(uint16_t op)       { icount_ -= 27; { INC_M(IND); } }
void T11::inc_de(uint16_t op)        { icount_ -= 24; { INC_M(DE);  } }
void T11::inc_ded(uint16_t op)       { icount_ -= 30; { INC_M(DED); } }
void T11::inc_ix(uint16_t op)        { icount_ -= 30; { INC_M(IX);  } }
void T11::inc_ixd(uint16_t op)       { icount_ -= 36; { INC_M(IXD); } }

void T11::dec_rg(uint16_t op)        { icount_ -= 12; { DEC_R(RG);  } }
void T11::dec_rgd(uint16_t op)       { icount_ -= 21; { DEC_M(RGD); } }
void T11::dec_in(uint16_t op)        { icount_ -= 21; { DEC_M(IN);  } }
void T11::dec_ind(uint16_t op)       { icount_ -= 27; { DEC_M(IND); } }
void T11::dec_de(uint16_t op)        { icount_ -= 24; { DEC_M(DE);  } }
void T11::dec_ded(uint16_t op)       { icount_ -= 30; { DEC_M(DED); } }
void T11::dec_ix(uint16_t op)        { icount_ -= 30; { DEC_M(IX);  } }
void T11::dec_ixd(uint16_t op)       { icount_ -= 36; { DEC_M(IXD); } }

void T11::neg_rg(uint16_t op)        { icount_ -= 12; { NEG_R(RG);  } }
void T11::neg_rgd(uint16_t op)       { icount_ -= 21; { NEG_M(RGD); } }
void T11::neg_in(uint16_t op)        { icount_ -= 21; { NEG_M(IN);  } }
void T11::neg_ind(uint16_t op)       { icount_ -= 27; { NEG_M(IND); } }
void T11::neg_de(uint16_t op)        { icount_ -= 24; { NEG_M(DE);  } }
void T11::neg_ded(uint16_t op)       { icount_ -= 30; { NEG_M(DED); } }
void T11::neg_ix(uint16_t op)        { icount_ -= 30; { NEG_M(IX);  } }
void T11::neg_ixd(uint16_t op)       { icount_ -= 36; { NEG_M(IXD); } }

void T11::adc_rg(uint16_t op)        { icount_ -= 12; { ADC_R(RG);  } }
void T11::adc_rgd(uint16_t op)       { icount_ -= 21; { ADC_M(RGD); } }
void T11::adc_in(uint16_t op)        { icount_ -= 21; { ADC_M(IN);  } }
void T11::adc_ind(uint16_t op)       { icount_ -= 27; { ADC_M(IND); } }
void T11::adc_de(uint16_t op)        { icount_ -= 24; { ADC_M(DE);  } }
void T11::adc_ded(uint16_t op)       { icount_ -= 30; { ADC_M(DED); } }
void T11::adc_ix(uint16_t op)        { icount_ -= 30; { ADC_M(IX);  } }
void T11::adc_ixd(uint16_t op)       { icount_ -= 36; { ADC_M(IXD); } }

void T11::sbc_rg(uint16_t op)        { icount_ -= 12; { SBC_R(RG);  } }
void T11::sbc_rgd(uint16_t op)       { icount_ -= 21; { SBC_M(RGD); } }
void T11::sbc_in(uint16_t op)        { icount_ -= 21; { SBC_M(IN);  } }
void T11::sbc_ind(uint16_t op)       { icount_ -= 27; { SBC_M(IND); } }
void T11::sbc_de(uint16_t op)        { icount_ -= 24; { SBC_M(DE);  } }
void T11::sbc_ded(uint16_t op)       { icount_ -= 30; { SBC_M(DED); } }
void T11::sbc_ix(uint16_t op)        { icount_ -= 30; { SBC_M(IX);  } }
void T11::sbc_ixd(uint16_t op)       { icount_ -= 36; { SBC_M(IXD); } }

void T11::tst_rg(uint16_t op)        { icount_ -= 12; { TST_R(RG);  } }
void T11::tst_rgd(uint16_t op)       { icount_ -= 18; { TST_M(RGD); } }
void T11::tst_in(uint16_t op)        { icount_ -= 18; { TST_M(IN);  } }
void T11::tst_ind(uint16_t op)       { icount_ -= 24; { TST_M(IND); } }
void T11::tst_de(uint16_t op)        { icount_ -= 21; { TST_M(DE);  } }
void T11::tst_ded(uint16_t op)       { icount_ -= 27; { TST_M(DED); } }
void T11::tst_ix(uint16_t op)        { icount_ -= 27; { TST_M(IX);  } }
void T11::tst_ixd(uint16_t op)       { icount_ -= 33; { TST_M(IXD); } }

void T11::ror_rg(uint16_t op)        { icount_ -= 12; { ROR_R(RG);  } }
void T11::ror_rgd(uint16_t op)       { icount_ -= 21; { ROR_M(RGD); } }
void T11::ror_in(uint16_t op)        { icount_ -= 21; { ROR_M(IN);  } }
void T11::ror_ind(uint16_t op)       { icount_ -= 27; { ROR_M(IND); } }
void T11::ror_de(uint16_t op)        { icount_ -= 24; { ROR_M(DE);  } }
void T11::ror_ded(uint16_t op)       { icount_ -= 30; { ROR_M(DED); } }
void T11::ror_ix(uint16_t op)        { icount_ -= 30; { ROR_M(IX);  } }
void T11::ror_ixd(uint16_t op)       { icount_ -= 36; { ROR_M(IXD); } }

void T11::rol_rg(uint16_t op)        { icount_ -= 12; { ROL_R(RG);  } }
void T11::rol_rgd(uint16_t op)       { icount_ -= 21; { ROL_M(RGD); } }
void T11::rol_in(uint16_t op)        { icount_ -= 21; { ROL_M(IN);  } }
void T11::rol_ind(uint16_t op)       { icount_ -= 27; { ROL_M(IND); } }
void T11::rol_de(uint16_t op)        { icount_ -= 24; { ROL_M(DE);  } }
void T11::rol_ded(uint16_t op)       { icount_ -= 30; { ROL_M(DED); } }
void T11::rol_ix(uint16_t op)        { icount_ -= 30; { ROL_M(IX);  } }
void T11::rol_ixd(uint16_t op)       { icount_ -= 36; { ROL_M(IXD); } }

void T11::asr_rg(uint16_t op)        { icount_ -= 12; { ASR_R(RG);  } }
void T11::asr_rgd(uint16_t op)       { icount_ -= 21; { ASR_M(RGD); } }
void T11::asr_in(uint16_t op)        { icount_ -= 21; { ASR_M(IN);  } }
void T11::asr_ind(uint16_t op)       { icount_ -= 27; { ASR_M(IND); } }
void T11::asr_de(uint16_t op)        { icount_ -= 24; { ASR_M(DE);  } }
void T11::asr_ded(uint16_t op)       { icount_ -= 30; { ASR_M(DED); } }
void T11::asr_ix(uint16_t op)        { icount_ -= 30; { ASR_M(IX);  } }
void T11::asr_ixd(uint16_t op)       { icount_ -= 36; { ASR_M(IXD); } }

void T11::asl_rg(uint16_t op)        { icount_ -= 12; { ASL_R(RG);  } }
void T11::asl_rgd(uint16_t op)       { icount_ -= 21; { ASL_M(RGD); } }
void T11::asl_in(uint16_t op)        { icount_ -= 21; { ASL_M(IN);  } }
void T11::asl_ind(uint16_t op)       { icount_ -= 27; { ASL_M(IND); } }
void T11::asl_de(uint16_t op)        { icount_ -= 24; { ASL_M(DE);  } }
void T11::asl_ded(uint16_t op)       { icount_ -= 30; { ASL_M(DED); } }
void T11::asl_ix(uint16_t op)        { icount_ -= 30; { ASL_M(IX);  } }
void T11::asl_ixd(uint16_t op)       { icount_ -= 36; { ASL_M(IXD); } }

void T11::sxt_rg(uint16_t op)        { icount_ -= 12; { SXT_R(RG);  } }
void T11::sxt_rgd(uint16_t op)       { icount_ -= 21; { SXT_M(RGD); } }
void T11::sxt_in(uint16_t op)        { icount_ -= 21; { SXT_M(IN);  } }
void T11::sxt_ind(uint16_t op)       { icount_ -= 27; { SXT_M(IND); } }
void T11::sxt_de(uint16_t op)        { icount_ -= 24; { SXT_M(DE);  } }
void T11::sxt_ded(uint16_t op)       { icount_ -= 30; { SXT_M(DED); } }
void T11::sxt_ix(uint16_t op)        { icount_ -= 30; { SXT_M(IX);  } }
void T11::sxt_ixd(uint16_t op)       { icount_ -= 36; { SXT_M(IXD); } }

void T11::mov_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { MOV_R(RG,RG);   } }
void T11::mov_rg_rgd(uint16_t op)    { icount_ -=  9+12; { MOV_M(RG,RGD);  } }
void T11::mov_rg_in(uint16_t op)     { icount_ -=  9+12; { MOV_M(RG,IN);   } }
void T11::mov_rg_ind(uint16_t op)    { icount_ -=  9+18; { MOV_M(RG,IND);  } }
void T11::mov_rg_de(uint16_t op)     { icount_ -=  9+15; { MOV_M(RG,DE);   } }
void T11::mov_rg_ded(uint16_t op)    { icount_ -=  9+21; { MOV_M(RG,DED);  } }
void T11::mov_rg_ix(uint16_t op)     { icount_ -=  9+21; { MOV_M(RG,IX);   } }
void T11::mov_rg_ixd(uint16_t op)    { icount_ -=  9+27; { MOV_M(RG,IXD);  } }
void T11::mov_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { MOV_M(RGD,RG);  } }
void T11::mov_rgd_rgd(uint16_t op)   { icount_ -= 15+12; { MOV_M(RGD,RGD); } }
void T11::mov_rgd_in(uint16_t op)    { icount_ -= 15+12; { MOV_M(RGD,IN);  } }
void T11::mov_rgd_ind(uint16_t op)   { icount_ -= 15+18; { MOV_M(RGD,IND); } }
void T11::mov_rgd_de(uint16_t op)    { icount_ -= 15+15; { MOV_M(RGD,DE);  } }
void T11::mov_rgd_ded(uint16_t op)   { icount_ -= 15+21; { MOV_M(RGD,DED); } }
void T11::mov_rgd_ix(uint16_t op)    { icount_ -= 15+21; { MOV_M(RGD,IX);  } }
void T11::mov_rgd_ixd(uint16_t op)   { icount_ -= 15+27; { MOV_M(RGD,IXD); } }
void T11::mov_in_rg(uint16_t op)     { icount_ -= 15+ 3; { MOV_M(IN,RG);   } }
void T11::mov_in_rgd(uint16_t op)    { icount_ -= 15+12; { MOV_M(IN,RGD);  } }
void T11::mov_in_in(uint16_t op)     { icount_ -= 15+12; { MOV_M(IN,IN);   } }
void T11::mov_in_ind(uint16_t op)    { icount_ -= 15+18; { MOV_M(IN,IND);  } }
void T11::mov_in_de(uint16_t op)     { icount_ -= 15+15; { MOV_M(IN,DE);   } }
void T11::mov_in_ded(uint16_t op)    { icount_ -= 15+21; { MOV_M(IN,DED);  } }
void T11::mov_in_ix(uint16_t op)     { icount_ -= 15+21; { MOV_M(IN,IX);   } }
void T11::mov_in_ixd(uint16_t op)    { icount_ -= 15+27; { MOV_M(IN,IXD);  } }
void T11::mov_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { MOV_M(IND,RG);  } }
void T11::mov_ind_rgd(uint16_t op)   { icount_ -= 21+12; { MOV_M(IND,RGD); } }
void T11::mov_ind_in(uint16_t op)    { icount_ -= 21+12; { MOV_M(IND,IN);  } }
void T11::mov_ind_ind(uint16_t op)   { icount_ -= 21+18; { MOV_M(IND,IND); } }
void T11::mov_ind_de(uint16_t op)    { icount_ -= 21+15; { MOV_M(IND,DE);  } }
void T11::mov_ind_ded(uint16_t op)   { icount_ -= 21+21; { MOV_M(IND,DED); } }
void T11::mov_ind_ix(uint16_t op)    { icount_ -= 21+21; { MOV_M(IND,IX);  } }
void T11::mov_ind_ixd(uint16_t op)   { icount_ -= 21+27; { MOV_M(IND,IXD); } }
void T11::mov_de_rg(uint16_t op)     { icount_ -= 18+ 3; { MOV_M(DE,RG);   } }
void T11::mov_de_rgd(uint16_t op)    { icount_ -= 18+12; { MOV_M(DE,RGD);  } }
void T11::mov_de_in(uint16_t op)     { icount_ -= 18+12; { MOV_M(DE,IN);   } }
void T11::mov_de_ind(uint16_t op)    { icount_ -= 18+18; { MOV_M(DE,IND);  } }
void T11::mov_de_de(uint16_t op)     { icount_ -= 18+15; { MOV_M(DE,DE);   } }
void T11::mov_de_ded(uint16_t op)    { icount_ -= 18+21; { MOV_M(DE,DED);  } }
void T11::mov_de_ix(uint16_t op)     { icount_ -= 18+21; { MOV_M(DE,IX);   } }
void T11::mov_de_ixd(uint16_t op)    { icount_ -= 18+27; { MOV_M(DE,IXD);  } }
void T11::mov_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { MOV_M(DED,RG);  } }
void T11::mov_ded_rgd(uint16_t op)   { icount_ -= 24+12; { MOV_M(DED,RGD); } }
void T11::mov_ded_in(uint16_t op)    { icount_ -= 24+12; { MOV_M(DED,IN);  } }
void T11::mov_ded_ind(uint16_t op)   { icount_ -= 24+18; { MOV_M(DED,IND); } }
void T11::mov_ded_de(uint16_t op)    { icount_ -= 24+15; { MOV_M(DED,DE);  } }
void T11::mov_ded_ded(uint16_t op)   { icount_ -= 24+21; { MOV_M(DED,DED); } }
void T11::mov_ded_ix(uint16_t op)    { icount_ -= 24+21; { MOV_M(DED,IX);  } }
void T11::mov_ded_ixd(uint16_t op)   { icount_ -= 24+27; { MOV_M(DED,IXD); } }
void T11::mov_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { MOV_M(IX,RG);   } }
void T11::mov_ix_rgd(uint16_t op)    { icount_ -= 24+12; { MOV_M(IX,RGD);  } }
void T11::mov_ix_in(uint16_t op)     { icount_ -= 24+12; { MOV_M(IX,IN);   } }
void T11::mov_ix_ind(uint16_t op)    { icount_ -= 24+18; { MOV_M(IX,IND);  } }
void T11::mov_ix_de(uint16_t op)     { icount_ -= 24+15; { MOV_M(IX,DE);   } }
void T11::mov_ix_ded(uint16_t op)    { icount_ -= 24+21; { MOV_M(IX,DED);  } }
void T11::mov_ix_ix(uint16_t op)     { icount_ -= 24+21; { MOV_M(IX,IX);   } }
void T11::mov_ix_ixd(uint16_t op)    { icount_ -= 24+27; { MOV_M(IX,IXD);  } }
void T11::mov_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { MOV_M(IXD,RG);  } }
void T11::mov_ixd_rgd(uint16_t op)   { icount_ -= 30+12; { MOV_M(IXD,RGD); } }
void T11::mov_ixd_in(uint16_t op)    { icount_ -= 30+12; { MOV_M(IXD,IN);  } }
void T11::mov_ixd_ind(uint16_t op)   { icount_ -= 30+18; { MOV_M(IXD,IND); } }
void T11::mov_ixd_de(uint16_t op)    { icount_ -= 30+15; { MOV_M(IXD,DE);  } }
void T11::mov_ixd_ded(uint16_t op)   { icount_ -= 30+21; { MOV_M(IXD,DED); } }
void T11::mov_ixd_ix(uint16_t op)    { icount_ -= 30+21; { MOV_M(IXD,IX);  } }
void T11::mov_ixd_ixd(uint16_t op)   { icount_ -= 30+27; { MOV_M(IXD,IXD); } }

void T11::cmp_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { CMP_R(RG,RG);   } }
void T11::cmp_rg_rgd(uint16_t op)    { icount_ -=  9+ 9; { CMP_M(RG,RGD);  } }
void T11::cmp_rg_in(uint16_t op)     { icount_ -=  9+ 9; { CMP_M(RG,IN);   } }
void T11::cmp_rg_ind(uint16_t op)    { icount_ -=  9+15; { CMP_M(RG,IND);  } }
void T11::cmp_rg_de(uint16_t op)     { icount_ -=  9+12; { CMP_M(RG,DE);   } }
void T11::cmp_rg_ded(uint16_t op)    { icount_ -=  9+18; { CMP_M(RG,DED);  } }
void T11::cmp_rg_ix(uint16_t op)     { icount_ -=  9+18; { CMP_M(RG,IX);   } }
void T11::cmp_rg_ixd(uint16_t op)    { icount_ -=  9+24; { CMP_M(RG,IXD);  } }
void T11::cmp_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { CMP_M(RGD,RG);  } }
void T11::cmp_rgd_rgd(uint16_t op)   { icount_ -= 15+ 9; { CMP_M(RGD,RGD); } }
void T11::cmp_rgd_in(uint16_t op)    { icount_ -= 15+ 9; { CMP_M(RGD,IN);  } }
void T11::cmp_rgd_ind(uint16_t op)   { icount_ -= 15+15; { CMP_M(RGD,IND); } }
void T11::cmp_rgd_de(uint16_t op)    { icount_ -= 15+12; { CMP_M(RGD,DE);  } }
void T11::cmp_rgd_ded(uint16_t op)   { icount_ -= 15+18; { CMP_M(RGD,DED); } }
void T11::cmp_rgd_ix(uint16_t op)    { icount_ -= 15+18; { CMP_M(RGD,IX);  } }
void T11::cmp_rgd_ixd(uint16_t op)   { icount_ -= 15+24; { CMP_M(RGD,IXD); } }
void T11::cmp_in_rg(uint16_t op)     { icount_ -= 15+ 3; { CMP_M(IN,RG);   } }
void T11::cmp_in_rgd(uint16_t op)    { icount_ -= 15+ 9; { CMP_M(IN,RGD);  } }
void T11::cmp_in_in(uint16_t op)     { icount_ -= 15+ 9; { CMP_M(IN,IN);   } }
void T11::cmp_in_ind(uint16_t op)    { icount_ -= 15+15; { CMP_M(IN,IND);  } }
void T11::cmp_in_de(uint16_t op)     { icount_ -= 15+12; { CMP_M(IN,DE);   } }
void T11::cmp_in_ded(uint16_t op)    { icount_ -= 15+18; { CMP_M(IN,DED);  } }
void T11::cmp_in_ix(uint16_t op)     { icount_ -= 15+18; { CMP_M(IN,IX);   } }
void T11::cmp_in_ixd(uint16_t op)    { icount_ -= 15+24; { CMP_M(IN,IXD);  } }
void T11::cmp_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { CMP_M(IND,RG);  } }
void T11::cmp_ind_rgd(uint16_t op)   { icount_ -= 21+ 9; { CMP_M(IND,RGD); } }
void T11::cmp_ind_in(uint16_t op)    { icount_ -= 21+ 9; { CMP_M(IND,IN);  } }
void T11::cmp_ind_ind(uint16_t op)   { icount_ -= 21+15; { CMP_M(IND,IND); } }
void T11::cmp_ind_de(uint16_t op)    { icount_ -= 21+12; { CMP_M(IND,DE);  } }
void T11::cmp_ind_ded(uint16_t op)   { icount_ -= 21+18; { CMP_M(IND,DED); } }
void T11::cmp_ind_ix(uint16_t op)    { icount_ -= 21+18; { CMP_M(IND,IX);  } }
void T11::cmp_ind_ixd(uint16_t op)   { icount_ -= 21+24; { CMP_M(IND,IXD); } }
void T11::cmp_de_rg(uint16_t op)     { icount_ -= 18+ 3; { CMP_M(DE,RG);   } }
void T11::cmp_de_rgd(uint16_t op)    { icount_ -= 18+ 9; { CMP_M(DE,RGD);  } }
void T11::cmp_de_in(uint16_t op)     { icount_ -= 18+ 9; { CMP_M(DE,IN);   } }
void T11::cmp_de_ind(uint16_t op)    { icount_ -= 18+15; { CMP_M(DE,IND);  } }
void T11::cmp_de_de(uint16_t op)     { icount_ -= 18+12; { CMP_M(DE,DE);   } }
void T11::cmp_de_ded(uint16_t op)    { icount_ -= 18+18; { CMP_M(DE,DED);  } }
void T11::cmp_de_ix(uint16_t op)     { icount_ -= 18+18; { CMP_M(DE,IX);   } }
void T11::cmp_de_ixd(uint16_t op)    { icount_ -= 18+24; { CMP_M(DE,IXD);  } }
void T11::cmp_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { CMP_M(DED,RG);  } }
void T11::cmp_ded_rgd(uint16_t op)   { icount_ -= 24+ 9; { CMP_M(DED,RGD); } }
void T11::cmp_ded_in(uint16_t op)    { icount_ -= 24+ 9; { CMP_M(DED,IN);  } }
void T11::cmp_ded_ind(uint16_t op)   { icount_ -= 24+15; { CMP_M(DED,IND); } }
void T11::cmp_ded_de(uint16_t op)    { icount_ -= 24+12; { CMP_M(DED,DE);  } }
void T11::cmp_ded_ded(uint16_t op)   { icount_ -= 24+18; { CMP_M(DED,DED); } }
void T11::cmp_ded_ix(uint16_t op)    { icount_ -= 24+18; { CMP_M(DED,IX);  } }
void T11::cmp_ded_ixd(uint16_t op)   { icount_ -= 24+24; { CMP_M(DED,IXD); } }
void T11::cmp_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { CMP_M(IX,RG);   } }
void T11::cmp_ix_rgd(uint16_t op)    { icount_ -= 24+ 9; { CMP_M(IX,RGD);  } }
void T11::cmp_ix_in(uint16_t op)     { icount_ -= 24+ 9; { CMP_M(IX,IN);   } }
void T11::cmp_ix_ind(uint16_t op)    { icount_ -= 24+15; { CMP_M(IX,IND);  } }
void T11::cmp_ix_de(uint16_t op)     { icount_ -= 24+12; { CMP_M(IX,DE);   } }
void T11::cmp_ix_ded(uint16_t op)    { icount_ -= 24+18; { CMP_M(IX,DED);  } }
void T11::cmp_ix_ix(uint16_t op)     { icount_ -= 24+18; { CMP_M(IX,IX);   } }
void T11::cmp_ix_ixd(uint16_t op)    { icount_ -= 24+24; { CMP_M(IX,IXD);  } }
void T11::cmp_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { CMP_M(IXD,RG);  } }
void T11::cmp_ixd_rgd(uint16_t op)   { icount_ -= 30+ 9; { CMP_M(IXD,RGD); } }
void T11::cmp_ixd_in(uint16_t op)    { icount_ -= 30+ 9; { CMP_M(IXD,IN);  } }
void T11::cmp_ixd_ind(uint16_t op)   { icount_ -= 30+15; { CMP_M(IXD,IND); } }
void T11::cmp_ixd_de(uint16_t op)    { icount_ -= 30+12; { CMP_M(IXD,DE);  } }
void T11::cmp_ixd_ded(uint16_t op)   { icount_ -= 30+18; { CMP_M(IXD,DED); } }
void T11::cmp_ixd_ix(uint16_t op)    { icount_ -= 30+18; { CMP_M(IXD,IX);  } }
void T11::cmp_ixd_ixd(uint16_t op)   { icount_ -= 30+24; { CMP_M(IXD,IXD); } }

void T11::bit_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { BIT_R(RG,RG);   } }
void T11::bit_rg_rgd(uint16_t op)    { icount_ -=  9+ 9; { BIT_M(RG,RGD);  } }
void T11::bit_rg_in(uint16_t op)     { icount_ -=  9+ 9; { BIT_M(RG,IN);   } }
void T11::bit_rg_ind(uint16_t op)    { icount_ -=  9+15; { BIT_M(RG,IND);  } }
void T11::bit_rg_de(uint16_t op)     { icount_ -=  9+12; { BIT_M(RG,DE);   } }
void T11::bit_rg_ded(uint16_t op)    { icount_ -=  9+18; { BIT_M(RG,DED);  } }
void T11::bit_rg_ix(uint16_t op)     { icount_ -=  9+18; { BIT_M(RG,IX);   } }
void T11::bit_rg_ixd(uint16_t op)    { icount_ -=  9+24; { BIT_M(RG,IXD);  } }
void T11::bit_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { BIT_M(RGD,RG);  } }
void T11::bit_rgd_rgd(uint16_t op)   { icount_ -= 15+ 9; { BIT_M(RGD,RGD); } }
void T11::bit_rgd_in(uint16_t op)    { icount_ -= 15+ 9; { BIT_M(RGD,IN);  } }
void T11::bit_rgd_ind(uint16_t op)   { icount_ -= 15+15; { BIT_M(RGD,IND); } }
void T11::bit_rgd_de(uint16_t op)    { icount_ -= 15+12; { BIT_M(RGD,DE);  } }
void T11::bit_rgd_ded(uint16_t op)   { icount_ -= 15+18; { BIT_M(RGD,DED); } }
void T11::bit_rgd_ix(uint16_t op)    { icount_ -= 15+18; { BIT_M(RGD,IX);  } }
void T11::bit_rgd_ixd(uint16_t op)   { icount_ -= 15+24; { BIT_M(RGD,IXD); } }
void T11::bit_in_rg(uint16_t op)     { icount_ -= 15+ 3; { BIT_M(IN,RG);   } }
void T11::bit_in_rgd(uint16_t op)    { icount_ -= 15+ 9; { BIT_M(IN,RGD);  } }
void T11::bit_in_in(uint16_t op)     { icount_ -= 15+ 9; { BIT_M(IN,IN);   } }
void T11::bit_in_ind(uint16_t op)    { icount_ -= 15+15; { BIT_M(IN,IND);  } }
void T11::bit_in_de(uint16_t op)     { icount_ -= 15+12; { BIT_M(IN,DE);   } }
void T11::bit_in_ded(uint16_t op)    { icount_ -= 15+18; { BIT_M(IN,DED);  } }
void T11::bit_in_ix(uint16_t op)     { icount_ -= 15+18; { BIT_M(IN,IX);   } }
void T11::bit_in_ixd(uint16_t op)    { icount_ -= 15+24; { BIT_M(IN,IXD);  } }
void T11::bit_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { BIT_M(IND,RG);  } }
void T11::bit_ind_rgd(uint16_t op)   { icount_ -= 21+ 9; { BIT_M(IND,RGD); } }
void T11::bit_ind_in(uint16_t op)    { icount_ -= 21+ 9; { BIT_M(IND,IN);  } }
void T11::bit_ind_ind(uint16_t op)   { icount_ -= 21+15; { BIT_M(IND,IND); } }
void T11::bit_ind_de(uint16_t op)    { icount_ -= 21+12; { BIT_M(IND,DE);  } }
void T11::bit_ind_ded(uint16_t op)   { icount_ -= 21+18; { BIT_M(IND,DED); } }
void T11::bit_ind_ix(uint16_t op)    { icount_ -= 21+18; { BIT_M(IND,IX);  } }
void T11::bit_ind_ixd(uint16_t op)   { icount_ -= 21+24; { BIT_M(IND,IXD); } }
void T11::bit_de_rg(uint16_t op)     { icount_ -= 18+ 3; { BIT_M(DE,RG);   } }
void T11::bit_de_rgd(uint16_t op)    { icount_ -= 18+ 9; { BIT_M(DE,RGD);  } }
void T11::bit_de_in(uint16_t op)     { icount_ -= 18+ 9; { BIT_M(DE,IN);   } }
void T11::bit_de_ind(uint16_t op)    { icount_ -= 18+15; { BIT_M(DE,IND);  } }
void T11::bit_de_de(uint16_t op)     { icount_ -= 18+12; { BIT_M(DE,DE);   } }
void T11::bit_de_ded(uint16_t op)    { icount_ -= 18+18; { BIT_M(DE,DED);  } }
void T11::bit_de_ix(uint16_t op)     { icount_ -= 18+18; { BIT_M(DE,IX);   } }
void T11::bit_de_ixd(uint16_t op)    { icount_ -= 18+24; { BIT_M(DE,IXD);  } }
void T11::bit_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { BIT_M(DED,RG);  } }
void T11::bit_ded_rgd(uint16_t op)   { icount_ -= 24+ 9; { BIT_M(DED,RGD); } }
void T11::bit_ded_in(uint16_t op)    { icount_ -= 24+ 9; { BIT_M(DED,IN);  } }
void T11::bit_ded_ind(uint16_t op)   { icount_ -= 24+15; { BIT_M(DED,IND); } }
void T11::bit_ded_de(uint16_t op)    { icount_ -= 24+12; { BIT_M(DED,DE);  } }
void T11::bit_ded_ded(uint16_t op)   { icount_ -= 24+18; { BIT_M(DED,DED); } }
void T11::bit_ded_ix(uint16_t op)    { icount_ -= 24+18; { BIT_M(DED,IX);  } }
void T11::bit_ded_ixd(uint16_t op)   { icount_ -= 24+24; { BIT_M(DED,IXD); } }
void T11::bit_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { BIT_M(IX,RG);   } }
void T11::bit_ix_rgd(uint16_t op)    { icount_ -= 24+ 9; { BIT_M(IX,RGD);  } }
void T11::bit_ix_in(uint16_t op)     { icount_ -= 24+ 9; { BIT_M(IX,IN);   } }
void T11::bit_ix_ind(uint16_t op)    { icount_ -= 24+15; { BIT_M(IX,IND);  } }
void T11::bit_ix_de(uint16_t op)     { icount_ -= 24+12; { BIT_M(IX,DE);   } }
void T11::bit_ix_ded(uint16_t op)    { icount_ -= 24+18; { BIT_M(IX,DED);  } }
void T11::bit_ix_ix(uint16_t op)     { icount_ -= 24+18; { BIT_M(IX,IX);   } }
void T11::bit_ix_ixd(uint16_t op)    { icount_ -= 24+24; { BIT_M(IX,IXD);  } }
void T11::bit_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { BIT_M(IXD,RG);  } }
void T11::bit_ixd_rgd(uint16_t op)   { icount_ -= 30+ 9; { BIT_M(IXD,RGD); } }
void T11::bit_ixd_in(uint16_t op)    { icount_ -= 30+ 9; { BIT_M(IXD,IN);  } }
void T11::bit_ixd_ind(uint16_t op)   { icount_ -= 30+15; { BIT_M(IXD,IND); } }
void T11::bit_ixd_de(uint16_t op)    { icount_ -= 30+12; { BIT_M(IXD,DE);  } }
void T11::bit_ixd_ded(uint16_t op)   { icount_ -= 30+18; { BIT_M(IXD,DED); } }
void T11::bit_ixd_ix(uint16_t op)    { icount_ -= 30+18; { BIT_M(IXD,IX);  } }
void T11::bit_ixd_ixd(uint16_t op)   { icount_ -= 30+24; { BIT_M(IXD,IXD); } }

void T11::bic_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { BIC_R(RG,RG);   } }
void T11::bic_rg_rgd(uint16_t op)    { icount_ -=  9+12; { BIC_M(RG,RGD);  } }
void T11::bic_rg_in(uint16_t op)     { icount_ -=  9+12; { BIC_M(RG,IN);   } }
void T11::bic_rg_ind(uint16_t op)    { icount_ -=  9+18; { BIC_M(RG,IND);  } }
void T11::bic_rg_de(uint16_t op)     { icount_ -=  9+15; { BIC_M(RG,DE);   } }
void T11::bic_rg_ded(uint16_t op)    { icount_ -=  9+21; { BIC_M(RG,DED);  } }
void T11::bic_rg_ix(uint16_t op)     { icount_ -=  9+21; { BIC_M(RG,IX);   } }
void T11::bic_rg_ixd(uint16_t op)    { icount_ -=  9+27; { BIC_M(RG,IXD);  } }
void T11::bic_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { BIC_X(RGD,RG);  } }
void T11::bic_rgd_rgd(uint16_t op)   { icount_ -= 15+12; { BIC_M(RGD,RGD); } }
void T11::bic_rgd_in(uint16_t op)    { icount_ -= 15+12; { BIC_M(RGD,IN);  } }
void T11::bic_rgd_ind(uint16_t op)   { icount_ -= 15+18; { BIC_M(RGD,IND); } }
void T11::bic_rgd_de(uint16_t op)    { icount_ -= 15+15; { BIC_M(RGD,DE);  } }
void T11::bic_rgd_ded(uint16_t op)   { icount_ -= 15+21; { BIC_M(RGD,DED); } }
void T11::bic_rgd_ix(uint16_t op)    { icount_ -= 15+21; { BIC_M(RGD,IX);  } }
void T11::bic_rgd_ixd(uint16_t op)   { icount_ -= 15+27; { BIC_M(RGD,IXD); } }
void T11::bic_in_rg(uint16_t op)     { icount_ -= 15+ 3; { BIC_X(IN,RG);   } }
void T11::bic_in_rgd(uint16_t op)    { icount_ -= 15+12; { BIC_M(IN,RGD);  } }
void T11::bic_in_in(uint16_t op)     { icount_ -= 15+12; { BIC_M(IN,IN);   } }
void T11::bic_in_ind(uint16_t op)    { icount_ -= 15+18; { BIC_M(IN,IND);  } }
void T11::bic_in_de(uint16_t op)     { icount_ -= 15+15; { BIC_M(IN,DE);   } }
void T11::bic_in_ded(uint16_t op)    { icount_ -= 15+21; { BIC_M(IN,DED);  } }
void T11::bic_in_ix(uint16_t op)     { icount_ -= 15+21; { BIC_M(IN,IX);   } }
void T11::bic_in_ixd(uint16_t op)    { icount_ -= 15+27; { BIC_M(IN,IXD);  } }
void T11::bic_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { BIC_X(IND,RG);  } }
void T11::bic_ind_rgd(uint16_t op)   { icount_ -= 21+12; { BIC_M(IND,RGD); } }
void T11::bic_ind_in(uint16_t op)    { icount_ -= 21+12; { BIC_M(IND,IN);  } }
void T11::bic_ind_ind(uint16_t op)   { icount_ -= 21+18; { BIC_M(IND,IND); } }
void T11::bic_ind_de(uint16_t op)    { icount_ -= 21+15; { BIC_M(IND,DE);  } }
void T11::bic_ind_ded(uint16_t op)   { icount_ -= 21+21; { BIC_M(IND,DED); } }
void T11::bic_ind_ix(uint16_t op)    { icount_ -= 21+21; { BIC_M(IND,IX);  } }
void T11::bic_ind_ixd(uint16_t op)   { icount_ -= 21+27; { BIC_M(IND,IXD); } }
void T11::bic_de_rg(uint16_t op)     { icount_ -= 18+ 3; { BIC_X(DE,RG);   } }
void T11::bic_de_rgd(uint16_t op)    { icount_ -= 18+12; { BIC_M(DE,RGD);  } }
void T11::bic_de_in(uint16_t op)     { icount_ -= 18+12; { BIC_M(DE,IN);   } }
void T11::bic_de_ind(uint16_t op)    { icount_ -= 18+18; { BIC_M(DE,IND);  } }
void T11::bic_de_de(uint16_t op)     { icount_ -= 18+15; { BIC_M(DE,DE);   } }
void T11::bic_de_ded(uint16_t op)    { icount_ -= 18+21; { BIC_M(DE,DED);  } }
void T11::bic_de_ix(uint16_t op)     { icount_ -= 18+21; { BIC_M(DE,IX);   } }
void T11::bic_de_ixd(uint16_t op)    { icount_ -= 18+27; { BIC_M(DE,IXD);  } }
void T11::bic_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { BIC_X(DED,RG);  } }
void T11::bic_ded_rgd(uint16_t op)   { icount_ -= 24+12; { BIC_M(DED,RGD); } }
void T11::bic_ded_in(uint16_t op)    { icount_ -= 24+12; { BIC_M(DED,IN);  } }
void T11::bic_ded_ind(uint16_t op)   { icount_ -= 24+18; { BIC_M(DED,IND); } }
void T11::bic_ded_de(uint16_t op)    { icount_ -= 24+15; { BIC_M(DED,DE);  } }
void T11::bic_ded_ded(uint16_t op)   { icount_ -= 24+21; { BIC_M(DED,DED); } }
void T11::bic_ded_ix(uint16_t op)    { icount_ -= 24+21; { BIC_M(DED,IX);  } }
void T11::bic_ded_ixd(uint16_t op)   { icount_ -= 24+27; { BIC_M(DED,IXD); } }
void T11::bic_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { BIC_X(IX,RG);   } }
void T11::bic_ix_rgd(uint16_t op)    { icount_ -= 24+12; { BIC_M(IX,RGD);  } }
void T11::bic_ix_in(uint16_t op)     { icount_ -= 24+12; { BIC_M(IX,IN);   } }
void T11::bic_ix_ind(uint16_t op)    { icount_ -= 24+18; { BIC_M(IX,IND);  } }
void T11::bic_ix_de(uint16_t op)     { icount_ -= 24+15; { BIC_M(IX,DE);   } }
void T11::bic_ix_ded(uint16_t op)    { icount_ -= 24+21; { BIC_M(IX,DED);  } }
void T11::bic_ix_ix(uint16_t op)     { icount_ -= 24+21; { BIC_M(IX,IX);   } }
void T11::bic_ix_ixd(uint16_t op)    { icount_ -= 24+27; { BIC_M(IX,IXD);  } }
void T11::bic_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { BIC_X(IXD,RG);  } }
void T11::bic_ixd_rgd(uint16_t op)   { icount_ -= 30+12; { BIC_M(IXD,RGD); } }
void T11::bic_ixd_in(uint16_t op)    { icount_ -= 30+12; { BIC_M(IXD,IN);  } }
void T11::bic_ixd_ind(uint16_t op)   { icount_ -= 30+18; { BIC_M(IXD,IND); } }
void T11::bic_ixd_de(uint16_t op)    { icount_ -= 30+15; { BIC_M(IXD,DE);  } }
void T11::bic_ixd_ded(uint16_t op)   { icount_ -= 30+21; { BIC_M(IXD,DED); } }
void T11::bic_ixd_ix(uint16_t op)    { icount_ -= 30+21; { BIC_M(IXD,IX);  } }
void T11::bic_ixd_ixd(uint16_t op)   { icount_ -= 30+27; { BIC_M(IXD,IXD); } }

void T11::bis_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { BIS_R(RG,RG);   } }
void T11::bis_rg_rgd(uint16_t op)    { icount_ -=  9+12; { BIS_M(RG,RGD);  } }
void T11::bis_rg_in(uint16_t op)     { icount_ -=  9+12; { BIS_M(RG,IN);   } }
void T11::bis_rg_ind(uint16_t op)    { icount_ -=  9+18; { BIS_M(RG,IND);  } }
void T11::bis_rg_de(uint16_t op)     { icount_ -=  9+15; { BIS_M(RG,DE);   } }
void T11::bis_rg_ded(uint16_t op)    { icount_ -=  9+21; { BIS_M(RG,DED);  } }
void T11::bis_rg_ix(uint16_t op)     { icount_ -=  9+21; { BIS_M(RG,IX);   } }
void T11::bis_rg_ixd(uint16_t op)    { icount_ -=  9+27; { BIS_M(RG,IXD);  } }
void T11::bis_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { BIS_X(RGD,RG);  } }
void T11::bis_rgd_rgd(uint16_t op)   { icount_ -= 15+12; { BIS_M(RGD,RGD); } }
void T11::bis_rgd_in(uint16_t op)    { icount_ -= 15+12; { BIS_M(RGD,IN);  } }
void T11::bis_rgd_ind(uint16_t op)   { icount_ -= 15+18; { BIS_M(RGD,IND); } }
void T11::bis_rgd_de(uint16_t op)    { icount_ -= 15+15; { BIS_M(RGD,DE);  } }
void T11::bis_rgd_ded(uint16_t op)   { icount_ -= 15+21; { BIS_M(RGD,DED); } }
void T11::bis_rgd_ix(uint16_t op)    { icount_ -= 15+21; { BIS_M(RGD,IX);  } }
void T11::bis_rgd_ixd(uint16_t op)   { icount_ -= 15+27; { BIS_M(RGD,IXD); } }
void T11::bis_in_rg(uint16_t op)     { icount_ -= 15+ 3; { BIS_X(IN,RG);   } }
void T11::bis_in_rgd(uint16_t op)    { icount_ -= 15+12; { BIS_M(IN,RGD);  } }
void T11::bis_in_in(uint16_t op)     { icount_ -= 15+12; { BIS_M(IN,IN);   } }
void T11::bis_in_ind(uint16_t op)    { icount_ -= 15+18; { BIS_M(IN,IND);  } }
void T11::bis_in_de(uint16_t op)     { icount_ -= 15+15; { BIS_M(IN,DE);   } }
void T11::bis_in_ded(uint16_t op)    { icount_ -= 15+21; { BIS_M(IN,DED);  } }
void T11::bis_in_ix(uint16_t op)     { icount_ -= 15+21; { BIS_M(IN,IX);   } }
void T11::bis_in_ixd(uint16_t op)    { icount_ -= 15+27; { BIS_M(IN,IXD);  } }
void T11::bis_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { BIS_X(IND,RG);  } }
void T11::bis_ind_rgd(uint16_t op)   { icount_ -= 21+12; { BIS_M(IND,RGD); } }
void T11::bis_ind_in(uint16_t op)    { icount_ -= 21+12; { BIS_M(IND,IN);  } }
void T11::bis_ind_ind(uint16_t op)   { icount_ -= 21+18; { BIS_M(IND,IND); } }
void T11::bis_ind_de(uint16_t op)    { icount_ -= 21+15; { BIS_M(IND,DE);  } }
void T11::bis_ind_ded(uint16_t op)   { icount_ -= 21+21; { BIS_M(IND,DED); } }
void T11::bis_ind_ix(uint16_t op)    { icount_ -= 21+21; { BIS_M(IND,IX);  } }
void T11::bis_ind_ixd(uint16_t op)   { icount_ -= 21+27; { BIS_M(IND,IXD); } }
void T11::bis_de_rg(uint16_t op)     { icount_ -= 18+ 3; { BIS_X(DE,RG);   } }
void T11::bis_de_rgd(uint16_t op)    { icount_ -= 18+12; { BIS_M(DE,RGD);  } }
void T11::bis_de_in(uint16_t op)     { icount_ -= 18+12; { BIS_M(DE,IN);   } }
void T11::bis_de_ind(uint16_t op)    { icount_ -= 18+18; { BIS_M(DE,IND);  } }
void T11::bis_de_de(uint16_t op)     { icount_ -= 18+15; { BIS_M(DE,DE);   } }
void T11::bis_de_ded(uint16_t op)    { icount_ -= 18+21; { BIS_M(DE,DED);  } }
void T11::bis_de_ix(uint16_t op)     { icount_ -= 18+21; { BIS_M(DE,IX);   } }
void T11::bis_de_ixd(uint16_t op)    { icount_ -= 18+27; { BIS_M(DE,IXD);  } }
void T11::bis_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { BIS_X(DED,RG);  } }
void T11::bis_ded_rgd(uint16_t op)   { icount_ -= 24+12; { BIS_M(DED,RGD); } }
void T11::bis_ded_in(uint16_t op)    { icount_ -= 24+12; { BIS_M(DED,IN);  } }
void T11::bis_ded_ind(uint16_t op)   { icount_ -= 24+18; { BIS_M(DED,IND); } }
void T11::bis_ded_de(uint16_t op)    { icount_ -= 24+15; { BIS_M(DED,DE);  } }
void T11::bis_ded_ded(uint16_t op)   { icount_ -= 24+21; { BIS_M(DED,DED); } }
void T11::bis_ded_ix(uint16_t op)    { icount_ -= 24+21; { BIS_M(DED,IX);  } }
void T11::bis_ded_ixd(uint16_t op)   { icount_ -= 24+27; { BIS_M(DED,IXD); } }
void T11::bis_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { BIS_X(IX,RG);   } }
void T11::bis_ix_rgd(uint16_t op)    { icount_ -= 24+12; { BIS_M(IX,RGD);  } }
void T11::bis_ix_in(uint16_t op)     { icount_ -= 24+12; { BIS_M(IX,IN);   } }
void T11::bis_ix_ind(uint16_t op)    { icount_ -= 24+18; { BIS_M(IX,IND);  } }
void T11::bis_ix_de(uint16_t op)     { icount_ -= 24+15; { BIS_M(IX,DE);   } }
void T11::bis_ix_ded(uint16_t op)    { icount_ -= 24+21; { BIS_M(IX,DED);  } }
void T11::bis_ix_ix(uint16_t op)     { icount_ -= 24+21; { BIS_M(IX,IX);   } }
void T11::bis_ix_ixd(uint16_t op)    { icount_ -= 24+27; { BIS_M(IX,IXD);  } }
void T11::bis_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { BIS_X(IXD,RG);  } }
void T11::bis_ixd_rgd(uint16_t op)   { icount_ -= 30+12; { BIS_M(IXD,RGD); } }
void T11::bis_ixd_in(uint16_t op)    { icount_ -= 30+12; { BIS_M(IXD,IN);  } }
void T11::bis_ixd_ind(uint16_t op)   { icount_ -= 30+18; { BIS_M(IXD,IND); } }
void T11::bis_ixd_de(uint16_t op)    { icount_ -= 30+15; { BIS_M(IXD,DE);  } }
void T11::bis_ixd_ded(uint16_t op)   { icount_ -= 30+21; { BIS_M(IXD,DED); } }
void T11::bis_ixd_ix(uint16_t op)    { icount_ -= 30+21; { BIS_M(IXD,IX);  } }
void T11::bis_ixd_ixd(uint16_t op)   { icount_ -= 30+27; { BIS_M(IXD,IXD); } }

void T11::add_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { ADD_R(RG,RG);   } }
void T11::add_rg_rgd(uint16_t op)    { icount_ -=  9+12; { ADD_M(RG,RGD);  } }
void T11::add_rg_in(uint16_t op)     { icount_ -=  9+12; { ADD_M(RG,IN);   } }
void T11::add_rg_ind(uint16_t op)    { icount_ -=  9+18; { ADD_M(RG,IND);  } }
void T11::add_rg_de(uint16_t op)     { icount_ -=  9+15; { ADD_M(RG,DE);   } }
void T11::add_rg_ded(uint16_t op)    { icount_ -=  9+21; { ADD_M(RG,DED);  } }
void T11::add_rg_ix(uint16_t op)     { icount_ -=  9+21; { ADD_M(RG,IX);   } }
void T11::add_rg_ixd(uint16_t op)    { icount_ -=  9+27; { ADD_M(RG,IXD);  } }
void T11::add_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { ADD_X(RGD,RG);  } }
void T11::add_rgd_rgd(uint16_t op)   { icount_ -= 15+12; { ADD_M(RGD,RGD); } }
void T11::add_rgd_in(uint16_t op)    { icount_ -= 15+12; { ADD_M(RGD,IN);  } }
void T11::add_rgd_ind(uint16_t op)   { icount_ -= 15+18; { ADD_M(RGD,IND); } }
void T11::add_rgd_de(uint16_t op)    { icount_ -= 15+15; { ADD_M(RGD,DE);  } }
void T11::add_rgd_ded(uint16_t op)   { icount_ -= 15+21; { ADD_M(RGD,DED); } }
void T11::add_rgd_ix(uint16_t op)    { icount_ -= 15+21; { ADD_M(RGD,IX);  } }
void T11::add_rgd_ixd(uint16_t op)   { icount_ -= 15+27; { ADD_M(RGD,IXD); } }
void T11::add_in_rg(uint16_t op)     { icount_ -= 15+ 3; { ADD_X(IN,RG);   } }
void T11::add_in_rgd(uint16_t op)    { icount_ -= 15+12; { ADD_M(IN,RGD);  } }
void T11::add_in_in(uint16_t op)     { icount_ -= 15+12; { ADD_M(IN,IN);   } }
void T11::add_in_ind(uint16_t op)    { icount_ -= 15+18; { ADD_M(IN,IND);  } }
void T11::add_in_de(uint16_t op)     { icount_ -= 15+15; { ADD_M(IN,DE);   } }
void T11::add_in_ded(uint16_t op)    { icount_ -= 15+21; { ADD_M(IN,DED);  } }
void T11::add_in_ix(uint16_t op)     { icount_ -= 15+21; { ADD_M(IN,IX);   } }
void T11::add_in_ixd(uint16_t op)    { icount_ -= 15+27; { ADD_M(IN,IXD);  } }
void T11::add_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { ADD_X(IND,RG);  } }
void T11::add_ind_rgd(uint16_t op)   { icount_ -= 21+12; { ADD_M(IND,RGD); } }
void T11::add_ind_in(uint16_t op)    { icount_ -= 21+12; { ADD_M(IND,IN);  } }
void T11::add_ind_ind(uint16_t op)   { icount_ -= 21+18; { ADD_M(IND,IND); } }
void T11::add_ind_de(uint16_t op)    { icount_ -= 21+15; { ADD_M(IND,DE);  } }
void T11::add_ind_ded(uint16_t op)   { icount_ -= 21+21; { ADD_M(IND,DED); } }
void T11::add_ind_ix(uint16_t op)    { icount_ -= 21+21; { ADD_M(IND,IX);  } }
void T11::add_ind_ixd(uint16_t op)   { icount_ -= 21+27; { ADD_M(IND,IXD); } }
void T11::add_de_rg(uint16_t op)     { icount_ -= 18+ 3; { ADD_X(DE,RG);   } }
void T11::add_de_rgd(uint16_t op)    { icount_ -= 18+12; { ADD_M(DE,RGD);  } }
void T11::add_de_in(uint16_t op)     { icount_ -= 18+12; { ADD_M(DE,IN);   } }
void T11::add_de_ind(uint16_t op)    { icount_ -= 18+18; { ADD_M(DE,IND);  } }
void T11::add_de_de(uint16_t op)     { icount_ -= 18+15; { ADD_M(DE,DE);   } }
void T11::add_de_ded(uint16_t op)    { icount_ -= 18+21; { ADD_M(DE,DED);  } }
void T11::add_de_ix(uint16_t op)     { icount_ -= 18+21; { ADD_M(DE,IX);   } }
void T11::add_de_ixd(uint16_t op)    { icount_ -= 18+27; { ADD_M(DE,IXD);  } }
void T11::add_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { ADD_X(DED,RG);  } }
void T11::add_ded_rgd(uint16_t op)   { icount_ -= 24+12; { ADD_M(DED,RGD); } }
void T11::add_ded_in(uint16_t op)    { icount_ -= 24+12; { ADD_M(DED,IN);  } }
void T11::add_ded_ind(uint16_t op)   { icount_ -= 24+18; { ADD_M(DED,IND); } }
void T11::add_ded_de(uint16_t op)    { icount_ -= 24+15; { ADD_M(DED,DE);  } }
void T11::add_ded_ded(uint16_t op)   { icount_ -= 24+21; { ADD_M(DED,DED); } }
void T11::add_ded_ix(uint16_t op)    { icount_ -= 24+21; { ADD_M(DED,IX);  } }
void T11::add_ded_ixd(uint16_t op)   { icount_ -= 24+27; { ADD_M(DED,IXD); } }
void T11::add_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { ADD_X(IX,RG);   } }
void T11::add_ix_rgd(uint16_t op)    { icount_ -= 24+12; { ADD_M(IX,RGD);  } }
void T11::add_ix_in(uint16_t op)     { icount_ -= 24+12; { ADD_M(IX,IN);   } }
void T11::add_ix_ind(uint16_t op)    { icount_ -= 24+18; { ADD_M(IX,IND);  } }
void T11::add_ix_de(uint16_t op)     { icount_ -= 24+15; { ADD_M(IX,DE);   } }
void T11::add_ix_ded(uint16_t op)    { icount_ -= 24+21; { ADD_M(IX,DED);  } }
void T11::add_ix_ix(uint16_t op)     { icount_ -= 24+21; { ADD_M(IX,IX);   } }
void T11::add_ix_ixd(uint16_t op)    { icount_ -= 24+27; { ADD_M(IX,IXD);  } }
void T11::add_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { ADD_X(IXD,RG);  } }
void T11::add_ixd_rgd(uint16_t op)   { icount_ -= 30+12; { ADD_M(IXD,RGD); } }
void T11::add_ixd_in(uint16_t op)    { icount_ -= 30+12; { ADD_M(IXD,IN);  } }
void T11::add_ixd_ind(uint16_t op)   { icount_ -= 30+18; { ADD_M(IXD,IND); } }
void T11::add_ixd_de(uint16_t op)    { icount_ -= 30+15; { ADD_M(IXD,DE);  } }
void T11::add_ixd_ded(uint16_t op)   { icount_ -= 30+21; { ADD_M(IXD,DED); } }
void T11::add_ixd_ix(uint16_t op)    { icount_ -= 30+21; { ADD_M(IXD,IX);  } }
void T11::add_ixd_ixd(uint16_t op)   { icount_ -= 30+27; { ADD_M(IXD,IXD); } }

void T11::xor_rg(uint16_t op)        { icount_ -= 12; { XOR_R(RG);  } }
void T11::xor_rgd(uint16_t op)       { icount_ -= 21; { XOR_M(RGD); } }
void T11::xor_in(uint16_t op)        { icount_ -= 21; { XOR_M(IN);  } }
void T11::xor_ind(uint16_t op)       { icount_ -= 27; { XOR_M(IND); } }
void T11::xor_de(uint16_t op)        { icount_ -= 24; { XOR_M(DE);  } }
void T11::xor_ded(uint16_t op)       { icount_ -= 30; { XOR_M(DED); } }
void T11::xor_ix(uint16_t op)        { icount_ -= 30; { XOR_M(IX);  } }
void T11::xor_ixd(uint16_t op)       { icount_ -= 36; { XOR_M(IXD); } }

void T11::sob(uint16_t op)
{
	int sreg, source;

	icount_ -= 18;
	GET_SREG; source = REGD(sreg);
	source -= 1;
	REGW(sreg) = source;
	if (source)
		PC -= 2 * (op & 0x3f);
}

void T11::bpl(uint16_t op)           { icount_ -= 12; { BR(!GET_N); } }
void T11::bmi(uint16_t op)           { icount_ -= 12; { BR( GET_N); } }
void T11::bhi(uint16_t op)           { icount_ -= 12; { BR(!GET_C && !GET_Z); } }
void T11::blos(uint16_t op)          { icount_ -= 12; { BR( GET_C ||  GET_Z); } }
void T11::bvc(uint16_t op)           { icount_ -= 12; { BR(!GET_V); } }
void T11::bvs(uint16_t op)           { icount_ -= 12; { BR( GET_V); } }
void T11::bcc(uint16_t op)           { icount_ -= 12; { BR(!GET_C); } }
void T11::bcs(uint16_t op)           { icount_ -= 12; { BR( GET_C); } }

void T11::emt(uint16_t op)
{
	icount_ -= 48;
	trap_to(0x18);
}

void T11::trap(uint16_t op)
{
	icount_ -= 48;
	trap_to(0x1c);
}

void T11::clrb_rg(uint16_t op)       { icount_ -= 12; { CLRB_R(RG);  } }
void T11::clrb_rgd(uint16_t op)      { icount_ -= 21; { CLRB_M(RGD); } }
void T11::clrb_in(uint16_t op)       { icount_ -= 21; { CLRB_M(IN);  } }
void T11::clrb_ind(uint16_t op)      { icount_ -= 27; { CLRB_M(IND); } }
void T11::clrb_de(uint16_t op)       { icount_ -= 24; { CLRB_M(DE);  } }
void T11::clrb_ded(uint16_t op)      { icount_ -= 30; { CLRB_M(DED); } }
void T11::clrb_ix(uint16_t op)       { icount_ -= 30; { CLRB_M(IX);  } }
void T11::clrb_ixd(uint16_t op)      { icount_ -= 36; { CLRB_M(IXD); } }

void T11::comb_rg(uint16_t op)       { icount_ -= 12; { COMB_R(RG);  } }
void T11::comb_rgd(uint16_t op)      { icount_ -= 21; { COMB_M(RGD); } }
void T11::comb_in(uint16_t op)       { icount_ -= 21; { COMB_M(IN);  } }
void T11::comb_ind(uint16_t op)      { icount_ -= 27; { COMB_M(IND); } }
void T11::comb_de(uint16_t op)       { icount_ -= 24; { COMB_M(DE);  } }
void T11::comb_ded(uint16_t op)      { icount_ -= 30; { COMB_M(DED); } }
void T11::comb_ix(uint16_t op)       { icount_ -= 30; { COMB_M(IX);  } }
void T11::comb_ixd(uint16_t op)      { icount_ -= 36; { COMB_M(IXD); } }

void T11::incb_rg(uint16_t op)       { icount_ -= 12; { INCB_R(RG);  } }
void T11::incb_rgd(uint16_t op)      { icount_ -= 21; { INCB_M(RGD); } }
void T11::incb_in(uint16_t op)       { icount_ -= 21; { INCB_M(IN);  } }
void T11::incb_ind(uint16_t op)      { icount_ -= 27; { INCB_M(IND); } }
void T11::incb_de(uint16_t op)       { icount_ -= 24; { INCB_M(DE);  } }
void T11::incb_ded(uint16_t op)      { icount_ -= 30; { INCB_M(DED); } }
void T11::incb_ix(uint16_t op)       { icount_ -= 30; { INCB_M(IX);  } }
void T11::incb_ixd(uint16_t op)      { icount_ -= 36; { INCB_M(IXD); } }

void T11::decb_rg(uint16_t op)       { icount_ -= 12; { DECB_R(RG);  } }
void T11::decb_rgd(uint16_t op)      { icount_ -= 21; { DECB_M(RGD); } }
void T11::decb_in(uint16_t op)       { icount_ -= 21; { DECB_M(IN);  } }
void T11::decb_ind(uint16_t op)      { icount_ -= 27; { DECB_M(IND); } }
void T11::decb_de(uint16_t op)       { icount_ -= 24; { DECB_M(DE);  } }
void T11::decb_ded(uint16_t op)      { icount_ -= 30; { DECB_M(DED); } }
void T11::decb_ix(uint16_t op)       { icount_ -= 30; { DECB_M(IX);  } }
void T11::decb_ixd(uint16_t op)      { icount_ -= 36; { DECB_M(IXD); } }

void T11::negb_rg(uint16_t op)       { icount_ -= 12; { NEGB_R(RG);  } }
void T11::negb_rgd(uint16_t op)      { icount_ -= 21; { NEGB_M(RGD); } }
void T11::negb_in(uint16_t op)       { icount_ -= 21; { NEGB_M(IN);  } }
void T11::negb_ind(uint16_t op)      { icount_ -= 27; { NEGB_M(IND); } }
void T11::negb_de(uint16_t op)       { icount_ -= 24; { NEGB_M(DE);  } }
void T11::negb_ded(uint16_t op)      { icount_ -= 30; { NEGB_M(DED); } }
void T11::negb_ix(uint16_t op)       { icount_ -= 30; { NEGB_M(IX);  } }
void T11::negb_ixd(uint16_t op)      { icount_ -= 36; { NEGB_M(IXD); } }

void T11::adcb_rg(uint16_t op)       { icount_ -= 12; { ADCB_R(RG);  } }
void T11::adcb_rgd(uint16_t op)      { icount_ -= 21; { ADCB_M(RGD); } }
void T11::adcb_in(uint16_t op)       { icount_ -= 21; { ADCB_M(IN);  } }
void T11::adcb_ind(uint16_t op)      { icount_ -= 27; { ADCB_M(IND); } }
void T11::adcb_de(uint16_t op)       { icount_ -= 24; { ADCB_M(DE);  } }
void T11::adcb_ded(uint16_t op)      { icount_ -= 30; { ADCB_M(DED); } }
void T11::adcb_ix(uint16_t op)       { icount_ -= 30; { ADCB_M(IX);  } }
void T11::adcb_ixd(uint16_t op)      { icount_ -= 36; { ADCB_M(IXD); } }

void T11::sbcb_rg(uint16_t op)       { icount_ -= 12; { SBCB_R(RG);  } }
void T11::sbcb_rgd(uint16_t op)      { icount_ -= 21; { SBCB_M(RGD); } }
void T11::sbcb_in(uint16_t op)       { icount_ -= 21; { SBCB_M(IN);  } }
void T11::sbcb_ind(uint16_t op)      { icount_ -= 27; { SBCB_M(IND); } }
void T11::sbcb_de(uint16_t op)       { icount_ -= 24; { SBCB_M(DE);  } }
void T11::sbcb_ded(uint16_t op)      { icount_ -= 30; { SBCB_M(DED); } }
void T11::sbcb_ix(uint16_t op)       { icount_ -= 30; { SBCB_M(IX);  } }
void T11::sbcb_ixd(uint16_t op)      { icount_ -= 36; { SBCB_M(IXD); } }

void T11::tstb_rg(uint16_t op)       { icount_ -= 12; { TSTB_R(RG);  } }
void T11::tstb_rgd(uint16_t op)      { icount_ -= 18; { TSTB_M(RGD); } }
void T11::tstb_in(uint16_t op)       { icount_ -= 18; { TSTB_M(IN);  } }
void T11::tstb_ind(uint16_t op)      { icount_ -= 24; { TSTB_M(IND); } }
void T11::tstb_de(uint16_t op)       { icount_ -= 21; { TSTB_M(DE);  } }
void T11::tstb_ded(uint16_t op)      { icount_ -= 27; { TSTB_M(DED); } }
void T11::tstb_ix(uint16_t op)       { icount_ -= 27; { TSTB_M(IX);  } }
void T11::tstb_ixd(uint16_t op)      { icount_ -= 33; { TSTB_M(IXD); } }

void T11::rorb_rg(uint16_t op)       { icount_ -= 12; { RORB_R(RG);  } }
void T11::rorb_rgd(uint16_t op)      { icount_ -= 21; { RORB_M(RGD); } }
void T11::rorb_in(uint16_t op)       { icount_ -= 21; { RORB_M(IN);  } }
void T11::rorb_ind(uint16_t op)      { icount_ -= 27; { RORB_M(IND); } }
void T11::rorb_de(uint16_t op)       { icount_ -= 24; { RORB_M(DE);  } }
void T11::rorb_ded(uint16_t op)      { icount_ -= 30; { RORB_M(DED); } }
void T11::rorb_ix(uint16_t op)       { icount_ -= 30; { RORB_M(IX);  } }
void T11::rorb_ixd(uint16_t op)      { icount_ -= 36; { RORB_M(IXD); } }

void T11::rolb_rg(uint16_t op)       { icount_ -= 12; { ROLB_R(RG);  } }
void T11::rolb_rgd(uint16_t op)      { icount_ -= 21; { ROLB_M(RGD); } }
void T11::rolb_in(uint16_t op)       { icount_ -= 21; { ROLB_M(IN);  } }
void T11::rolb_ind(uint16_t op)      { icount_ -= 27; { ROLB_M(IND); } }
void T11::rolb_de(uint16_t op)       { icount_ -= 24; { ROLB_M(DE);  } }
void T11::rolb_ded(uint16_t op)      { icount_ -= 30; { ROLB_M(DED); } }
void T11::rolb_ix(uint16_t op)       { icount_ -= 30; { ROLB_M(IX);  } }
void T11::rolb_ixd(uint16_t op)      { icount_ -= 36; { ROLB_M(IXD); } }

void T11::asrb_rg(uint16_t op)       { icount_ -= 12; { ASRB_R(RG);  } }
void T11::asrb_rgd(uint16_t op)      { icount_ -= 21; { ASRB_M(RGD); } }
void T11::asrb_in(uint16_t op)       { icount_ -= 21; { ASRB_M(IN);  } }
void T11::asrb_ind(uint16_t op)      { icount_ -= 27; { ASRB_M(IND); } }
void T11::asrb_de(uint16_t op)       { icount_ -= 24; { ASRB_M(DE);  } }
void T11::asrb_ded(uint16_t op)      { icount_ -= 30; { ASRB_M(DED); } }
void T11::asrb_ix(uint16_t op)       { icount_ -= 30; { ASRB_M(IX);  } }
void T11::asrb_ixd(uint16_t op)      { icount_ -= 36; { ASRB_M(IXD); } }

void T11::aslb_rg(uint16_t op)       { icount_ -= 12; { ASLB_R(RG);  } }
void T11::aslb_rgd(uint16_t op)      { icount_ -= 21; { ASLB_M(RGD); } }
void T11::aslb_in(uint16_t op)       { icount_ -= 21; { ASLB_M(IN);  } }
void T11::aslb_ind(uint16_t op)      { icount_ -= 27; { ASLB_M(IND); } }
void T11::aslb_de(uint16_t op)       { icount_ -= 24; { ASLB_M(DE);  } }
void T11::aslb_ded(uint16_t op)      { icount_ -= 30; { ASLB_M(DED); } }
void T11::aslb_ix(uint16_t op)       { icount_ -= 30; { ASLB_M(IX);  } }
void T11::aslb_ixd(uint16_t op)      { icount_ -= 36; { ASLB_M(IXD); } }

void T11::mtps_rg(uint16_t op)       { icount_ -= 24; { MTPS_R(RG);  } }
void T11::mtps_rgd(uint16_t op)      { icount_ -= 30; { MTPS_M(RGD); } }
void T11::mtps_in(uint16_t op)       { icount_ -= 30; { MTPS_M(IN);  } }
void T11::mtps_ind(uint16_t op)      { icount_ -= 36; { MTPS_M(IND); } }
void T11::mtps_de(uint16_t op)       { icount_ -= 33; { MTPS_M(DE);  } }
void T11::mtps_ded(uint16_t op)      { icount_ -= 39; { MTPS_M(DED); } }
void T11::mtps_ix(uint16_t op)       { icount_ -= 39; { MTPS_M(IX);  } }
void T11::mtps_ixd(uint16_t op)      { icount_ -= 45; { MTPS_M(IXD); } }

void T11::mfps_rg(uint16_t op)       { icount_ -= 12; { MFPS_R(RG);  } }
void T11::mfps_rgd(uint16_t op)      { icount_ -= 21; { MFPS_M(RGD); } }
void T11::mfps_in(uint16_t op)       { icount_ -= 21; { MFPS_M(IN);  } }
void T11::mfps_ind(uint16_t op)      { icount_ -= 27; { MFPS_M(IND); } }
void T11::mfps_de(uint16_t op)       { icount_ -= 24; { MFPS_M(DE);  } }
void T11::mfps_ded(uint16_t op)      { icount_ -= 30; { MFPS_M(DED); } }
void T11::mfps_ix(uint16_t op)       { icount_ -= 30; { MFPS_M(IX);  } }
void T11::mfps_ixd(uint16_t op)      { icount_ -= 36; { MFPS_M(IXD); } }

void T11::movb_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { MOVB_R(RG,RG);   } }
void T11::movb_rg_rgd(uint16_t op)    { icount_ -=  9+12; { MOVB_M(RG,RGD);  } }
void T11::movb_rg_in(uint16_t op)     { icount_ -=  9+12; { MOVB_M(RG,IN);   } }
void T11::movb_rg_ind(uint16_t op)    { icount_ -=  9+18; { MOVB_M(RG,IND);  } }
void T11::movb_rg_de(uint16_t op)     { icount_ -=  9+15; { MOVB_M(RG,DE);   } }
void T11::movb_rg_ded(uint16_t op)    { icount_ -=  9+21; { MOVB_M(RG,DED);  } }
void T11::movb_rg_ix(uint16_t op)     { icount_ -=  9+21; { MOVB_M(RG,IX);   } }
void T11::movb_rg_ixd(uint16_t op)    { icount_ -=  9+27; { MOVB_M(RG,IXD);  } }
void T11::movb_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { MOVB_X(RGD,RG);  } }
void T11::movb_rgd_rgd(uint16_t op)   { icount_ -= 15+12; { MOVB_M(RGD,RGD); } }
void T11::movb_rgd_in(uint16_t op)    { icount_ -= 15+12; { MOVB_M(RGD,IN);  } }
void T11::movb_rgd_ind(uint16_t op)   { icount_ -= 15+18; { MOVB_M(RGD,IND); } }
void T11::movb_rgd_de(uint16_t op)    { icount_ -= 15+15; { MOVB_M(RGD,DE);  } }
void T11::movb_rgd_ded(uint16_t op)   { icount_ -= 15+21; { MOVB_M(RGD,DED); } }
void T11::movb_rgd_ix(uint16_t op)    { icount_ -= 15+21; { MOVB_M(RGD,IX);  } }
void T11::movb_rgd_ixd(uint16_t op)   { icount_ -= 15+27; { MOVB_M(RGD,IXD); } }
void T11::movb_in_rg(uint16_t op)     { icount_ -= 15+ 3; { MOVB_X(IN,RG);   } }
void T11::movb_in_rgd(uint16_t op)    { icount_ -= 15+12; { MOVB_M(IN,RGD);  } }
void T11::movb_in_in(uint16_t op)     { icount_ -= 15+12; { MOVB_M(IN,IN);   } }
void T11::movb_in_ind(uint16_t op)    { icount_ -= 15+18; { MOVB_M(IN,IND);  } }
void T11::movb_in_de(uint16_t op)     { icount_ -= 15+15; { MOVB_M(IN,DE);   } }
void T11::movb_in_ded(uint16_t op)    { icount_ -= 15+21; { MOVB_M(IN,DED);  } }
void T11::movb_in_ix(uint16_t op)     { icount_ -= 15+21; { MOVB_M(IN,IX);   } }
void T11::movb_in_ixd(uint16_t op)    { icount_ -= 15+27; { MOVB_M(IN,IXD);  } }
void T11::movb_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { MOVB_X(IND,RG);  } }
void T11::movb_ind_rgd(uint16_t op)   { icount_ -= 21+12; { MOVB_M(IND,RGD); } }
void T11::movb_ind_in(uint16_t op)    { icount_ -= 21+12; { MOVB_M(IND,IN);  } }
void T11::movb_ind_ind(uint16_t op)   { icount_ -= 21+18; { MOVB_M(IND,IND); } }
void T11::movb_ind_de(uint16_t op)    { icount_ -= 21+15; { MOVB_M(IND,DE);  } }
void T11::movb_ind_ded(uint16_t op)   { icount_ -= 21+21; { MOVB_M(IND,DED); } }
void T11::movb_ind_ix(uint16_t op)    { icount_ -= 21+21; { MOVB_M(IND,IX);  } }
void T11::movb_ind_ixd(uint16_t op)   { icount_ -= 21+27; { MOVB_M(IND,IXD); } }
void T11::movb_de_rg(uint16_t op)     { icount_ -= 18+ 3; { MOVB_X(DE,RG);   } }
void T11::movb_de_rgd(uint16_t op)    { icount_ -= 18+12; { MOVB_M(DE,RGD);  } }
void T11::movb_de_in(uint16_t op)     { icount_ -= 18+12; { MOVB_M(DE,IN);   } }
void T11::movb_de_ind(uint16_t op)    { icount_ -= 18+18; { MOVB_M(DE,IND);  } }
void T11::movb_de_de(uint16_t op)     { icount_ -= 18+15; { MOVB_M(DE,DE);   } }
void T11::movb_de_ded(uint16_t op)    { icount_ -= 18+21; { MOVB_M(DE,DED);  } }
void T11::movb_de_ix(uint16_t op)     { icount_ -= 18+21; { MOVB_M(DE,IX);   } }
void T11::movb_de_ixd(uint16_t op)    { icount_ -= 18+27; { MOVB_M(DE,IXD);  } }
void T11::movb_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { MOVB_X(DED,RG);  } }
void T11::movb_ded_rgd(uint16_t op)   { icount_ -= 24+12; { MOVB_M(DED,RGD); } }
void T11::movb_ded_in(uint16_t op)    { icount_ -= 24+12; { MOVB_M(DED,IN);  } }
void T11::movb_ded_ind(uint16_t op)   { icount_ -= 24+18; { MOVB_M(DED,IND); } }
void T11::movb_ded_de(uint16_t op)    { icount_ -= 24+15; { MOVB_M(DED,DE);  } }
void T11::movb_ded_ded(uint16_t op)   { icount_ -= 24+21; { MOVB_M(DED,DED); } }
void T11::movb_ded_ix(uint16_t op)    { icount_ -= 24+21; { MOVB_M(DED,IX);  } }
void T11::movb_ded_ixd(uint16_t op)   { icount_ -= 24+27; { MOVB_M(DED,IXD); } }
void T11::movb_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { MOVB_X(IX,RG);   } }
void T11::movb_ix_rgd(uint16_t op)    { icount_ -= 24+12; { MOVB_M(IX,RGD);  } }
void T11::movb_ix_in(uint16_t op)     { icount_ -= 24+12; { MOVB_M(IX,IN);   } }
void T11::movb_ix_ind(uint16_t op)    { icount_ -= 24+18; { MOVB_M(IX,IND);  } }
void T11::movb_ix_de(uint16_t op)     { icount_ -= 24+15; { MOVB_M(IX,DE);   } }
void T11::movb_ix_ded(uint16_t op)    { icount_ -= 24+21; { MOVB_M(IX,DED);  } }
void T11::movb_ix_ix(uint16_t op)     { icount_ -= 24+21; { MOVB_M(IX,IX);   } }
void T11::movb_ix_ixd(uint16_t op)    { icount_ -= 24+27; { MOVB_M(IX,IXD);  } }
void T11::movb_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { MOVB_X(IXD,RG);  } }
void T11::movb_ixd_rgd(uint16_t op)   { icount_ -= 30+12; { MOVB_M(IXD,RGD); } }
void T11::movb_ixd_in(uint16_t op)    { icount_ -= 30+12; { MOVB_M(IXD,IN);  } }
void T11::movb_ixd_ind(uint16_t op)   { icount_ -= 30+18; { MOVB_M(IXD,IND); } }
void T11::movb_ixd_de(uint16_t op)    { icount_ -= 30+15; { MOVB_M(IXD,DE);  } }
void T11::movb_ixd_ded(uint16_t op)   { icount_ -= 30+21; { MOVB_M(IXD,DED); } }
void T11::movb_ixd_ix(uint16_t op)    { icount_ -= 30+21; { MOVB_M(IXD,IX);  } }
void T11::movb_ixd_ixd(uint16_t op)   { icount_ -= 30+27; { MOVB_M(IXD,IXD); } }

void T11::cmpb_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { CMPB_R(RG,RG);   } }
void T11::cmpb_rg_rgd(uint16_t op)    { icount_ -=  9+ 9; { CMPB_M(RG,RGD);  } }
void T11::cmpb_rg_in(uint16_t op)     { icount_ -=  9+ 9; { CMPB_M(RG,IN);   } }
void T11::cmpb_rg_ind(uint16_t op)    { icount_ -=  9+15; { CMPB_M(RG,IND);  } }
void T11::cmpb_rg_de(uint16_t op)     { icount_ -=  9+12; { CMPB_M(RG,DE);   } }
void T11::cmpb_rg_ded(uint16_t op)    { icount_ -=  9+18; { CMPB_M(RG,DED);  } }
void T11::cmpb_rg_ix(uint16_t op)     { icount_ -=  9+18; { CMPB_M(RG,IX);   } }
void T11::cmpb_rg_ixd(uint16_t op)    { icount_ -=  9+24; { CMPB_M(RG,IXD);  } }
void T11::cmpb_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { CMPB_M(RGD,RG);  } }
void T11::cmpb_rgd_rgd(uint16_t op)   { icount_ -= 15+ 9; { CMPB_M(RGD,RGD); } }
void T11::cmpb_rgd_in(uint16_t op)    { icount_ -= 15+ 9; { CMPB_M(RGD,IN);  } }
void T11::cmpb_rgd_ind(uint16_t op)   { icount_ -= 15+15; { CMPB_M(RGD,IND); } }
void T11::cmpb_rgd_de(uint16_t op)    { icount_ -= 15+12; { CMPB_M(RGD,DE);  } }
void T11::cmpb_rgd_ded(uint16_t op)   { icount_ -= 15+18; { CMPB_M(RGD,DED); } }
void T11::cmpb_rgd_ix(uint16_t op)    { icount_ -= 15+18; { CMPB_M(RGD,IX);  } }
void T11::cmpb_rgd_ixd(uint16_t op)   { icount_ -= 15+24; { CMPB_M(RGD,IXD); } }
void T11::cmpb_in_rg(uint16_t op)     { icount_ -= 15+ 3; { CMPB_M(IN,RG);   } }
void T11::cmpb_in_rgd(uint16_t op)    { icount_ -= 15+ 9; { CMPB_M(IN,RGD);  } }
void T11::cmpb_in_in(uint16_t op)     { icount_ -= 15+ 9; { CMPB_M(IN,IN);   } }
void T11::cmpb_in_ind(uint16_t op)    { icount_ -= 15+15; { CMPB_M(IN,IND);  } }
void T11::cmpb_in_de(uint16_t op)     { icount_ -= 15+12; { CMPB_M(IN,DE);   } }
void T11::cmpb_in_ded(uint16_t op)    { icount_ -= 15+18; { CMPB_M(IN,DED);  } }
void T11::cmpb_in_ix(uint16_t op)     { icount_ -= 15+18; { CMPB_M(IN,IX);   } }
void T11::cmpb_in_ixd(uint16_t op)    { icount_ -= 15+24; { CMPB_M(IN,IXD);  } }
void T11::cmpb_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { CMPB_M(IND,RG);  } }
void T11::cmpb_ind_rgd(uint16_t op)   { icount_ -= 21+ 9; { CMPB_M(IND,RGD); } }
void T11::cmpb_ind_in(uint16_t op)    { icount_ -= 21+ 9; { CMPB_M(IND,IN);  } }
void T11::cmpb_ind_ind(uint16_t op)   { icount_ -= 21+15; { CMPB_M(IND,IND); } }
void T11::cmpb_ind_de(uint16_t op)    { icount_ -= 21+12; { CMPB_M(IND,DE);  } }
void T11::cmpb_ind_ded(uint16_t op)   { icount_ -= 21+18; { CMPB_M(IND,DED); } }
void T11::cmpb_ind_ix(uint16_t op)    { icount_ -= 21+18; { CMPB_M(IND,IX);  } }
void T11::cmpb_ind_ixd(uint16_t op)   { icount_ -= 21+24; { CMPB_M(IND,IXD); } }
void T11::cmpb_de_rg(uint16_t op)     { icount_ -= 18+ 3; { CMPB_M(DE,RG);   } }
void T11::cmpb_de_rgd(uint16_t op)    { icount_ -= 18+ 9; { CMPB_M(DE,RGD);  } }
void T11::cmpb_de_in(uint16_t op)     { icount_ -= 18+ 9; { CMPB_M(DE,IN);   } }
void T11::cmpb_de_ind(uint16_t op)    { icount_ -= 18+15; { CMPB_M(DE,IND);  } }
void T11::cmpb_de_de(uint16_t op)     { icount_ -= 18+12; { CMPB_M(DE,DE);   } }
void T11::cmpb_de_ded(uint16_t op)    { icount_ -= 18+18; { CMPB_M(DE,DED);  } }
void T11::cmpb_de_ix(uint16_t op)     { icount_ -= 18+18; { CMPB_M(DE,IX);   } }
void T11::cmpb_de_ixd(uint16_t op)    { icount_ -= 18+24; { CMPB_M(DE,IXD);  } }
void T11::cmpb_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { CMPB_M(DED,RG);  } }
void T11::cmpb_ded_rgd(uint16_t op)   { icount_ -= 24+ 9; { CMPB_M(DED,RGD); } }
void T11::cmpb_ded_in(uint16_t op)    { icount_ -= 24+ 9; { CMPB_M(DED,IN);  } }
void T11::cmpb_ded_ind(uint16_t op)   { icount_ -= 24+15; { CMPB_M(DED,IND); } }
void T11::cmpb_ded_de(uint16_t op)    { icount_ -= 24+12; { CMPB_M(DED,DE);  } }
void T11::cmpb_ded_ded(uint16_t op)   { icount_ -= 24+18; { CMPB_M(DED,DED); } }
void T11::cmpb_ded_ix(uint16_t op)    { icount_ -= 24+18; { CMPB_M(DED,IX);  } }
void T11::cmpb_ded_ixd(uint16_t op)   { icount_ -= 24+24; { CMPB_M(DED,IXD); } }
void T11::cmpb_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { CMPB_M(IX,RG);   } }
void T11::cmpb_ix_rgd(uint16_t op)    { icount_ -= 24+ 9; { CMPB_M(IX,RGD);  } }
void T11::cmpb_ix_in(uint16_t op)     { icount_ -= 24+ 9; { CMPB_M(IX,IN);   } }
void T11::cmpb_ix_ind(uint16_t op)    { icount_ -= 24+15; { CMPB_M(IX,IND);  } }
void T11::cmpb_ix_de(uint16_t op)     { icount_ -= 24+12; { CMPB_M(IX,DE);   } }
void T11::cmpb_ix_ded(uint16_t op)    { icount_ -= 24+18; { CMPB_M(IX,DED);  } }
void T11::cmpb_ix_ix(uint16_t op)     { icount_ -= 24+18; { CMPB_M(IX,IX);   } }
void T11::cmpb_ix_ixd(uint16_t op)    { icount_ -= 24+24; { CMPB_M(IX,IXD);  } }
void T11::cmpb_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { CMPB_M(IXD,RG);  } }
void T11::cmpb_ixd_rgd(uint16_t op)   { icount_ -= 30+ 9; { CMPB_M(IXD,RGD); } }
void T11::cmpb_ixd_in(uint16_t op)    { icount_ -= 30+ 9; { CMPB_M(IXD,IN);  } }
void T11::cmpb_ixd_ind(uint16_t op)   { icount_ -= 30+15; { CMPB_M(IXD,IND); } }
void T11::cmpb_ixd_de(uint16_t op)    { icount_ -= 30+12; { CMPB_M(IXD,DE);  } }
void T11::cmpb_ixd_ded(uint16_t op)   { icount_ -= 30+18; { CMPB_M(IXD,DED); } }
void T11::cmpb_ixd_ix(uint16_t op)    { icount_ -= 30+18; { CMPB_M(IXD,IX);  } }
void T11::cmpb_ixd_ixd(uint16_t op)   { icount_ -= 30+24; { CMPB_M(IXD,IXD); } }

void T11::bitb_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { BITB_R(RG,RG);   } }
void T11::bitb_rg_rgd(uint16_t op)    { icount_ -=  9+ 9; { BITB_M(RG,RGD);  } }
void T11::bitb_rg_in(uint16_t op)     { icount_ -=  9+ 9; { BITB_M(RG,IN);   } }
void T11::bitb_rg_ind(uint16_t op)    { icount_ -=  9+15; { BITB_M(RG,IND);  } }
void T11::bitb_rg_de(uint16_t op)     { icount_ -=  9+12; { BITB_M(RG,DE);   } }
void T11::bitb_rg_ded(uint16_t op)    { icount_ -=  9+18; { BITB_M(RG,DED);  } }
void T11::bitb_rg_ix(uint16_t op)     { icount_ -=  9+18; { BITB_M(RG,IX);   } }
void T11::bitb_rg_ixd(uint16_t op)    { icount_ -=  9+24; { BITB_M(RG,IXD);  } }
void T11::bitb_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { BITB_M(RGD,RG);  } }
void T11::bitb_rgd_rgd(uint16_t op)   { icount_ -= 15+ 9; { BITB_M(RGD,RGD); } }
void T11::bitb_rgd_in(uint16_t op)    { icount_ -= 15+ 9; { BITB_M(RGD,IN);  } }
void T11::bitb_rgd_ind(uint16_t op)   { icount_ -= 15+15; { BITB_M(RGD,IND); } }
void T11::bitb_rgd_de(uint16_t op)    { icount_ -= 15+12; { BITB_M(RGD,DE);  } }
void T11::bitb_rgd_ded(uint16_t op)   { icount_ -= 15+18; { BITB_M(RGD,DED); } }
void T11::bitb_rgd_ix(uint16_t op)    { icount_ -= 15+18; { BITB_M(RGD,IX);  } }
void T11::bitb_rgd_ixd(uint16_t op)   { icount_ -= 15+24; { BITB_M(RGD,IXD); } }
void T11::bitb_in_rg(uint16_t op)     { icount_ -= 15+ 3; { BITB_M(IN,RG);   } }
void T11::bitb_in_rgd(uint16_t op)    { icount_ -= 15+ 9; { BITB_M(IN,RGD);  } }
void T11::bitb_in_in(uint16_t op)     { icount_ -= 15+ 9; { BITB_M(IN,IN);   } }
void T11::bitb_in_ind(uint16_t op)    { icount_ -= 15+15; { BITB_M(IN,IND);  } }
void T11::bitb_in_de(uint16_t op)     { icount_ -= 15+12; { BITB_M(IN,DE);   } }
void T11::bitb_in_ded(uint16_t op)    { icount_ -= 15+18; { BITB_M(IN,DED);  } }
void T11::bitb_in_ix(uint16_t op)     { icount_ -= 15+18; { BITB_M(IN,IX);   } }
void T11::bitb_in_ixd(uint16_t op)    { icount_ -= 15+24; { BITB_M(IN,IXD);  } }
void T11::bitb_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { BITB_M(IND,RG);  } }
void T11::bitb_ind_rgd(uint16_t op)   { icount_ -= 21+ 9; { BITB_M(IND,RGD); } }
void T11::bitb_ind_in(uint16_t op)    { icount_ -= 21+ 9; { BITB_M(IND,IN);  } }
void T11::bitb_ind_ind(uint16_t op)   { icount_ -= 21+15; { BITB_M(IND,IND); } }
void T11::bitb_ind_de(uint16_t op)    { icount_ -= 21+12; { BITB_M(IND,DE);  } }
void T11::bitb_ind_ded(uint16_t op)   { icount_ -= 21+18; { BITB_M(IND,DED); } }
void T11::bitb_ind_ix(uint16_t op)    { icount_ -= 21+18; { BITB_M(IND,IX);  } }
void T11::bitb_ind_ixd(uint16_t op)   { icount_ -= 21+24; { BITB_M(IND,IXD); } }
void T11::bitb_de_rg(uint16_t op)     { icount_ -= 18+ 3; { BITB_M(DE,RG);   } }
void T11::bitb_de_rgd(uint16_t op)    { icount_ -= 18+ 9; { BITB_M(DE,RGD);  } }
void T11::bitb_de_in(uint16_t op)     { icount_ -= 18+ 9; { BITB_M(DE,IN);   } }
void T11::bitb_de_ind(uint16_t op)    { icount_ -= 18+15; { BITB_M(DE,IND);  } }
void T11::bitb_de_de(uint16_t op)     { icount_ -= 18+12; { BITB_M(DE,DE);   } }
void T11::bitb_de_ded(uint16_t op)    { icount_ -= 18+18; { BITB_M(DE,DED);  } }
void T11::bitb_de_ix(uint16_t op)     { icount_ -= 18+18; { BITB_M(DE,IX);   } }
void T11::bitb_de_ixd(uint16_t op)    { icount_ -= 18+24; { BITB_M(DE,IXD);  } }
void T11::bitb_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { BITB_M(DED,RG);  } }
void T11::bitb_ded_rgd(uint16_t op)   { icount_ -= 24+ 9; { BITB_M(DED,RGD); } }
void T11::bitb_ded_in(uint16_t op)    { icount_ -= 24+ 9; { BITB_M(DED,IN);  } }
void T11::bitb_ded_ind(uint16_t op)   { icount_ -= 24+15; { BITB_M(DED,IND); } }
void T11::bitb_ded_de(uint16_t op)    { icount_ -= 24+12; { BITB_M(DED,DE);  } }
void T11::bitb_ded_ded(uint16_t op)   { icount_ -= 24+18; { BITB_M(DED,DED); } }
void T11::bitb_ded_ix(uint16_t op)    { icount_ -= 24+18; { BITB_M(DED,IX);  } }
void T11::bitb_ded_ixd(uint16_t op)   { icount_ -= 24+24; { BITB_M(DED,IXD); } }
void T11::bitb_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { BITB_M(IX,RG);   } }
void T11::bitb_ix_rgd(uint16_t op)    { icount_ -= 24+ 9; { BITB_M(IX,RGD);  } }
void T11::bitb_ix_in(uint16_t op)     { icount_ -= 24+ 9; { BITB_M(IX,IN);   } }
void T11::bitb_ix_ind(uint16_t op)    { icount_ -= 24+15; { BITB_M(IX,IND);  } }
void T11::bitb_ix_de(uint16_t op)     { icount_ -= 24+12; { BITB_M(IX,DE);   } }
void T11::bitb_ix_ded(uint16_t op)    { icount_ -= 24+18; { BITB_M(IX,DED);  } }
void T11::bitb_ix_ix(uint16_t op)     { icount_ -= 24+18; { BITB_M(IX,IX);   } }
void T11::bitb_ix_ixd(uint16_t op)    { icount_ -= 24+24; { BITB_M(IX,IXD);  } }
void T11::bitb_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { BITB_M(IXD,RG);  } }
void T11::bitb_ixd_rgd(uint16_t op)   { icount_ -= 30+ 9; { BITB_M(IXD,RGD); } }
void T11::bitb_ixd_in(uint16_t op)    { icount_ -= 30+ 9; { BITB_M(IXD,IN);  } }
void T11::bitb_ixd_ind(uint16_t op)   { icount_ -= 30+15; { BITB_M(IXD,IND); } }
void T11::bitb_ixd_de(uint16_t op)    { icount_ -= 30+12; { BITB_M(IXD,DE);  } }
void T11::bitb_ixd_ded(uint16_t op)   { icount_ -= 30+18; { BITB_M(IXD,DED); } }
void T11::bitb_ixd_ix(uint16_t op)    { icount_ -= 30+18; { BITB_M(IXD,IX);  } }
void T11::bitb_ixd_ixd(uint16_t op)   { icount_ -= 30+24; { BITB_M(IXD,IXD); } }

void T11::bicb_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { BICB_R(RG,RG);   } }
void T11::bicb_rg_rgd(uint16_t op)    { icount_ -=  9+12; { BICB_M(RG,RGD);  } }
void T11::bicb_rg_in(uint16_t op)     { icount_ -=  9+12; { BICB_M(RG,IN);   } }
void T11::bicb_rg_ind(uint16_t op)    { icount_ -=  9+18; { BICB_M(RG,IND);  } }
void T11::bicb_rg_de(uint16_t op)     { icount_ -=  9+15; { BICB_M(RG,DE);   } }
void T11::bicb_rg_ded(uint16_t op)    { icount_ -=  9+21; { BICB_M(RG,DED);  } }
void T11::bicb_rg_ix(uint16_t op)     { icount_ -=  9+21; { BICB_M(RG,IX);   } }
void T11::bicb_rg_ixd(uint16_t op)    { icount_ -=  9+27; { BICB_M(RG,IXD);  } }
void T11::bicb_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { BICB_X(RGD,RG);  } }
void T11::bicb_rgd_rgd(uint16_t op)   { icount_ -= 15+12; { BICB_M(RGD,RGD); } }
void T11::bicb_rgd_in(uint16_t op)    { icount_ -= 15+12; { BICB_M(RGD,IN);  } }
void T11::bicb_rgd_ind(uint16_t op)   { icount_ -= 15+18; { BICB_M(RGD,IND); } }
void T11::bicb_rgd_de(uint16_t op)    { icount_ -= 15+15; { BICB_M(RGD,DE);  } }
void T11::bicb_rgd_ded(uint16_t op)   { icount_ -= 15+21; { BICB_M(RGD,DED); } }
void T11::bicb_rgd_ix(uint16_t op)    { icount_ -= 15+21; { BICB_M(RGD,IX);  } }
void T11::bicb_rgd_ixd(uint16_t op)   { icount_ -= 15+27; { BICB_M(RGD,IXD); } }
void T11::bicb_in_rg(uint16_t op)     { icount_ -= 15+ 3; { BICB_X(IN,RG);   } }
void T11::bicb_in_rgd(uint16_t op)    { icount_ -= 15+12; { BICB_M(IN,RGD);  } }
void T11::bicb_in_in(uint16_t op)     { icount_ -= 15+12; { BICB_M(IN,IN);   } }
void T11::bicb_in_ind(uint16_t op)    { icount_ -= 15+18; { BICB_M(IN,IND);  } }
void T11::bicb_in_de(uint16_t op)     { icount_ -= 15+15; { BICB_M(IN,DE);   } }
void T11::bicb_in_ded(uint16_t op)    { icount_ -= 15+21; { BICB_M(IN,DED);  } }
void T11::bicb_in_ix(uint16_t op)     { icount_ -= 15+21; { BICB_M(IN,IX);   } }
void T11::bicb_in_ixd(uint16_t op)    { icount_ -= 15+27; { BICB_M(IN,IXD);  } }
void T11::bicb_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { BICB_X(IND,RG);  } }
void T11::bicb_ind_rgd(uint16_t op)   { icount_ -= 21+12; { BICB_M(IND,RGD); } }
void T11::bicb_ind_in(uint16_t op)    { icount_ -= 21+12; { BICB_M(IND,IN);  } }
void T11::bicb_ind_ind(uint16_t op)   { icount_ -= 21+18; { BICB_M(IND,IND); } }
void T11::bicb_ind_de(uint16_t op)    { icount_ -= 21+15; { BICB_M(IND,DE);  } }
void T11::bicb_ind_ded(uint16_t op)   { icount_ -= 21+21; { BICB_M(IND,DED); } }
void T11::bicb_ind_ix(uint16_t op)    { icount_ -= 21+21; { BICB_M(IND,IX);  } }
void T11::bicb_ind_ixd(uint16_t op)   { icount_ -= 21+27; { BICB_M(IND,IXD); } }
void T11::bicb_de_rg(uint16_t op)     { icount_ -= 18+ 3; { BICB_X(DE,RG);   } }
void T11::bicb_de_rgd(uint16_t op)    { icount_ -= 18+12; { BICB_M(DE,RGD);  } }
void T11::bicb_de_in(uint16_t op)     { icount_ -= 18+12; { BICB_M(DE,IN);   } }
void T11::bicb_de_ind(uint16_t op)    { icount_ -= 18+18; { BICB_M(DE,IND);  } }
void T11::bicb_de_de(uint16_t op)     { icount_ -= 18+15; { BICB_M(DE,DE);   } }
void T11::bicb_de_ded(uint16_t op)    { icount_ -= 18+21; { BICB_M(DE,DED);  } }
void T11::bicb_de_ix(uint16_t op)     { icount_ -= 18+21; { BICB_M(DE,IX);   } }
void T11::bicb_de_ixd(uint16_t op)    { icount_ -= 18+27; { BICB_M(DE,IXD);  } }
void T11::bicb_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { BICB_X(DED,RG);  } }
void T11::bicb_ded_rgd(uint16_t op)   { icount_ -= 24+12; { BICB_M(DED,RGD); } }
void T11::bicb_ded_in(uint16_t op)    { icount_ -= 24+12; { BICB_M(DED,IN);  } }
void T11::bicb_ded_ind(uint16_t op)   { icount_ -= 24+18; { BICB_M(DED,IND); } }
void T11::bicb_ded_de(uint16_t op)    { icount_ -= 24+15; { BICB_M(DED,DE);  } }
void T11::bicb_ded_ded(uint16_t op)   { icount_ -= 24+21; { BICB_M(DED,DED); } }
void T11::bicb_ded_ix(uint16_t op)    { icount_ -= 24+21; { BICB_M(DED,IX);  } }
void T11::bicb_ded_ixd(uint16_t op)   { icount_ -= 24+27; { BICB_M(DED,IXD); } }
void T11::bicb_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { BICB_X(IX,RG);   } }
void T11::bicb_ix_rgd(uint16_t op)    { icount_ -= 24+12; { BICB_M(IX,RGD);  } }
void T11::bicb_ix_in(uint16_t op)     { icount_ -= 24+12; { BICB_M(IX,IN);   } }
void T11::bicb_ix_ind(uint16_t op)    { icount_ -= 24+18; { BICB_M(IX,IND);  } }
void T11::bicb_ix_de(uint16_t op)     { icount_ -= 24+15; { BICB_M(IX,DE);   } }
void T11::bicb_ix_ded(uint16_t op)    { icount_ -= 24+21; { BICB_M(IX,DED);  } }
void T11::bicb_ix_ix(uint16_t op)     { icount_ -= 24+21; { BICB_M(IX,IX);   } }
void T11::bicb_ix_ixd(uint16_t op)    { icount_ -= 24+27; { BICB_M(IX,IXD);  } }
void T11::bicb_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { BICB_X(IXD,RG);  } }
void T11::bicb_ixd_rgd(uint16_t op)   { icount_ -= 30+12; { BICB_M(IXD,RGD); } }
void T11::bicb_ixd_in(uint16_t op)    { icount_ -= 30+12; { BICB_M(IXD,IN);  } }
void T11::bicb_ixd_ind(uint16_t op)   { icount_ -= 30+18; { BICB_M(IXD,IND); } }
void T11::bicb_ixd_de(uint16_t op)    { icount_ -= 30+15; { BICB_M(IXD,DE);  } }
void T11::bicb_ixd_ded(uint16_t op)   { icount_ -= 30+21; { BICB_M(IXD,DED); } }
void T11::bicb_ixd_ix(uint16_t op)    { icount_ -= 30+21; { BICB_M(IXD,IX);  } }
void T11::bicb_ixd_ixd(uint16_t op)   { icount_ -= 30+27; { BICB_M(IXD,IXD); } }

void T11::bisb_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { BISB_R(RG,RG);   } }
void T11::bisb_rg_rgd(uint16_t op)    { icount_ -=  9+12; { BISB_M(RG,RGD);  } }
void T11::bisb_rg_in(uint16_t op)     { icount_ -=  9+12; { BISB_M(RG,IN);   } }
void T11::bisb_rg_ind(uint16_t op)    { icount_ -=  9+18; { BISB_M(RG,IND);  } }
void T11::bisb_rg_de(uint16_t op)     { icount_ -=  9+15; { BISB_M(RG,DE);   } }
void T11::bisb_rg_ded(uint16_t op)    { icount_ -=  9+21; { BISB_M(RG,DED);  } }
void T11::bisb_rg_ix(uint16_t op)     { icount_ -=  9+21; { BISB_M(RG,IX);   } }
void T11::bisb_rg_ixd(uint16_t op)    { icount_ -=  9+27; { BISB_M(RG,IXD);  } }
void T11::bisb_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { BISB_X(RGD,RG);  } }
void T11::bisb_rgd_rgd(uint16_t op)   { icount_ -= 15+12; { BISB_M(RGD,RGD); } }
void T11::bisb_rgd_in(uint16_t op)    { icount_ -= 15+12; { BISB_M(RGD,IN);  } }
void T11::bisb_rgd_ind(uint16_t op)   { icount_ -= 15+18; { BISB_M(RGD,IND); } }
void T11::bisb_rgd_de(uint16_t op)    { icount_ -= 15+15; { BISB_M(RGD,DE);  } }
void T11::bisb_rgd_ded(uint16_t op)   { icount_ -= 15+21; { BISB_M(RGD,DED); } }
void T11::bisb_rgd_ix(uint16_t op)    { icount_ -= 15+21; { BISB_M(RGD,IX);  } }
void T11::bisb_rgd_ixd(uint16_t op)   { icount_ -= 15+27; { BISB_M(RGD,IXD); } }
void T11::bisb_in_rg(uint16_t op)     { icount_ -= 15+ 3; { BISB_X(IN,RG);   } }
void T11::bisb_in_rgd(uint16_t op)    { icount_ -= 15+12; { BISB_M(IN,RGD);  } }
void T11::bisb_in_in(uint16_t op)     { icount_ -= 15+12; { BISB_M(IN,IN);   } }
void T11::bisb_in_ind(uint16_t op)    { icount_ -= 15+18; { BISB_M(IN,IND);  } }
void T11::bisb_in_de(uint16_t op)     { icount_ -= 15+15; { BISB_M(IN,DE);   } }
void T11::bisb_in_ded(uint16_t op)    { icount_ -= 15+21; { BISB_M(IN,DED);  } }
void T11::bisb_in_ix(uint16_t op)     { icount_ -= 15+21; { BISB_M(IN,IX);   } }
void T11::bisb_in_ixd(uint16_t op)    { icount_ -= 15+27; { BISB_M(IN,IXD);  } }
void T11::bisb_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { BISB_X(IND,RG);  } }
void T11::bisb_ind_rgd(uint16_t op)   { icount_ -= 21+12; { BISB_M(IND,RGD); } }
void T11::bisb_ind_in(uint16_t op)    { icount_ -= 21+12; { BISB_M(IND,IN);  } }
void T11::bisb_ind_ind(uint16_t op)   { icount_ -= 21+18; { BISB_M(IND,IND); } }
void T11::bisb_ind_de(uint16_t op)    { icount_ -= 21+15; { BISB_M(IND,DE);  } }
void T11::bisb_ind_ded(uint16_t op)   { icount_ -= 21+21; { BISB_M(IND,DED); } }
void T11::bisb_ind_ix(uint16_t op)    { icount_ -= 21+21; { BISB_M(IND,IX);  } }
void T11::bisb_ind_ixd(uint16_t op)   { icount_ -= 21+27; { BISB_M(IND,IXD); } }
void T11::bisb_de_rg(uint16_t op)     { icount_ -= 18+ 3; { BISB_X(DE,RG);   } }
void T11::bisb_de_rgd(uint16_t op)    { icount_ -= 18+12; { BISB_M(DE,RGD);  } }
void T11::bisb_de_in(uint16_t op)     { icount_ -= 18+12; { BISB_M(DE,IN);   } }
void T11::bisb_de_ind(uint16_t op)    { icount_ -= 18+18; { BISB_M(DE,IND);  } }
void T11::bisb_de_de(uint16_t op)     { icount_ -= 18+15; { BISB_M(DE,DE);   } }
void T11::bisb_de_ded(uint16_t op)    { icount_ -= 18+21; { BISB_M(DE,DED);  } }
void T11::bisb_de_ix(uint16_t op)     { icount_ -= 18+21; { BISB_M(DE,IX);   } }
void T11::bisb_de_ixd(uint16_t op)    { icount_ -= 18+27; { BISB_M(DE,IXD);  } }
void T11::bisb_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { BISB_X(DED,RG);  } }
void T11::bisb_ded_rgd(uint16_t op)   { icount_ -= 24+12; { BISB_M(DED,RGD); } }
void T11::bisb_ded_in(uint16_t op)    { icount_ -= 24+12; { BISB_M(DED,IN);  } }
void T11::bisb_ded_ind(uint16_t op)   { icount_ -= 24+18; { BISB_M(DED,IND); } }
void T11::bisb_ded_de(uint16_t op)    { icount_ -= 24+15; { BISB_M(DED,DE);  } }
void T11::bisb_ded_ded(uint16_t op)   { icount_ -= 24+21; { BISB_M(DED,DED); } }
void T11::bisb_ded_ix(uint16_t op)    { icount_ -= 24+21; { BISB_M(DED,IX);  } }
void T11::bisb_ded_ixd(uint16_t op)   { icount_ -= 24+27; { BISB_M(DED,IXD); } }
void T11::bisb_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { BISB_X(IX,RG);   } }
void T11::bisb_ix_rgd(uint16_t op)    { icount_ -= 24+12; { BISB_M(IX,RGD);  } }
void T11::bisb_ix_in(uint16_t op)     { icount_ -= 24+12; { BISB_M(IX,IN);   } }
void T11::bisb_ix_ind(uint16_t op)    { icount_ -= 24+18; { BISB_M(IX,IND);  } }
void T11::bisb_ix_de(uint16_t op)     { icount_ -= 24+15; { BISB_M(IX,DE);   } }
void T11::bisb_ix_ded(uint16_t op)    { icount_ -= 24+21; { BISB_M(IX,DED);  } }
void T11::bisb_ix_ix(uint16_t op)     { icount_ -= 24+21; { BISB_M(IX,IX);   } }
void T11::bisb_ix_ixd(uint16_t op)    { icount_ -= 24+27; { BISB_M(IX,IXD);  } }
void T11::bisb_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { BISB_X(IXD,RG);  } }
void T11::bisb_ixd_rgd(uint16_t op)   { icount_ -= 30+12; { BISB_M(IXD,RGD); } }
void T11::bisb_ixd_in(uint16_t op)    { icount_ -= 30+12; { BISB_M(IXD,IN);  } }
void T11::bisb_ixd_ind(uint16_t op)   { icount_ -= 30+18; { BISB_M(IXD,IND); } }
void T11::bisb_ixd_de(uint16_t op)    { icount_ -= 30+15; { BISB_M(IXD,DE);  } }
void T11::bisb_ixd_ded(uint16_t op)   { icount_ -= 30+21; { BISB_M(IXD,DED); } }
void T11::bisb_ixd_ix(uint16_t op)    { icount_ -= 30+21; { BISB_M(IXD,IX);  } }
void T11::bisb_ixd_ixd(uint16_t op)   { icount_ -= 30+27; { BISB_M(IXD,IXD); } }

void T11::sub_rg_rg(uint16_t op)     { icount_ -=  9+ 3; { SUB_R(RG,RG);   } }
void T11::sub_rg_rgd(uint16_t op)    { icount_ -=  9+12; { SUB_M(RG,RGD);  } }
void T11::sub_rg_in(uint16_t op)     { icount_ -=  9+12; { SUB_M(RG,IN);   } }
void T11::sub_rg_ind(uint16_t op)    { icount_ -=  9+18; { SUB_M(RG,IND);  } }
void T11::sub_rg_de(uint16_t op)     { icount_ -=  9+15; { SUB_M(RG,DE);   } }
void T11::sub_rg_ded(uint16_t op)    { icount_ -=  9+21; { SUB_M(RG,DED);  } }
void T11::sub_rg_ix(uint16_t op)     { icount_ -=  9+21; { SUB_M(RG,IX);   } }
void T11::sub_rg_ixd(uint16_t op)    { icount_ -=  9+27; { SUB_M(RG,IXD);  } }
void T11::sub_rgd_rg(uint16_t op)    { icount_ -= 15+ 3; { SUB_X(RGD,RG);  } }
void T11::sub_rgd_rgd(uint16_t op)   { icount_ -= 15+12; { SUB_M(RGD,RGD); } }
void T11::sub_rgd_in(uint16_t op)    { icount_ -= 15+12; { SUB_M(RGD,IN);  } }
void T11::sub_rgd_ind(uint16_t op)   { icount_ -= 15+18; { SUB_M(RGD,IND); } }
void T11::sub_rgd_de(uint16_t op)    { icount_ -= 15+15; { SUB_M(RGD,DE);  } }
void T11::sub_rgd_ded(uint16_t op)   { icount_ -= 15+21; { SUB_M(RGD,DED); } }
void T11::sub_rgd_ix(uint16_t op)    { icount_ -= 15+21; { SUB_M(RGD,IX);  } }
void T11::sub_rgd_ixd(uint16_t op)   { icount_ -= 15+27; { SUB_M(RGD,IXD); } }
void T11::sub_in_rg(uint16_t op)     { icount_ -= 15+ 3; { SUB_X(IN,RG);   } }
void T11::sub_in_rgd(uint16_t op)    { icount_ -= 15+12; { SUB_M(IN,RGD);  } }
void T11::sub_in_in(uint16_t op)     { icount_ -= 15+12; { SUB_M(IN,IN);   } }
void T11::sub_in_ind(uint16_t op)    { icount_ -= 15+18; { SUB_M(IN,IND);  } }
void T11::sub_in_de(uint16_t op)     { icount_ -= 15+15; { SUB_M(IN,DE);   } }
void T11::sub_in_ded(uint16_t op)    { icount_ -= 15+21; { SUB_M(IN,DED);  } }
void T11::sub_in_ix(uint16_t op)     { icount_ -= 15+21; { SUB_M(IN,IX);   } }
void T11::sub_in_ixd(uint16_t op)    { icount_ -= 15+27; { SUB_M(IN,IXD);  } }
void T11::sub_ind_rg(uint16_t op)    { icount_ -= 21+ 3; { SUB_X(IND,RG);  } }
void T11::sub_ind_rgd(uint16_t op)   { icount_ -= 21+12; { SUB_M(IND,RGD); } }
void T11::sub_ind_in(uint16_t op)    { icount_ -= 21+12; { SUB_M(IND,IN);  } }
void T11::sub_ind_ind(uint16_t op)   { icount_ -= 21+18; { SUB_M(IND,IND); } }
void T11::sub_ind_de(uint16_t op)    { icount_ -= 21+15; { SUB_M(IND,DE);  } }
void T11::sub_ind_ded(uint16_t op)   { icount_ -= 21+21; { SUB_M(IND,DED); } }
void T11::sub_ind_ix(uint16_t op)    { icount_ -= 21+21; { SUB_M(IND,IX);  } }
void T11::sub_ind_ixd(uint16_t op)   { icount_ -= 21+27; { SUB_M(IND,IXD); } }
void T11::sub_de_rg(uint16_t op)     { icount_ -= 18+ 3; { SUB_X(DE,RG);   } }
void T11::sub_de_rgd(uint16_t op)    { icount_ -= 18+12; { SUB_M(DE,RGD);  } }
void T11::sub_de_in(uint16_t op)     { icount_ -= 18+12; { SUB_M(DE,IN);   } }
void T11::sub_de_ind(uint16_t op)    { icount_ -= 18+18; { SUB_M(DE,IND);  } }
void T11::sub_de_de(uint16_t op)     { icount_ -= 18+15; { SUB_M(DE,DE);   } }
void T11::sub_de_ded(uint16_t op)    { icount_ -= 18+21; { SUB_M(DE,DED);  } }
void T11::sub_de_ix(uint16_t op)     { icount_ -= 18+21; { SUB_M(DE,IX);   } }
void T11::sub_de_ixd(uint16_t op)    { icount_ -= 18+27; { SUB_M(DE,IXD);  } }
void T11::sub_ded_rg(uint16_t op)    { icount_ -= 24+ 3; { SUB_X(DED,RG);  } }
void T11::sub_ded_rgd(uint16_t op)   { icount_ -= 24+12; { SUB_M(DED,RGD); } }
void T11::sub_ded_in(uint16_t op)    { icount_ -= 24+12; { SUB_M(DED,IN);  } }
void T11::sub_ded_ind(uint16_t op)   { icount_ -= 24+18; { SUB_M(DED,IND); } }
void T11::sub_ded_de(uint16_t op)    { icount_ -= 24+15; { SUB_M(DED,DE);  } }
void T11::sub_ded_ded(uint16_t op)   { icount_ -= 24+21; { SUB_M(DED,DED); } }
void T11::sub_ded_ix(uint16_t op)    { icount_ -= 24+21; { SUB_M(DED,IX);  } }
void T11::sub_ded_ixd(uint16_t op)   { icount_ -= 24+27; { SUB_M(DED,IXD); } }
void T11::sub_ix_rg(uint16_t op)     { icount_ -= 24+ 3; { SUB_X(IX,RG);   } }
void T11::sub_ix_rgd(uint16_t op)    { icount_ -= 24+12; { SUB_M(IX,RGD);  } }
void T11::sub_ix_in(uint16_t op)     { icount_ -= 24+12; { SUB_M(IX,IN);   } }
void T11::sub_ix_ind(uint16_t op)    { icount_ -= 24+18; { SUB_M(IX,IND);  } }
void T11::sub_ix_de(uint16_t op)     { icount_ -= 24+15; { SUB_M(IX,DE);   } }
void T11::sub_ix_ded(uint16_t op)    { icount_ -= 24+21; { SUB_M(IX,DED);  } }
void T11::sub_ix_ix(uint16_t op)     { icount_ -= 24+21; { SUB_M(IX,IX);   } }
void T11::sub_ix_ixd(uint16_t op)    { icount_ -= 24+27; { SUB_M(IX,IXD);  } }
void T11::sub_ixd_rg(uint16_t op)    { icount_ -= 30+ 3; { SUB_X(IXD,RG);  } }
void T11::sub_ixd_rgd(uint16_t op)   { icount_ -= 30+12; { SUB_M(IXD,RGD); } }
void T11::sub_ixd_in(uint16_t op)    { icount_ -= 30+12; { SUB_M(IXD,IN);  } }
void T11::sub_ixd_ind(uint16_t op)   { icount_ -= 30+18; { SUB_M(IXD,IND); } }
void T11::sub_ixd_de(uint16_t op)    { icount_ -= 30+15; { SUB_M(IXD,DE);  } }
void T11::sub_ixd_ded(uint16_t op)   { icount_ -= 30+21; { SUB_M(IXD,DED); } }
void T11::sub_ixd_ix(uint16_t op)    { icount_ -= 30+21; { SUB_M(IXD,IX);  } }
void T11::sub_ixd_ixd(uint16_t op)   { icount_ -= 30+27; { SUB_M(IXD,IXD); } }
