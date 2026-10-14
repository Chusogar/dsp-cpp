package dsp.cpu;

/**
 * Z80 CPU core, ported from dsp-cpp {@code cpu/z80.cpp} (DSP emulator nz80.pas).
 * Standard Z80 including undocumented IXH/IXL, SLL and DDCB copies.
 */
public final class Z80 {
    public static final int CF = 0x01;
    public static final int NF = 0x02;
    public static final int PF = 0x04;
    public static final int XF = 0x08;
    public static final int HF = 0x10;
    public static final int YF = 0x20;
    public static final int ZF = 0x40;
    public static final int SF = 0x80;

    private static final int[] T_MAIN = {
            4, 10, 7, 6, 4, 4, 7, 4, 4, 11, 7, 6, 4, 4, 7, 4,
            8, 10, 7, 6, 4, 4, 7, 4, 12, 11, 7, 6, 4, 4, 7, 4,
            7, 10, 16, 6, 4, 4, 7, 4, 7, 11, 16, 6, 4, 4, 7, 4,
            7, 10, 13, 6, 11, 11, 10, 4, 7, 11, 13, 6, 4, 4, 7, 4,
            4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
            4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
            4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
            7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
            4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
            4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
            4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
            4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
            5, 10, 10, 10, 10, 11, 7, 11, 5, 10, 10, 4, 10, 17, 7, 11,
            5, 10, 10, 11, 10, 11, 7, 11, 5, 4, 10, 11, 10, 4, 7, 11,
            5, 10, 10, 19, 10, 11, 7, 11, 5, 4, 10, 4, 10, 4, 7, 11,
            5, 10, 10, 4, 10, 11, 7, 11, 5, 6, 10, 4, 10, 4, 7, 11
    };

    private static final int[] T_CB = buildFilled(4, new int[] {6, 14, 22, 30, 38, 46, 54, 62,
            70, 78, 86, 94, 102, 110, 118, 126, 134, 142, 150, 158, 166, 174, 182, 190, 198, 206,
            214, 222, 230, 238, 246, 254}, 11, new int[] {70, 78, 86, 94, 102, 110, 118, 126}, 8);

    private static final int[] T_INDEX = {
            4, 10, 7, 6, 4, 4, 7, 4, 4, 11, 7, 6, 4, 4, 7, 4,
            8, 10, 7, 6, 4, 4, 7, 4, 12, 11, 7, 6, 4, 4, 7, 4,
            7, 10, 16, 6, 4, 4, 7, 4, 7, 11, 16, 6, 4, 4, 7, 4,
            7, 10, 13, 6, 19, 19, 15, 4, 7, 11, 13, 6, 4, 4, 7, 4,
            4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
            4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
            4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
            15, 15, 15, 15, 15, 15, 4, 15, 4, 4, 4, 4, 4, 4, 15, 4,
            4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
            4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
            4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
            4, 4, 4, 4, 4, 4, 15, 4, 4, 4, 4, 4, 4, 4, 15, 4,
            5, 10, 10, 10, 10, 11, 7, 11, 5, 10, 10, 7, 10, 17, 7, 11,
            5, 10, 10, 11, 10, 11, 7, 11, 5, 4, 10, 11, 10, 4, 7, 11,
            5, 10, 10, 19, 10, 11, 7, 11, 5, 4, 10, 4, 10, 4, 7, 11,
            5, 10, 10, 4, 10, 11, 7, 11, 5, 6, 10, 4, 10, 4, 7, 11
    };

    private static final int[] T_INDEX_CB = buildIndexCb();

    private static final int[] T_ED = {
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            8, 8, 11, 16, 4, 10, 4, 5, 8, 8, 11, 16, 4, 10, 4, 5,
            8, 8, 11, 16, 4, 10, 4, 5, 8, 8, 11, 16, 4, 10, 4, 5,
            8, 8, 11, 16, 4, 10, 4, 14, 8, 8, 11, 16, 4, 10, 4, 14,
            8, 8, 11, 16, 4, 10, 4, 4, 8, 8, 11, 16, 4, 10, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            12, 12, 12, 12, 4, 4, 4, 4, 12, 12, 12, 12, 4, 4, 4, 4,
            12, 12, 12, 12, 4, 4, 4, 4, 12, 12, 12, 12, 4, 4, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4
    };

    private static final int[] T_EXTRA = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,
            5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            5, 5, 5, 5, 0, 0, 0, 0, 5, 5, 5, 5, 0, 0, 0, 0,
            6, 0, 0, 0, 7, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0,
            6, 0, 0, 0, 7, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0,
            6, 0, 0, 0, 7, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0,
            6, 0, 0, 0, 7, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0
    };

    private static final int[] PARITY = buildParity();

    public int a, f, b, c, d, e, h, l;
    public int a2, f2, b2, c2, d2, e2, h2, l2;
    public int ix, iy, sp, wz;
    public int i, r, im;
    public boolean iff1, iff2, halted;

    private final int clock;
    private MemoryRead read = address -> 0xff;
    private MemoryRead opcodeRead;
    private MemoryWrite write = (address, value) -> {};
    private MemoryRead in = port -> 0xff;
    private MemoryWrite out = (port, value) -> {};
    private CycleHandler cycleHandler;
    private InstructionHook instructionHook;
    private IrqAckHandler irqAck;
    private ReturnHandler returnCb;
    private boolean fetchingOpcode = true;
    private int[] tMain = T_MAIN;
    private int[] tCb = T_CB;
    private int[] tIndex = T_INDEX;
    private int[] tIndexCb = T_INDEX_CB;
    private int[] tEd = T_ED;
    private int[] tExtra = T_EXTRA;
    private int irqCycleAlign;
    private int cycles;
    private int executed;
    private boolean afterEi;
    private IrqLine irqState = IrqLine.CLEAR;
    private IrqLine nmiState = IrqLine.CLEAR;
    private boolean nmiLatched;
    private int irqVector = 0xff;
    private int pc;

    public Z80(int clock) {
        this.clock = clock;
        reset();
    }

    public void setMemoryHandlers(MemoryRead read, MemoryWrite write) {
        this.read = read;
        this.write = write;
    }

    public void setIoHandlers(MemoryRead in, MemoryWrite out) {
        this.in = in;
        this.out = out;
    }

    public void setOpcodeRead(MemoryRead handler) {
        this.opcodeRead = handler;
    }

    public void setCycleHandler(CycleHandler handler) {
        this.cycleHandler = handler;
    }

    public void setInstructionHook(InstructionHook handler) {
        this.instructionHook = handler;
    }

    public void setIrqAckCallback(IrqAckHandler handler) {
        this.irqAck = handler;
    }

    public void setReturnCallback(ReturnHandler handler) {
        this.returnCb = handler;
    }

    public void setTimingTables(int[] main, int[] cb, int[] index, int[] indexCb, int[] ed, int[] extra) {
        if (main != null) {
            tMain = main;
        }
        if (cb != null) {
            tCb = cb;
        }
        if (index != null) {
            tIndex = index;
        }
        if (indexCb != null) {
            tIndexCb = indexCb;
        }
        if (ed != null) {
            tEd = ed;
        }
        if (extra != null) {
            tExtra = extra;
        }
    }

    public void setIrqCycleAlign(int align) {
        irqCycleAlign = align;
    }

    public void reset() {
        a = f = b = c = d = e = h = l = 0;
        a2 = f2 = b2 = c2 = d2 = e2 = h2 = l2 = 0;
        ix = iy = wz = 0;
        sp = 0xffff;
        pc = 0;
        i = r = im = 0;
        iff1 = iff2 = false;
        halted = false;
        afterEi = false;
        irqState = IrqLine.CLEAR;
        nmiState = IrqLine.CLEAR;
        nmiLatched = false;
        irqVector = 0xff;
    }

    public int clock() {
        return clock;
    }

    public int pc() {
        return pc;
    }

    public void setPc(int value) {
        pc = u16(value);
    }

    public void setIrq(IrqLine state) {
        setIrq(state, 0xff);
    }

    public void setIrq(IrqLine state, int vector) {
        irqState = state;
        irqVector = vector & 0xff;
    }

    public void setNmi(IrqLine state) {
        nmiState = state;
        if (state == IrqLine.CLEAR) {
            nmiLatched = false;
        }
    }

    public int run(int limit) {
        executed = 0;
        while (executed < limit) {
            cycles = 0;
            if (!afterEi) {
                if (nmiState != IrqLine.CLEAR && !nmiLatched) {
                    cycles += takeNmi();
                } else if (irqState != IrqLine.CLEAR) {
                    cycles += takeIrq();
                }
            }
            afterEi = false;

            if (halted) {
                cycles += 4;
                refreshR();
                executed += cycles;
                if (cycleHandler != null) {
                    cycleHandler.onCycles(cycles);
                }
                continue;
            }

            if (instructionHook != null) {
                instructionHook.beforeOpcode(pc);
            }
            fetchingOpcode = true;
            int opcode = fetch();
            refreshR();
            cycles += tMain[opcode];
            execMain(opcode);
            executed += cycles;
            if (cycleHandler != null) {
                cycleHandler.onCycles(cycles);
            }
        }
        return executed;
    }

    private void execMain(int opcode) {
        switch (opcode) {
            case 0x00:
                break;
            case 0x01:
                setBc(fetch16());
                break;
            case 0x02:
                wr(bc(), a);
                wz = ((bc() + 1) & 0xff) | (a << 8);
                break;
            case 0x03:
                setBc(bc() + 1);
                break;
            case 0x04:
                b = inc8(b);
                break;
            case 0x05:
                b = dec8(b);
                break;
            case 0x06:
                b = fetch();
                break;
            case 0x07:
                f = (f & (SF | ZF | PF)) | (a >> 7);
                a = u8((a << 1) | (a >> 7));
                f |= a & (YF | XF);
                break;
            case 0x08: {
                int ta = a;
                a = a2;
                a2 = ta;
                int tf = f;
                f = f2;
                f2 = tf;
                break;
            }
            case 0x09:
                setHl(add16(hl(), bc()));
                break;
            case 0x0a:
                a = rd(bc());
                wz = u16(bc() + 1);
                break;
            case 0x0b:
                setBc(bc() - 1);
                break;
            case 0x0c:
                c = inc8(c);
                break;
            case 0x0d:
                c = dec8(c);
                break;
            case 0x0e:
                c = fetch();
                break;
            case 0x0f:
                f = (f & (SF | ZF | PF)) | (a & CF);
                a = u8((a >> 1) | (a << 7));
                f |= a & (YF | XF);
                break;
            case 0x10: {
                int offset = s8(fetch());
                b = u8(b - 1);
                if (b != 0) {
                    pc = u16(pc + offset);
                    wz = pc;
                    cycles += tExtra[opcode];
                }
                break;
            }
            case 0x11:
                setDe(fetch16());
                break;
            case 0x12:
                wr(de(), a);
                wz = ((de() + 1) & 0xff) | (a << 8);
                break;
            case 0x13:
                setDe(de() + 1);
                break;
            case 0x14:
                d = inc8(d);
                break;
            case 0x15:
                d = dec8(d);
                break;
            case 0x16:
                d = fetch();
                break;
            case 0x17: {
                int carry = a >> 7;
                a = u8((a << 1) | (f & CF));
                f = (f & (SF | ZF | PF)) | carry | (a & (YF | XF));
                break;
            }
            case 0x18: {
                int offset = s8(fetch());
                pc = u16(pc + offset);
                wz = pc;
                break;
            }
            case 0x19:
                setHl(add16(hl(), de()));
                break;
            case 0x1a:
                a = rd(de());
                wz = u16(de() + 1);
                break;
            case 0x1b:
                setDe(de() - 1);
                break;
            case 0x1c:
                e = inc8(e);
                break;
            case 0x1d:
                e = dec8(e);
                break;
            case 0x1e:
                e = fetch();
                break;
            case 0x1f: {
                int carry = a & CF;
                a = u8((a >> 1) | ((f & CF) << 7));
                f = (f & (SF | ZF | PF)) | carry | (a & (YF | XF));
                break;
            }
            case 0x20:
            case 0x28:
            case 0x30:
            case 0x38: {
                int offset = s8(fetch());
                boolean taken = switch (opcode) {
                    case 0x20 -> (f & ZF) == 0;
                    case 0x28 -> (f & ZF) != 0;
                    case 0x30 -> (f & CF) == 0;
                    default -> (f & CF) != 0;
                };
                if (taken) {
                    pc = u16(pc + offset);
                    wz = pc;
                    cycles += tExtra[opcode];
                }
                break;
            }
            case 0x21:
                setHl(fetch16());
                break;
            case 0x22: {
                int addr = fetch16();
                wr(addr, l);
                wr(addr + 1, h);
                wz = u16(addr + 1);
                break;
            }
            case 0x23:
                setHl(hl() + 1);
                break;
            case 0x24:
                h = inc8(h);
                break;
            case 0x25:
                h = dec8(h);
                break;
            case 0x26:
                h = fetch();
                break;
            case 0x27:
                daa();
                break;
            case 0x29:
                setHl(add16(hl(), hl()));
                break;
            case 0x2a: {
                int addr = fetch16();
                l = rd(addr);
                h = rd(addr + 1);
                wz = u16(addr + 1);
                break;
            }
            case 0x2b:
                setHl(hl() - 1);
                break;
            case 0x2c:
                l = inc8(l);
                break;
            case 0x2d:
                l = dec8(l);
                break;
            case 0x2e:
                l = fetch();
                break;
            case 0x2f:
                a = u8(~a);
                f = (f & (SF | ZF | PF | CF)) | HF | NF | (a & (YF | XF));
                break;
            case 0x31:
                sp = fetch16();
                break;
            case 0x32: {
                int addr = fetch16();
                wr(addr, a);
                wz = ((addr + 1) & 0xff) | (a << 8);
                break;
            }
            case 0x33:
                sp = u16(sp + 1);
                break;
            case 0x34:
                wr(hl(), inc8(rd(hl())));
                break;
            case 0x35:
                wr(hl(), dec8(rd(hl())));
                break;
            case 0x36:
                wr(hl(), fetch());
                break;
            case 0x37:
                f = (f & (SF | ZF | PF)) | CF | (a & (YF | XF));
                break;
            case 0x39:
                setHl(add16(hl(), sp));
                break;
            case 0x3a: {
                int addr = fetch16();
                a = rd(addr);
                wz = u16(addr + 1);
                break;
            }
            case 0x3b:
                sp = u16(sp - 1);
                break;
            case 0x3c:
                a = inc8(a);
                break;
            case 0x3d:
                a = dec8(a);
                break;
            case 0x3e:
                a = fetch();
                break;
            case 0x3f:
                f = (f & (SF | ZF | PF)) | ((f & CF) != 0 ? HF : CF) | (a & (YF | XF));
                break;
            case 0x76:
                halted = true;
                break;
            default:
                if (opcode >= 0x40 && opcode <= 0x7f) {
                    int dst = (opcode >> 3) & 7;
                    int src = opcode & 7;
                    int value = src == 6 ? rd(hl()) : r8(src);
                    if (dst == 6) {
                        wr(hl(), value);
                    } else {
                        w8(dst, value);
                    }
                } else if (opcode >= 0x80 && opcode <= 0xbf) {
                    int src = opcode & 7;
                    int value = src == 6 ? rd(hl()) : r8(src);
                    aluA((opcode >> 3) & 7, value);
                } else {
                    execControl(opcode);
                }
                break;
        }
    }

    private void execControl(int opcode) {
        switch (opcode) {
            case 0xc0:
            case 0xc8:
            case 0xd0:
            case 0xd8:
            case 0xe0:
            case 0xe8:
            case 0xf0:
            case 0xf8:
                if (cc((opcode >> 3) & 7)) {
                    pc = pop();
                    wz = pc;
                    cycles += tExtra[opcode];
                }
                break;
            case 0xc1:
                setBc(pop());
                break;
            case 0xd1:
                setDe(pop());
                break;
            case 0xe1:
                setHl(pop());
                break;
            case 0xf1:
                setAf(pop());
                break;
            case 0xc5:
                push(bc());
                break;
            case 0xd5:
                push(de());
                break;
            case 0xe5:
                push(hl());
                break;
            case 0xf5:
                push(af());
                break;
            case 0xc2:
            case 0xca:
            case 0xd2:
            case 0xda:
            case 0xe2:
            case 0xea:
            case 0xf2:
            case 0xfa: {
                int addr = fetch16();
                wz = addr;
                if (cc((opcode >> 3) & 7)) {
                    pc = addr;
                }
                break;
            }
            case 0xc3:
                pc = fetch16();
                wz = pc;
                break;
            case 0xc4:
            case 0xcc:
            case 0xd4:
            case 0xdc:
            case 0xe4:
            case 0xec:
            case 0xf4:
            case 0xfc: {
                int addr = fetch16();
                wz = addr;
                if (cc((opcode >> 3) & 7)) {
                    push(pc);
                    pc = addr;
                    cycles += tExtra[opcode];
                }
                break;
            }
            case 0xc6:
                addA(fetch());
                break;
            case 0xce:
                adcA(fetch());
                break;
            case 0xd6:
                subA(fetch());
                break;
            case 0xde:
                sbcA(fetch());
                break;
            case 0xe6:
                andA(fetch());
                break;
            case 0xee:
                xorA(fetch());
                break;
            case 0xf6:
                orA(fetch());
                break;
            case 0xfe:
                cpA(fetch());
                break;
            case 0xc7:
            case 0xcf:
            case 0xd7:
            case 0xdf:
            case 0xe7:
            case 0xef:
            case 0xf7:
            case 0xff:
                push(pc);
                pc = opcode & 0x38;
                wz = pc;
                break;
            case 0xc9:
                pc = pop();
                wz = pc;
                break;
            case 0xcb:
                execCb();
                break;
            case 0xcd: {
                int addr = fetch16();
                push(pc);
                pc = addr;
                wz = pc;
                break;
            }
            case 0xd3: {
                int port = fetch();
                out.write((a << 8) | port, a);
                wz = (a << 8) | ((port + 1) & 0xff);
                break;
            }
            case 0xd9: {
                int tb = b;
                b = b2;
                b2 = tb;
                int tc = c;
                c = c2;
                c2 = tc;
                int td = d;
                d = d2;
                d2 = td;
                int te = e;
                e = e2;
                e2 = te;
                int th = h;
                h = h2;
                h2 = th;
                int tl = l;
                l = l2;
                l2 = tl;
                break;
            }
            case 0xdb: {
                int port = fetch();
                int addr = (a << 8) | port;
                a = in.read(addr) & 0xff;
                wz = u16(addr + 1);
                break;
            }
            case 0xdd:
                execIndex(true);
                break;
            case 0xe3: {
                int value = rd(sp) | (rd(sp + 1) << 8);
                wr(sp, l);
                wr(sp + 1, h);
                setHl(value);
                wz = value;
                break;
            }
            case 0xe9:
                pc = hl();
                break;
            case 0xeb: {
                int td = d;
                d = h;
                h = td;
                int te = e;
                e = l;
                l = te;
                break;
            }
            case 0xed:
                execEd();
                break;
            case 0xf3:
                iff1 = iff2 = false;
                break;
            case 0xf9:
                sp = hl();
                break;
            case 0xfb:
                iff1 = iff2 = true;
                afterEi = true;
                break;
            case 0xfd:
                execIndex(false);
                break;
            default:
                break;
        }
    }

    private void execCb() {
        fetchingOpcode = true;
        int opcode = fetch();
        refreshR();
        cycles += tCb[opcode];
        int index = opcode & 7;
        int value = index == 6 ? rd(hl()) : r8(index);
        int op = opcode >> 6;
        int bitIndex = (opcode >> 3) & 7;
        if (op == 1) {
            bit(bitIndex, value, index == 6 ? (wz >> 8) : value);
            return;
        }
        int result = cbOp(op, bitIndex, value);
        if (index == 6) {
            wr(hl(), result);
        } else {
            w8(index, result);
        }
    }

    private void execEd() {
        fetchingOpcode = true;
        int opcode = fetch();
        refreshR();
        cycles += tEd[opcode];
        switch (opcode) {
            case 0x40:
            case 0x48:
            case 0x50:
            case 0x58:
            case 0x60:
            case 0x68:
            case 0x70:
            case 0x78: {
                int value = in.read(bc()) & 0xff;
                wz = u16(bc() + 1);
                f = (f & CF) | sz53p(value);
                int dst = (opcode >> 3) & 7;
                if (dst != 6) {
                    w8(dst, value);
                }
                break;
            }
            case 0x41:
            case 0x49:
            case 0x51:
            case 0x59:
            case 0x61:
            case 0x69:
            case 0x71:
            case 0x79: {
                int src = (opcode >> 3) & 7;
                int value = src == 6 ? 0 : r8(src);
                out.write(bc(), value);
                wz = u16(bc() + 1);
                break;
            }
            case 0x42:
            case 0x52:
            case 0x62:
            case 0x72:
                sbcHl(pair((opcode >> 4) & 3));
                break;
            case 0x4a:
            case 0x5a:
            case 0x6a:
            case 0x7a:
                adcHl(pair((opcode >> 4) & 3));
                break;
            case 0x43:
            case 0x53:
            case 0x63:
            case 0x73: {
                int addr = fetch16();
                int value = pair((opcode >> 4) & 3);
                wr(addr, value);
                wr(addr + 1, value >> 8);
                wz = u16(addr + 1);
                break;
            }
            case 0x4b:
            case 0x5b:
            case 0x6b:
            case 0x7b: {
                int addr = fetch16();
                int value = rd(addr) | (rd(addr + 1) << 8);
                wz = u16(addr + 1);
                setPair((opcode >> 4) & 3, value);
                break;
            }
            case 0x44:
            case 0x4c:
            case 0x54:
            case 0x5c:
            case 0x64:
            case 0x6c:
            case 0x74:
            case 0x7c: {
                int value = a;
                a = 0;
                subA(value);
                break;
            }
            case 0x45:
            case 0x55:
            case 0x65:
            case 0x75:
            case 0x5d:
            case 0x6d:
            case 0x7d:
            case 0x4d:
                if (returnCb != null) {
                    returnCb.onReturn(opcode == 0x4d);
                }
                iff1 = iff2;
                pc = pop();
                wz = pc;
                break;
            case 0x46:
            case 0x4e:
            case 0x66:
            case 0x6e:
                im = 0;
                break;
            case 0x56:
            case 0x76:
                im = 1;
                break;
            case 0x5e:
            case 0x7e:
                im = 2;
                break;
            case 0x47:
                i = a;
                break;
            case 0x4f:
                r = a;
                break;
            case 0x57:
                a = i;
                f = (f & CF) | sz53(a) | (iff2 ? PF : 0);
                break;
            case 0x5f:
                a = r;
                f = (f & CF) | sz53(a) | (iff2 ? PF : 0);
                break;
            case 0x67:
                rrd();
                break;
            case 0x6f:
                rld();
                break;
            case 0xa0:
                blockLd(1, false);
                break;
            case 0xa8:
                blockLd(-1, false);
                break;
            case 0xb0:
                blockLd(1, true);
                break;
            case 0xb8:
                blockLd(-1, true);
                break;
            case 0xa1:
                blockCp(1, false);
                break;
            case 0xa9:
                blockCp(-1, false);
                break;
            case 0xb1:
                blockCp(1, true);
                break;
            case 0xb9:
                blockCp(-1, true);
                break;
            case 0xa2:
                blockIn(1, false);
                break;
            case 0xaa:
                blockIn(-1, false);
                break;
            case 0xb2:
                blockIn(1, true);
                break;
            case 0xba:
                blockIn(-1, true);
                break;
            case 0xa3:
                blockOut(1, false);
                break;
            case 0xab:
                blockOut(-1, false);
                break;
            case 0xb3:
                blockOut(1, true);
                break;
            case 0xbb:
                blockOut(-1, true);
                break;
            default:
                break;
        }
    }

    private void execIndex(boolean useIx) {
        fetchingOpcode = true;
        int opcode = fetch();
        refreshR();
        cycles += tIndex[opcode];
        int index = useIx ? ix : iy;
        int indexHigh = (index >> 8) & 0xff;
        int indexLow = index & 0xff;
        switch (opcode) {
            case 0x09:
                setIndex(useIx, add16(index, bc()));
                break;
            case 0x19:
                setIndex(useIx, add16(index, de()));
                break;
            case 0x21:
                setIndex(useIx, fetch16());
                break;
            case 0x22: {
                int addr = fetch16();
                wr(addr, indexLow);
                wr(addr + 1, indexHigh);
                wz = u16(addr + 1);
                break;
            }
            case 0x23:
                setIndex(useIx, index + 1);
                break;
            case 0x24:
                setIndex(useIx, (inc8(indexHigh) << 8) | indexLow);
                break;
            case 0x25:
                setIndex(useIx, (dec8(indexHigh) << 8) | indexLow);
                break;
            case 0x26:
                setIndex(useIx, (fetch() << 8) | indexLow);
                break;
            case 0x29:
                setIndex(useIx, add16(index, index));
                break;
            case 0x2a: {
                int addr = fetch16();
                setIndex(useIx, rd(addr) | (rd(addr + 1) << 8));
                wz = u16(addr + 1);
                break;
            }
            case 0x2b:
                setIndex(useIx, index - 1);
                break;
            case 0x2c:
                setIndex(useIx, (indexHigh << 8) | inc8(indexLow));
                break;
            case 0x2d:
                setIndex(useIx, (indexHigh << 8) | dec8(indexLow));
                break;
            case 0x2e:
                setIndex(useIx, (indexHigh << 8) | fetch());
                break;
            case 0x34: {
                int addr = indexedEa(index);
                wr(addr, inc8(rd(addr)));
                break;
            }
            case 0x35: {
                int addr = indexedEa(index);
                wr(addr, dec8(rd(addr)));
                break;
            }
            case 0x36: {
                int addr = indexedEa(index);
                wr(addr, fetch());
                break;
            }
            case 0x39:
                setIndex(useIx, add16(index, sp));
                break;
            case 0xcb:
                execIndexCb(useIx);
                break;
            case 0xe1:
                setIndex(useIx, pop());
                break;
            case 0xe3: {
                int value = rd(sp) | (rd(sp + 1) << 8);
                wr(sp, indexLow);
                wr(sp + 1, indexHigh);
                setIndex(useIx, value);
                wz = value;
                break;
            }
            case 0xe5:
                push(index);
                break;
            case 0xe9:
                pc = index;
                break;
            case 0xf9:
                sp = index;
                break;
            default:
                if (opcode >= 0x40 && opcode <= 0x7f && opcode != 0x76) {
                    int dst = (opcode >> 3) & 7;
                    int src = opcode & 7;
                    if (src == 6) {
                        w8(dst, rd(indexedEa(index)));
                    } else if (dst == 6) {
                        wr(indexedEa(index), r8(src));
                    } else {
                        int value = src == 4 ? indexHigh : src == 5 ? indexLow : r8(src);
                        if (dst == 4) {
                            setIndex(useIx, (value << 8) | (getIndex(useIx) & 0xff));
                        } else if (dst == 5) {
                            setIndex(useIx, (getIndex(useIx) & 0xff00) | value);
                        } else {
                            w8(dst, value);
                        }
                    }
                } else if (opcode >= 0x80 && opcode <= 0xbf) {
                    int src = opcode & 7;
                    int value;
                    if (src == 6) {
                        value = rd(indexedEa(index));
                    } else if (src == 4) {
                        value = indexHigh;
                    } else if (src == 5) {
                        value = indexLow;
                    } else {
                        value = r8(src);
                    }
                    aluA((opcode >> 3) & 7, value);
                } else {
                    pc = u16(pc - 1);
                    r = ((r - 1) & 0x7f) | (r & 0x80);
                    afterEi = true;
                }
                break;
        }
    }

    private void execIndexCb(boolean useIx) {
        int offset = s8(fetch());
        int opcode = fetch();
        cycles += tIndexCb[opcode];
        int addr = u16(getIndex(useIx) + offset);
        wz = addr;
        int value = rd(addr);
        int op = opcode >> 6;
        int bitIndex = (opcode >> 3) & 7;
        if (op == 1) {
            bit(bitIndex, value, addr >> 8);
            return;
        }
        int result = cbOp(op, bitIndex, value);
        wr(addr, result);
        int index = opcode & 7;
        if (index != 6) {
            w8(index, result);
        }
    }

    private int takeNmi() {
        if (nmiLatched) {
            return 0;
        }
        halted = false;
        iff2 = iff1;
        iff1 = false;
        push(pc);
        pc = 0x0066;
        wz = pc;
        refreshR();
        if (nmiState == IrqLine.PULSE || nmiState == IrqLine.HOLD) {
            nmiState = IrqLine.CLEAR;
        } else {
            nmiLatched = true;
        }
        return 11;
    }

    private int takeIrq() {
        if (!iff1) {
            return 0;
        }
        if (irqAck != null) {
            irqAck.onIrqAck();
        }
        halted = false;
        iff1 = iff2 = false;
        if (irqState == IrqLine.HOLD || irqState == IrqLine.PULSE) {
            irqState = IrqLine.CLEAR;
        }
        refreshR();
        int tstates = 13;
        switch (im) {
            case 0:
                push(pc);
                pc = irqVector & 0x38;
                wz = pc;
                tstates = 13;
                break;
            case 1:
                push(pc);
                pc = 0x0038;
                wz = pc;
                tstates = 13;
                break;
            default: {
                int addr = (i << 8) | irqVector;
                push(pc);
                pc = rd(addr) | (rd(addr + 1) << 8);
                wz = pc;
                tstates = 19;
                break;
            }
        }
        if (irqCycleAlign > 1) {
            int rem = tstates % irqCycleAlign;
            if (rem != 0) {
                tstates += irqCycleAlign - rem;
            }
        }
        return tstates;
    }

    private int indexedEa(int index) {
        int offset = s8(fetch());
        wz = u16(index + offset);
        return wz;
    }

    private int getIndex(boolean useIx) {
        return useIx ? ix : iy;
    }

    private void setIndex(boolean useIx, int value) {
        if (useIx) {
            ix = u16(value);
        } else {
            iy = u16(value);
        }
    }

    private int rd(int addr) {
        return read.read(u16(addr)) & 0xff;
    }

    private void wr(int addr, int value) {
        write.write(u16(addr), value & 0xff);
    }

    private int fetch() {
        int value = (fetchingOpcode && opcodeRead != null) ? opcodeRead.read(pc) : read.read(pc);
        fetchingOpcode = false;
        pc = u16(pc + 1);
        return value & 0xff;
    }

    private int fetch16() {
        int value = rd(pc) | (rd(pc + 1) << 8);
        pc = u16(pc + 2);
        return value;
    }

    private void push(int value) {
        value = u16(value);
        sp = u16(sp - 1);
        wr(sp, value >> 8);
        sp = u16(sp - 1);
        wr(sp, value);
    }

    private int pop() {
        int value = rd(sp);
        sp = u16(sp + 1);
        value |= rd(sp) << 8;
        sp = u16(sp + 1);
        return value;
    }

    private void refreshR() {
        r = ((r + 1) & 0x7f) | (r & 0x80);
    }

    private int bc() {
        return (b << 8) | c;
    }

    private int de() {
        return (d << 8) | e;
    }

    private int hl() {
        return (h << 8) | l;
    }

    private int af() {
        return (a << 8) | f;
    }

    private void setBc(int v) {
        v = u16(v);
        b = v >> 8;
        c = v & 0xff;
    }

    private void setDe(int v) {
        v = u16(v);
        d = v >> 8;
        e = v & 0xff;
    }

    private void setHl(int v) {
        v = u16(v);
        h = v >> 8;
        l = v & 0xff;
    }

    private void setAf(int v) {
        v = u16(v);
        a = v >> 8;
        f = v & 0xff;
    }

    private int pair(int which) {
        return switch (which) {
            case 0 -> bc();
            case 1 -> de();
            case 2 -> hl();
            default -> sp;
        };
    }

    private void setPair(int which, int value) {
        switch (which) {
            case 0 -> setBc(value);
            case 1 -> setDe(value);
            case 2 -> setHl(value);
            default -> sp = u16(value);
        }
    }

    private int r8(int index) {
        return switch (index) {
            case 0 -> b;
            case 1 -> c;
            case 2 -> d;
            case 3 -> e;
            case 4 -> h;
            case 5 -> l;
            case 7 -> a;
            default -> 0;
        };
    }

    private void w8(int index, int value) {
        value &= 0xff;
        switch (index) {
            case 0 -> b = value;
            case 1 -> c = value;
            case 2 -> d = value;
            case 3 -> e = value;
            case 4 -> h = value;
            case 5 -> l = value;
            case 7 -> a = value;
            default -> {
            }
        }
    }

    private boolean cc(int cond) {
        return switch (cond) {
            case 0 -> (f & ZF) == 0;
            case 1 -> (f & ZF) != 0;
            case 2 -> (f & CF) == 0;
            case 3 -> (f & CF) != 0;
            case 4 -> (f & PF) == 0;
            case 5 -> (f & PF) != 0;
            case 6 -> (f & SF) == 0;
            default -> (f & SF) != 0;
        };
    }

    private void aluA(int op, int value) {
        switch (op) {
            case 0 -> addA(value);
            case 1 -> adcA(value);
            case 2 -> subA(value);
            case 3 -> sbcA(value);
            case 4 -> andA(value);
            case 5 -> xorA(value);
            case 6 -> orA(value);
            default -> cpA(value);
        }
    }

    private int cbOp(int op, int bitIndex, int value) {
        if (op == 0) {
            return switch (bitIndex) {
                case 0 -> rlc(value);
                case 1 -> rrc(value);
                case 2 -> rl(value);
                case 3 -> rr(value);
                case 4 -> sla(value);
                case 5 -> sra(value);
                case 6 -> sll(value);
                default -> srl(value);
            };
        }
        if (op == 2) {
            return value & ~(1 << bitIndex);
        }
        return value | (1 << bitIndex);
    }

    private void addA(int value) {
        int result = a + value;
        f = sz53(result) | (result > 0xff ? CF : 0)
                | (((a ^ value ^ result) & HF) != 0 ? HF : 0)
                | ((((a ^ value ^ 0x80) & (a ^ result)) & 0x80) != 0 ? PF : 0);
        a = u8(result);
    }

    private void adcA(int value) {
        int carry = f & CF;
        int result = a + value + carry;
        f = sz53(result) | (result > 0xff ? CF : 0)
                | (((a ^ value ^ result) & HF) != 0 ? HF : 0)
                | ((((a ^ value ^ 0x80) & (a ^ result)) & 0x80) != 0 ? PF : 0);
        a = u8(result);
    }

    private void subA(int value) {
        int result = a - value;
        f = sz53(result) | NF | (result < 0 ? CF : 0)
                | (((a ^ value ^ result) & HF) != 0 ? HF : 0)
                | ((((a ^ value) & (a ^ result)) & 0x80) != 0 ? PF : 0);
        a = u8(result);
    }

    private void sbcA(int value) {
        int carry = f & CF;
        int result = a - value - carry;
        f = sz53(result) | NF | (result < 0 ? CF : 0)
                | (((a ^ value ^ result) & HF) != 0 ? HF : 0)
                | ((((a ^ value) & (a ^ result)) & 0x80) != 0 ? PF : 0);
        a = u8(result);
    }

    private void andA(int value) {
        a &= value;
        f = sz53p(a) | HF;
    }

    private void xorA(int value) {
        a ^= value;
        f = sz53p(a);
    }

    private void orA(int value) {
        a |= value;
        f = sz53p(a);
    }

    private void cpA(int value) {
        int result = a - value;
        f = (u8(result) & SF) | (u8(result) == 0 ? ZF : 0) | NF
                | (value & (YF | XF)) | (result < 0 ? CF : 0)
                | (((a ^ value ^ result) & HF) != 0 ? HF : 0)
                | ((((a ^ value) & (a ^ result)) & 0x80) != 0 ? PF : 0);
    }

    private int inc8(int value) {
        int result = u8(value + 1);
        f = (f & CF) | sz53(result) | (result == 0x80 ? PF : 0) | ((result & 0x0f) == 0 ? HF : 0);
        return result;
    }

    private int dec8(int value) {
        int result = u8(value - 1);
        f = (f & CF) | NF | sz53(result) | (result == 0x7f ? PF : 0)
                | ((result & 0x0f) == 0x0f ? HF : 0);
        return result;
    }

    private int add16(int x, int y) {
        int result = x + y;
        wz = u16(x + 1);
        f = (f & (SF | ZF | PF)) | (((x ^ y ^ result) >> 8) & HF)
                | (result > 0xffff ? CF : 0) | ((result >> 8) & (YF | XF));
        return u16(result);
    }

    private void adcHl(int value) {
        int x = hl();
        int result = x + value + (f & CF);
        wz = u16(x + 1);
        int res16 = u16(result);
        f = ((res16 >> 8) & (SF | YF | XF)) | (res16 == 0 ? ZF : 0)
                | (result > 0xffff ? CF : 0) | (((x ^ value ^ res16) >> 8) & HF)
                | ((((x ^ value ^ 0x8000) & (x ^ res16)) & 0x8000) != 0 ? PF : 0);
        setHl(res16);
    }

    private void sbcHl(int value) {
        int x = hl();
        int result = x - value - (f & CF);
        wz = u16(x + 1);
        int res16 = u16(result);
        f = ((res16 >> 8) & (SF | YF | XF)) | (res16 == 0 ? ZF : 0) | NF
                | (result < 0 ? CF : 0) | (((x ^ value ^ res16) >> 8) & HF)
                | ((((x ^ value) & (x ^ res16)) & 0x8000) != 0 ? PF : 0);
        setHl(res16);
    }

    private void daa() {
        int correction = 0;
        int carry = f & CF;
        if ((f & HF) != 0 || (a & 0x0f) > 9) {
            correction |= 0x06;
        }
        if (carry != 0 || a > 0x99) {
            correction |= 0x60;
            carry = CF;
        }
        int before = a;
        if ((f & NF) != 0) {
            a = u8(a - correction);
        } else {
            a = u8(a + correction);
        }
        f = sz53p(a) | carry | (f & NF) | (((before ^ a) & HF) != 0 ? HF : 0);
    }

    private void rrd() {
        int value = rd(hl());
        wr(hl(), (value >> 4) | (a << 4));
        a = (a & 0xf0) | (value & 0x0f);
        wz = u16(hl() + 1);
        f = (f & CF) | sz53p(a);
    }

    private void rld() {
        int value = rd(hl());
        wr(hl(), (value << 4) | (a & 0x0f));
        a = (a & 0xf0) | (value >> 4);
        wz = u16(hl() + 1);
        f = (f & CF) | sz53p(a);
    }

    private int rlc(int value) {
        int carry = value >> 7;
        int result = u8((value << 1) | carry);
        f = sz53p(result) | carry;
        return result;
    }

    private int rrc(int value) {
        int carry = value & 1;
        int result = u8((value >> 1) | (carry << 7));
        f = sz53p(result) | carry;
        return result;
    }

    private int rl(int value) {
        int carry = value >> 7;
        int result = u8((value << 1) | (f & CF));
        f = sz53p(result) | carry;
        return result;
    }

    private int rr(int value) {
        int carry = value & 1;
        int result = u8((value >> 1) | ((f & CF) << 7));
        f = sz53p(result) | carry;
        return result;
    }

    private int sla(int value) {
        int carry = value >> 7;
        int result = u8(value << 1);
        f = sz53p(result) | carry;
        return result;
    }

    private int sra(int value) {
        int carry = value & 1;
        int result = u8((value >> 1) | (value & 0x80));
        f = sz53p(result) | carry;
        return result;
    }

    private int sll(int value) {
        int carry = value >> 7;
        int result = u8((value << 1) | 1);
        f = sz53p(result) | carry;
        return result;
    }

    private int srl(int value) {
        int carry = value & 1;
        int result = value >> 1;
        f = sz53p(result) | carry;
        return result;
    }

    private void bit(int index, int value, int xySource) {
        int masked = value & (1 << index);
        f = (f & CF) | HF | (masked != 0 ? (masked & SF) : (ZF | PF)) | (xySource & (YF | XF));
    }

    private void blockLd(int delta, boolean repeat) {
        int value = rd(hl());
        wr(de(), value);
        setHl(hl() + delta);
        setDe(de() + delta);
        setBc(bc() - 1);
        int n = u8(a + value);
        f = (f & (SF | ZF | CF)) | ((n & 0x02) != 0 ? YF : 0) | ((n & 0x08) != 0 ? XF : 0)
                | (bc() != 0 ? PF : 0);
        if (repeat && bc() != 0) {
            pc = u16(pc - 2);
            wz = u16(pc + 1);
            cycles += tExtra[delta > 0 ? 0xb0 : 0xb8];
        }
    }

    private void blockCp(int delta, boolean repeat) {
        int value = rd(hl());
        int result = u8(a - value);
        int half = ((a ^ value ^ result) & HF) != 0 ? HF : 0;
        setHl(hl() + delta);
        setBc(bc() - 1);
        int n = u8(result - (half != 0 ? 1 : 0));
        f = (f & CF) | NF | (result & SF) | (result == 0 ? ZF : 0) | half
                | ((n & 0x02) != 0 ? YF : 0) | ((n & 0x08) != 0 ? XF : 0) | (bc() != 0 ? PF : 0);
        wz = u16(wz + delta);
        if (repeat && bc() != 0 && result != 0) {
            pc = u16(pc - 2);
            wz = u16(pc + 1);
            cycles += tExtra[delta > 0 ? 0xb1 : 0xb9];
        }
    }

    private void blockIn(int delta, boolean repeat) {
        int value = in.read(bc()) & 0xff;
        wr(hl(), value);
        wz = u16(bc() + delta);
        b = u8(b - 1);
        setHl(hl() + delta);
        int sum = value + ((c + delta) & 0xff);
        f = sz53(b) | ((value & 0x80) != 0 ? NF : 0) | (sum > 0xff ? (HF | CF) : 0)
                | PARITY[(sum & 7) ^ b];
        if (repeat && b != 0) {
            pc = u16(pc - 2);
            cycles += tExtra[delta > 0 ? 0xb2 : 0xba];
        }
    }

    private void blockOut(int delta, boolean repeat) {
        int value = rd(hl());
        b = u8(b - 1);
        wz = u16(bc() + delta);
        out.write(bc(), value);
        setHl(hl() + delta);
        int sum = value + l;
        f = sz53(b) | ((value & 0x80) != 0 ? NF : 0) | (sum > 0xff ? (HF | CF) : 0)
                | PARITY[(sum & 7) ^ b];
        if (repeat && b != 0) {
            pc = u16(pc - 2);
            cycles += tExtra[delta > 0 ? 0xb3 : 0xbb];
        }
    }

    private static int sz53(int value) {
        value &= 0xff;
        return (value & (SF | YF | XF)) | (value == 0 ? ZF : 0);
    }

    private static int sz53p(int value) {
        value &= 0xff;
        return sz53(value) | PARITY[value];
    }

    private static int u8(int value) {
        return value & 0xff;
    }

    private static int u16(int value) {
        return value & 0xffff;
    }

    private static int s8(int value) {
        value &= 0xff;
        return value >= 128 ? value - 256 : value;
    }

    private static int[] buildParity() {
        int[] table = new int[256];
        for (int v = 0; v < 256; v++) {
            int bits = 0;
            for (int b = 0; b < 8; b++) {
                bits += (v >> b) & 1;
            }
            table[v] = (bits & 1) != 0 ? 0 : PF;
        }
        return table;
    }

    private static int[] buildFilled(int fill, int[] elevenIdx, int eleven, int[] eightIdx, int eight) {
        int[] table = new int[256];
        java.util.Arrays.fill(table, fill);
        for (int i : elevenIdx) {
            table[i] = eleven;
        }
        for (int i : eightIdx) {
            table[i] = eight;
        }
        return table;
    }

    private static int[] buildIndexCb() {
        int[] table = new int[256];
        java.util.Arrays.fill(table, 12);
        for (int i = 0x40; i < 0x80; i++) {
            table[i] = 9;
        }
        return table;
    }
}
