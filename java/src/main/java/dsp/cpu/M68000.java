package dsp.cpu;

/**
 * Motorola 68000/68010/68020 CPU core, ported from dsp-cpp {@code cpu/m68000.cpp}.
 */
public final class M68000 {
    public enum Type { M68000, M68010, M68020 }

    @FunctionalInterface
    public interface ResetInstructionHandler {
        void onResetInstruction();
    }

    @FunctionalInterface
    public interface ExceptionHandler {
        void onException(int vector, int pc);
    }

    @FunctionalInterface
    public interface AlineHandler {
        boolean onAline(int opcode, int pc);
    }

    @FunctionalInterface
    public interface EmulOpHandler {
        boolean onEmulOp(int opcode);
    }

    @FunctionalInterface
    public interface CmpildHandler {
        void onCmpild(int reg, int data);
    }

    @FunctionalInterface
    public interface RteHandler {
        void onRte();
    }

    @FunctionalInterface
    public interface IrqTakenHandler {
        void onIrqTaken(int level);
    }

    @FunctionalInterface
    public interface VectorAckHandler {
        int acknowledge(int level);
    }

    public static final class Reg32 {
        public int l;

        public int l0() {
            return l & 0xff;
        }

        public void setL0(int v) {
            l = (l & 0xffffff00) | (v & 0xff);
        }

        public int wl() {
            return l & 0xffff;
        }

        public void setWl(int v) {
            l = (l & 0xffff0000) | (v & 0xffff);
        }

        public int wh() {
            return (l >>> 16) & 0xffff;
        }

        public void setWh(int v) {
            l = (l & 0x0000ffff) | (v << 16);
        }
    }

    public static final class Flags {
        public boolean t, s, x, n, z, v, c;
        public int im;
    }

    public final Reg32[] d = new Reg32[8];
    public final Reg32[] a = new Reg32[8];
    public final Flags cc = new Flags();
    public final Reg32 pc_ = new Reg32();
    public final Reg32 ppc_ = new Reg32();

    private static final int ADDRESS_MASK = 0xfffffe;
    private int addressMask = ADDRESS_MASK;
    private int vbr;
    private ExceptionHandler exceptionHandler;
    private InstructionHook instructionHook;
    private AlineHandler alineHandler;
    private EmulOpHandler emulOpHandler;
    private CmpildHandler cmpildHandler;
    private RteHandler rteHandler;
    private IrqTakenHandler irqTakenHandler;

    private final int clock;
    private final Type type;
    private boolean opcode = true;
    private boolean prefetch;
    private int exceptions;
    private int pqAddr;
    private final int[] pqVal = new int[2];
    private int pqCount;
    private int ea;
    private final Reg32 otherSp_ = new Reg32();
    private boolean halted;
    private boolean stopped;
    private final IrqLine[] irq = new IrqLine[8];
    private IrqLine resetRequest = IrqLine.CLEAR;
    private IrqLine haltRequest = IrqLine.CLEAR;
    private int cycles;

    private MemoryRead read = address -> 0;
    private MemoryWrite write = (address, value) -> { };
    private MemoryRead readByte;
    private MemoryWrite writeByte;
    private CycleHandler cycleHandler;
    private ResetInstructionHandler resetInstructionHandler;
    private VectorAckHandler irqAck;

    private static final int[] kShift8 = {
            0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff
    };

    private static final int[][] MOVE_T_BW = {
            {4, 4, 8, 8, 8, 12, 14, 12}, {8, 8, 12, 12, 12, 16, 18, 16},
            {10, 10, 14, 14, 14, 18, 20, 18}, {12, 12, 16, 16, 16, 20, 22, 20},
            {14, 14, 18, 18, 18, 22, 24, 22}, {16, 16, 20, 20, 20, 24, 26, 24},
    };
    private static final int[][] MOVE_T_L = {
            {4, 4, 12, 12, 12, 16, 18, 16}, {12, 12, 20, 20, 20, 24, 26, 24},
            {14, 14, 22, 22, 22, 26, 28, 26}, {16, 16, 24, 24, 24, 28, 30, 28},
            {18, 18, 26, 26, 26, 30, 32, 30}, {20, 20, 28, 28, 28, 32, 34, 32},
    };

    private static int calcEaTBw(int dir) {
        if (dir <= 0x0f) return 0;
        if (dir <= 0x1f) return 4;
        if (dir <= 0x27) return 6;
        if (dir <= 0x2f) return 8;
        if (dir <= 0x37) return 10;
        switch (dir) {
            case 0x38: return 8;
            case 0x39: return 12;
            case 0x3a: return 8;
            case 0x3b: return 10;
            case 0x3c: return 4;
            default: return 0;
        }
    }

    private static int calcEaTL(int dir) {
        if (dir <= 0x0f) return 0;
        if (dir <= 0x1f) return 8;
        if (dir <= 0x27) return 10;
        if (dir <= 0x2f) return 12;
        if (dir <= 0x37) return 14;
        switch (dir) {
            case 0x38: return 12;
            case 0x39: return 16;
            case 0x3a: return 12;
            case 0x3b: return 14;
            case 0x3c: return 8;
            default: return 0;
        }
    }

    private static int calcMoveT(int dir, int dest, boolean longSize) {
        int row = 0;
        if (dir <= 0x0f) row = 0;
        else if (dir <= 0x1f || dir == 0x3c) row = 1;
        else if (dir <= 0x27) row = 2;
        else if (dir <= 0x2f || dir == 0x38 || dir == 0x3a) row = 3;
        else if (dir <= 0x37 || dir == 0x3b) row = 4;
        else if (dir == 0x39) row = 5;
        int res = longSize ? MOVE_T_L[row][dest >> 3] : MOVE_T_BW[row][dest >> 3];
        if (dest == 39) res += 4;
        return res;
    }

    private static int shift16(int count) {
        if (count <= 0) return 0;
        if (count >= 16) return 0xffff;
        return (0xffff << (16 - count)) & 0xffff;
    }

    private static int shift32(int count) {
        if (count <= 0) return 0;
        if (count >= 32) return 0xffffffff;
        return 0xffffffff << (32 - count);
    }

    public M68000(int clock) {
        this(clock, Type.M68000);
    }

    public M68000(int clock, Type type) {
        this.clock = clock;
        this.type = type;
        for (int i = 0; i < 8; i++) {
            d[i] = new Reg32();
            a[i] = new Reg32();
            irq[i] = IrqLine.CLEAR;
        }
    }

    public void setMemoryHandlers(MemoryRead read, MemoryWrite write) {
        this.read = read;
        this.write = write;
    }

    public void setByteHandlers(MemoryRead read, MemoryWrite write) {
        this.readByte = read;
        this.writeByte = write;
    }

    public void setCycleHandler(CycleHandler handler) {
        cycleHandler = handler;
    }

    public void setResetInstructionHandler(ResetInstructionHandler handler) {
        resetInstructionHandler = handler;
    }

    public void setIrqAcknowledge(VectorAckHandler handler) {
        irqAck = handler;
    }

    public void setAddressMask(int mask) {
        addressMask = mask & ~1;
    }

    public void setPrefetchEmulation(boolean on) {
        prefetch = on;
        pqCount = 0;
    }

    public void setExceptionHandler(ExceptionHandler handler) {
        exceptionHandler = handler;
    }

    public void setInstructionHook(InstructionHook handler) {
        instructionHook = handler;
    }

    public void setAlineHandler(AlineHandler handler) {
        alineHandler = handler;
    }

    public void setEmulOpHandler(EmulOpHandler handler) {
        emulOpHandler = handler;
    }

    public void setCmpildHandler(CmpildHandler handler) {
        cmpildHandler = handler;
    }

    public void setRteHandler(RteHandler handler) {
        rteHandler = handler;
    }

    public void setIrqTakenHandler(IrqTakenHandler handler) {
        irqTakenHandler = handler;
    }

    public void setIrq(int level, IrqLine state) {
        if (level >= 0 && level < 8) irq[level] = state;
    }

    public void setResetLine(IrqLine state) {
        resetRequest = state;
    }

    public void setHaltLine(IrqLine state) {
        haltRequest = state;
    }

    public int clock() {
        return clock;
    }

    public int pc() {
        return pc_.l;
    }

    public int ppc() {
        return ppc_.l;
    }

    public int peekWord(int address) {
        return getword(address);
    }

    public boolean opcode() {
        return opcode;
    }

    public void reset() {
        cc.t = false;
        cc.s = true;
        cc.x = false;
        cc.n = false;
        cc.z = true;
        cc.v = false;
        cc.c = false;
        cc.im = 7;
        opcode = true;
        otherSp_.l = 0;
        a[7].setWh(getword(0));
        a[7].setWl(getword(2));
        pc_.setWh(getword(4));
        pc_.setWl(getword(6));
        for (int i = 0; i < 8; i++) irq[i] = IrqLine.CLEAR;
        haltRequest = IrqLine.CLEAR;
        resetRequest = IrqLine.CLEAR;
        halted = false;
        stopped = false;
        pqCount = 0;
    }

    private int getword(int address) {
        return read.read(address & addressMask) & 0xffff;
    }

    private void putword(int address, int value) {
        write.write(address & addressMask, value & 0xffff);
    }

    private int getbyte(int address) {
        address &= (addressMask | 1);
        if (readByte != null) return readByte.read(address) & 0xff;
        int value = getword(address);
        return (address & 1) != 0 ? (value & 0xff) : ((value >> 8) & 0xff);
    }

    private void putbyte(int address, int value) {
        address &= (addressMask | 1);
        value &= 0xff;
        if (writeByte != null) {
            writeByte.write(address, value);
            return;
        }
        int old = getword(address);
        if ((address & 1) != 0) putword(address, (old & 0xff00) | value);
        else putword(address, (old & 0x00ff) | (value << 8));
    }

    private int fetchWord() {
        if (!prefetch) {
            int value = progWord(pc_.l);
            pc_.l += 2;
            return value;
        }
        int value = progWord(pc_.l);
        pc_.l += 2;
        int[] next = new int[2];
        for (int i = 0; i < 2; i++) next[i] = progWord(pc_.l + i * 2);
        pqAddr = pc_.l;
        pqVal[0] = next[0];
        pqVal[1] = next[1];
        pqCount = 2;
        return value;
    }

    private int fetchLong() {
        if (prefetch) {
            int hi = fetchWord();
            return (hi << 16) | fetchWord();
        }
        int value = (progWord(pc_.l) << 16) | progWord(pc_.l + 2);
        pc_.l += 4;
        return value;
    }

    private int progWord(int address) {
        if (prefetch) {
            for (int i = 0; i < pqCount; i++) {
                if (address == pqAddr + i * 2) return pqVal[i];
            }
        }
        return getword(address);
    }

    private int getFlags() {
        int value = cc.t ? 0x8000 : 0;
        if (cc.s) value |= 0x2000;
        value |= (cc.im & 7) << 8;
        if (cc.x) value |= 0x10;
        if (cc.n) value |= 0x08;
        if (cc.z) value |= 0x04;
        if (cc.v) value |= 0x02;
        if (cc.c) value |= 0x01;
        return value;
    }

    private void setFlags(int value) {
        boolean supervisor = (value & 0x2000) != 0;
        if (cc.s != supervisor) {
            int current = a[7].l;
            a[7].l = otherSp_.l;
            otherSp_.l = current;
            cc.s = supervisor;
        }
        cc.t = (value & 0x8000) != 0;
        cc.im = (value >> 8) & 7;
        cc.x = (value & 0x10) != 0;
        cc.n = (value & 0x08) != 0;
        cc.z = (value & 0x04) != 0;
        cc.v = (value & 0x02) != 0;
        cc.c = (value & 0x01) != 0;
    }

    private int indexedOffset(int base) {
        int extension = fetchWord();
        int index = (extension >> 12) & 7;
        Reg32 reg = (extension & 0x8000) != 0 ? a[index] : d[index];
        if (type == Type.M68020 && (extension & 0x100) != 0) {
            boolean baseSuppress = (extension & 0x80) != 0;
            boolean indexSuppress = (extension & 0x40) != 0;
            int scale = 1 << ((extension >> 9) & 3);
            int bdSize = (extension >> 4) & 3;
            int baseDisp = 0;
            if (bdSize == 2) baseDisp = (short) fetchWord();
            else if (bdSize == 3) baseDisp = fetchLong();
            int indexVal = 0;
            if (!indexSuppress) {
                indexVal = (extension & 0x800) != 0 ? reg.l : (short) reg.wl();
                indexVal *= scale;
            }
            int effBase = baseSuppress ? 0 : base;
            int iis = extension & 7;
            if (iis == 0) {
                return effBase + baseDisp + indexVal;
            }
            int intermediateAddr = effBase + baseDisp;
            int intermediate;
            if (iis <= 4) {
                intermediateAddr = intermediateAddr + indexVal;
                intermediate = (getword(intermediateAddr) << 16) | getword(intermediateAddr + 2);
            } else {
                intermediate = (getword(intermediateAddr) << 16) | getword(intermediateAddr + 2);
                intermediate = intermediate + indexVal;
            }
            int outerDisp = 0;
            int odSize = iis & 3;
            if (odSize == 2) outerDisp = (short) fetchWord();
            else if (odSize == 3) outerDisp = fetchLong();
            return intermediate + outerDisp;
        }
        int displacement = (byte) (extension & 0xff);
        int value = (extension & 0x800) != 0 ? reg.l : (short) reg.wl();
        return base + value + displacement;
    }

    private int readB(int dir) {
        if (dir <= 0x07) return d[dir & 7].l0();
        if (dir <= 0x0f) return a[dir & 7].l0();
        if (dir <= 0x17) {
            ea = a[dir & 7].l;
        } else if (dir <= 0x1f) {
            ea = a[dir & 7].l;
            a[dir & 7].l = ea + ((dir & 7) == 7 ? 2 : 1);
        } else if (dir <= 0x27) {
            a[dir & 7].l -= ((dir & 7) == 7 ? 2 : 1);
            ea = a[dir & 7].l;
        } else if (dir <= 0x2f) {
            ea = a[dir & 7].l + (short) fetchWord();
        } else if (dir <= 0x37) {
            ea = indexedOffset(a[dir & 7].l);
        } else if (dir == 0x38) {
            ea = (short) fetchWord();
        } else if (dir == 0x39) {
            ea = fetchLong();
        } else if (dir == 0x3a) {
            ea = pc_.l + (short) progWord(pc_.l);
            pc_.l += 2;
        } else if (dir == 0x3b) {
            ea = indexedOffset(pc_.l);
        } else if (dir == 0x3c) {
            ea = pc_.l;
            pc_.l += 2;
            return getword(ea) & 0xff;
        }
        opcode = false;
        int value = getbyte(ea);
        opcode = true;
        return value;
    }

    private void writeB2(int dir, int value) {
        value &= 0xff;
        if (dir <= 0x07) d[dir & 7].setL0(value);
        else if (dir <= 0x0f) a[dir & 7].setL0(value);
        else putbyte(ea, value);
    }

    private void writeB(int dir, int value) {
        value &= 0xff;
        if (dir <= 0x07) {
            d[dir & 7].setL0(value);
            return;
        }
        if (dir <= 0x0f) {
            a[dir & 7].setL0(value);
            return;
        }
        if (dir <= 0x17) {
            ea = a[dir & 7].l;
        } else if (dir <= 0x1f) {
            ea = a[dir & 7].l;
            a[dir & 7].l += ((dir & 7) == 7 ? 2 : 1);
        } else if (dir <= 0x27) {
            a[dir & 7].l -= ((dir & 7) == 7 ? 2 : 1);
            ea = a[dir & 7].l;
        } else if (dir <= 0x2f) {
            ea = a[dir & 7].l + (short) fetchWord();
        } else if (dir <= 0x37) {
            ea = indexedOffset(a[dir & 7].l);
        } else if (dir == 0x38) {
            ea = (short) fetchWord();
        } else if (dir == 0x39) {
            ea = fetchLong();
        }
        putbyte(ea, value);
    }

    private int readW(int dir) {
        if (dir <= 0x07) return d[dir & 7].wl();
        if (dir <= 0x0f) return a[dir & 7].wl();
        if (dir <= 0x17) {
            ea = a[dir & 7].l;
        } else if (dir <= 0x1f) {
            ea = a[dir & 7].l;
            a[dir & 7].l = ea + 2;
        } else if (dir <= 0x27) {
            a[dir & 7].l -= 2;
            ea = a[dir & 7].l;
        } else if (dir <= 0x2f) {
            ea = a[dir & 7].l + (short) fetchWord();
        } else if (dir <= 0x37) {
            ea = indexedOffset(a[dir & 7].l);
        } else if (dir == 0x38) {
            ea = (short) fetchWord();
        } else if (dir == 0x39) {
            ea = fetchLong();
        } else if (dir == 0x3a) {
            ea = pc_.l + (short) progWord(pc_.l);
            pc_.l += 2;
        } else if (dir == 0x3b) {
            ea = indexedOffset(pc_.l);
        } else if (dir == 0x3c) {
            ea = pc_.l;
            pc_.l += 2;
            return getword(ea);
        }
        opcode = false;
        int value = getword(ea);
        opcode = true;
        return value;
    }

    private void writeW2(int dir, int value) {
        value &= 0xffff;
        if (dir <= 0x07) d[dir & 7].setWl(value);
        else if (dir <= 0x0f) a[dir & 7].setWl(value);
        else putword(ea, value);
    }

    private void writeW(int dir, int value) {
        value &= 0xffff;
        if (dir <= 0x07) {
            d[dir & 7].setWl(value);
            return;
        }
        if (dir <= 0x0f) {
            a[dir & 7].setWl(value);
            return;
        }
        if (dir <= 0x17) {
            ea = a[dir & 7].l;
        } else if (dir <= 0x1f) {
            ea = a[dir & 7].l;
            a[dir & 7].l += 2;
        } else if (dir <= 0x27) {
            a[dir & 7].l -= 2;
            ea = a[dir & 7].l;
        } else if (dir <= 0x2f) {
            ea = a[dir & 7].l + (short) fetchWord();
        } else if (dir <= 0x37) {
            ea = indexedOffset(a[dir & 7].l);
        } else if (dir == 0x38) {
            ea = (short) fetchWord();
        } else if (dir == 0x39) {
            ea = fetchLong();
        }
        putword(ea, value);
    }

    private int readL(int dir) {
        if (dir <= 0x07) return d[dir & 7].l;
        if (dir <= 0x0f) return a[dir & 7].l;
        if (dir <= 0x17) {
            ea = a[dir & 7].l;
        } else if (dir <= 0x1f) {
            ea = a[dir & 7].l;
            a[dir & 7].l += 4;
        } else if (dir <= 0x27) {
            a[dir & 7].l -= 4;
            ea = a[dir & 7].l;
        } else if (dir <= 0x2f) {
            ea = a[dir & 7].l + (short) fetchWord();
        } else if (dir <= 0x37) {
            ea = indexedOffset(a[dir & 7].l);
        } else if (dir == 0x38) {
            ea = (short) fetchWord();
        } else if (dir == 0x39) {
            ea = fetchLong();
        } else if (dir == 0x3a) {
            ea = pc_.l + (short) progWord(pc_.l);
            pc_.l += 2;
        } else if (dir == 0x3b) {
            ea = indexedOffset(pc_.l);
        } else if (dir == 0x3c) {
            ea = pc_.l;
            return fetchLong();
        }
        opcode = false;
        int value = (getword(ea) << 16) | getword(ea + 2);
        opcode = true;
        return value;
    }

    private void writeL2(int dir, int value) {
        if (dir <= 0x07) {
            d[dir & 7].l = value;
        } else if (dir <= 0x0f) {
            a[dir & 7].l = value;
        } else {
            putword(ea, (value >>> 16) & 0xffff);
            putword(ea + 2, value & 0xffff);
        }
    }

    private void writeL(int dir, int value) {
        if (dir <= 0x07) {
            d[dir & 7].l = value;
            return;
        }
        if (dir <= 0x0f) {
            a[dir & 7].l = value;
            return;
        }
        if (dir <= 0x17) {
            ea = a[dir & 7].l;
        } else if (dir <= 0x1f) {
            ea = a[dir & 7].l;
            a[dir & 7].l = ea + 4;
        } else if (dir <= 0x27) {
            a[dir & 7].l -= 4;
            ea = a[dir & 7].l;
        } else if (dir <= 0x2f) {
            ea = a[dir & 7].l + (short) fetchWord();
        } else if (dir <= 0x37) {
            ea = indexedOffset(a[dir & 7].l);
        } else if (dir == 0x38) {
            ea = (short) fetchWord();
        } else if (dir == 0x39) {
            ea = fetchLong();
        }
        putword(ea, (value >>> 16) & 0xffff);
        putword(ea + 2, value & 0xffff);
    }

    private int readEa(int dir) {
        if (dir >= 0x10 && dir <= 0x27) return a[dir & 7].l;
        if (dir >= 0x28 && dir <= 0x2f) {
            return a[dir & 7].l + (short) fetchWord();
        }
        if (dir >= 0x30 && dir <= 0x37) return indexedOffset(a[dir & 7].l);
        switch (dir) {
            case 0x38: return (short) fetchWord();
            case 0x39: return fetchLong();
            case 0x3a: {
                int base = pc_.l;
                return base + (short) fetchWord();
            }
            case 0x3b: return indexedOffset(pc_.l);
            default: return 0;
        }
    }

    private boolean condition(int code) {
        switch (code & 0x0f) {
            case 0x00: return true;
            case 0x01: return false;
            case 0x02: return !cc.c && !cc.z;
            case 0x03: return cc.c || cc.z;
            case 0x04: return !cc.c;
            case 0x05: return cc.c;
            case 0x06: return !cc.z;
            case 0x07: return cc.z;
            case 0x08: return !cc.v;
            case 0x09: return cc.v;
            case 0x0a: return !cc.n;
            case 0x0b: return cc.n;
            case 0x0c: return cc.n == cc.v;
            case 0x0d: return cc.n != cc.v;
            case 0x0e: return (cc.n == cc.v) && !cc.z;
            default: return (cc.n != cc.v) || cc.z;
        }
    }

    private void exception(int vector, int extraCycles) {
        exceptions++;
        cycles += extraCycles;
        if (exceptionHandler != null) exceptionHandler.onException(vector, ppc_.l != 0 ? ppc_.l : pc_.l);
        int sr = getFlags();
        setFlags(sr | 0x2000);
        cc.t = false;
        int faultPc = ppc_.l != 0 ? ppc_.l : pc_.l;
        int vecNum = (vector & 0xffffffffL) <= 255L ? vector : (vector >> 2);
        int vecAddr = vbr + (vecNum << 2);
        if (type != Type.M68000) {
            a[7].l -= 2; putword(a[7].l, vecNum & 0x0fff);
            a[7].l -= 4; putword(a[7].l, (faultPc >>> 16) & 0xffff); putword(a[7].l + 2, faultPc & 0xffff);
            a[7].l -= 2; putword(a[7].l, sr);
        } else {
            a[7].l -= 6; putword(a[7].l, sr);
            putword(a[7].l + 2, (faultPc >>> 16) & 0xffff); putword(a[7].l + 4, faultPc & 0xffff);
        }
        opcode = false;
        pc_.l = (getword(vecAddr) << 16) | getword(vecAddr + 2);
        opcode = true;
    }

    private boolean checkSupervisor() {
        if (cc.s) return true;
        pc_.l = ppc_.l;
        exception(8, 34);
        return false;
    }

    private void illegal() {
        pc_.l = ppc_.l;
        exception(4, 34);
    }

    private void group0(int instruction) {
        int dir = instruction & 0x3f;
        int dest = (instruction >> 9) & 7;
        int orig = instruction & 7;
        int op = (instruction >> 6) & 0x3f;

        switch (op) {
            case 0x00: {  // ori.b
                int immediate = fetchWord() & 0xff;
                if (dir != 0x3c) {
                    cycles += (dir >> 3) != 0 ? 12 + calcEaTBw(dir) : 8;
                    int result = (readB(dir) | immediate) & 0xff;
                    writeB2(dir, result);
                    cc.n = (result & 0x80) != 0;
                    cc.z = result == 0;
                    cc.c = false;
                    cc.v = false;
                } else {  // ori to ccr
                    cycles += 20;
                    int result = ((getFlags() & 0xff) | immediate) & 0xff;
                    cc.x = (result & 0x10) != 0;
                    cc.n = (result & 0x08) != 0;
                    cc.z = (result & 0x04) != 0;
                    cc.v = (result & 0x02) != 0;
                    cc.c = (result & 0x01) != 0;
                }
                break;
            }
            case 0x01: {  // ori.w
                int immediate = fetchWord();
                if (dir != 0x3c) {
                    cycles += (dir >> 3) != 0 ? 12 + calcEaTBw(dir) : 8;
                    int result = (readW(dir) | immediate) & 0xffff;
                    writeW2(dir, result);
                    cc.n = (result & 0x8000) != 0;
                    cc.z = result == 0;
                    cc.c = false;
                    cc.v = false;
                } else if (checkSupervisor()) {  // ori to sr
                    cycles += 20;
                    setFlags((getFlags() | immediate) & 0xffff);
                }
                break;
            }
            case 0x02: {  // ori.l
                cycles += (dir >> 3) != 0 ? 20 + calcEaTL(dir) : 16;
                int immediate = fetchLong();
                int result = readL(dir) | immediate;
                writeL2(dir, result);
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
                break;
            }
            case 0x04: case 0x0c: case 0x14: case 0x1c:
            case 0x24: case 0x2c: case 0x34: case 0x3c: {  // btst dynamic / movep.w er
                if ((dir >> 3) == 0) {
                    cycles += 6;
                    int mask = 1 << (d[dest].l0() & 0x1f);
                    cc.z = (d[orig].l & mask) == 0;
                } else if ((dir >> 3) == 1) {  // movep.w memory to register
                    cycles += 16;
                    int address = a[orig].l + (short) fetchWord();
                    d[dest].l = (d[dest].l & 0xffff0000) | (getbyte(address) << 8) | getbyte(address + 2);
                } else {
                    cycles += 4 + calcEaTBw(dir);
                    int mask = (1 << (d[dest].l0() & 7)) & 0xff;
                    cc.z = (readB(dir) & mask) == 0;
                }
                break;
            }
            case 0x05: case 0x0d: case 0x15: case 0x1d:
            case 0x25: case 0x2d: case 0x35: case 0x3d: {  // bchg dynamic / movep.l er
                if ((dir >> 3) == 0) {
                    cycles += 8;
                    int mask = 1 << (d[dest].l0() & 0x1f);
                    cc.z = (d[orig].l & mask) == 0;
                    d[orig].l ^= mask;
                } else if ((dir >> 3) == 1) {  // movep.l memory to register
                    cycles += 24;
                    int address = a[orig].l + (short) fetchWord();
                    d[dest].l = (getbyte(address) << 24) | (getbyte(address + 2) << 16)
                            | (getbyte(address + 4) << 8) | getbyte(address + 6);
                } else {
                    cycles += 8 + calcEaTBw(dir);
                    int mask = (1 << (d[dest].l0() & 7)) & 0xff;
                    int value = readB(dir);
                    cc.z = (value & mask) == 0;
                    writeB2(dir, (value ^ mask) & 0xff);
                }
                break;
            }
            case 0x06: case 0x0e: case 0x16: case 0x1e:
            case 0x26: case 0x2e: case 0x36: case 0x3e: {  // bclr dynamic / movep.w re
                if ((dir >> 3) == 0) {
                    cycles += 10;
                    int mask = 1 << (d[dest].l0() & 0x1f);
                    cc.z = (d[orig].l & mask) == 0;
                    d[orig].l &= ~mask;
                } else if ((dir >> 3) == 1) {  // movep.w register to memory
                    cycles += 16;
                    int address = a[orig].l + (short) fetchWord();
                    putbyte(address, (d[dest].l >> 8) & 0xff);
                    putbyte(address + 2, d[dest].l0());
                } else {
                    cycles += 8 + calcEaTBw(dir);
                    if (type == Type.M68010) cycles += 2;
                    int mask = (1 << (d[dest].l0() & 7)) & 0xff;
                    int value = readB(dir);
                    cc.z = (value & mask) == 0;
                    writeB2(dir, (value & ~mask) & 0xff);
                }
                break;
            }
            case 0x07: case 0x0f: case 0x17: case 0x1f:
            case 0x27: case 0x2f: case 0x37: case 0x3f: {  // bset dynamic / movep.l re
                if ((dir >> 3) == 0) {
                    cycles += 8;
                    int mask = 1 << (d[dest].l0() & 0x1f);
                    cc.z = (d[orig].l & mask) == 0;
                    d[orig].l |= mask;
                } else if ((dir >> 3) == 1) {  // movep.l register to memory
                    cycles += 24;
                    int address = a[orig].l + (short) fetchWord();
                    putbyte(address, (d[dest].l >> 24) & 0xff);
                    putbyte(address + 2, (d[dest].l >> 16) & 0xff);
                    putbyte(address + 4, (d[dest].l >> 8) & 0xff);
                    putbyte(address + 6, d[dest].l0());
                } else {
                    cycles += 8 + calcEaTBw(dir);
                    int mask = (1 << (d[dest].l0() & 7)) & 0xff;
                    int value = readB(dir);
                    cc.z = (value & mask) == 0;
                    writeB2(dir, value | mask);
                }
                break;
            }
            case 0x08: {  // andi.b
                int immediate = fetchWord() & 0xff;
                if (dir != 0x3c) {
                    cycles += (dir >> 3) != 0 ? 12 + calcEaTBw(dir) : 8;
                    int result = (readB(dir) & immediate) & 0xff;
                    writeB2(dir, result);
                    cc.n = (result & 0x80) != 0;
                    cc.z = result == 0;
                    cc.v = false;
                    cc.c = false;
                } else {  // andi to ccr
                    cycles += type == Type.M68010 ? 16 : 20;
                    int result = ((getFlags() & 0xff) & immediate) & 0xff;
                    cc.x = (result & 0x10) != 0;
                    cc.n = (result & 0x08) != 0;
                    cc.z = (result & 0x04) != 0;
                    cc.v = (result & 0x02) != 0;
                    cc.c = (result & 0x01) != 0;
                }
                break;
            }
            case 0x09: {  // andi.w
                int immediate = fetchWord();
                if (dir != 0x3c) {
                    cycles += (dir >> 3) != 0 ? 12 + calcEaTBw(dir) : 8;
                    int result = (readW(dir) & immediate) & 0xffff;
                    writeW2(dir, result);
                    cc.n = (result & 0x8000) != 0;
                    cc.z = result == 0;
                    cc.v = false;
                    cc.c = false;
                } else if (checkSupervisor()) {  // andi to sr
                    cycles += type == Type.M68010 ? 16 : 20;
                    setFlags((getFlags() & immediate) & 0xffff);
                }
                break;
            }
            case 0x0a: {  // andi.l
                cycles += (dir >> 3) != 0 ? 20 + calcEaTL(dir) : 14;
                int immediate = fetchLong();
                int result = readL(dir) & immediate;
                writeL2(dir, result);
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.v = false;
                cc.c = false;
                break;
            }
            case 0x10: {  // subi.b
                cycles += (dir >> 3) != 0 ? 12 + calcEaTBw(dir) : 8;
                int immediate = fetchWord() & 0xff;
                int value = readB(dir);
                int result = (value - immediate) & 0xffff;
                writeB2(dir, result);
                cc.v = (((immediate ^ value) & (result ^ value)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                break;
            }
            case 0x11: {  // subi.w
                cycles += (dir >> 3) != 0 ? 12 + calcEaTBw(dir) : 8;
                int immediate = fetchWord();
                int value = readW(dir);
                int result = value - immediate;
                writeW2(dir, result);
                cc.v = ((((immediate ^ value) & (result ^ value)) >> 8) & 0x80) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                break;
            }
            case 0x12: {  // subi.l
                if ((dir >> 3) != 0) cycles += 20 + calcEaTL(dir);
                else cycles += type == Type.M68010 ? 14 : 16;
                int immediate = fetchLong();
                int value = readL(dir);
                int result = value - immediate;
                writeL2(dir, result);
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.v = ((((immediate ^ value) & (result ^ value)) >> 24) & 0x80) != 0;
                cc.c = ((((immediate & result) | (~value & (immediate | result))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
                break;
            }
            case 0x18: {  // addi.b
                cycles += (dir >> 3) != 0 ? 12 + calcEaTBw(dir) : 8;
                int immediate = fetchWord() & 0xff;
                int value = readB(dir);
                int result = (immediate + value) & 0xffff;
                writeB2(dir, result);
                cc.v = (((immediate ^ result) & (value ^ result)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                break;
            }
            case 0x19: {  // addi.w
                cycles += (dir >> 3) != 0 ? 12 + calcEaTBw(dir) : 8;
                int immediate = fetchWord();
                int value = readW(dir);
                int result = immediate + value;
                writeW2(dir, result);
                cc.v = ((((immediate ^ result) & (value ^ result)) >> 8) & 0x80) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                break;
            }
            case 0x1a: {  // addi.l
                if ((dir >> 3) != 0) cycles += 20 + calcEaTL(dir);
                else cycles += type == Type.M68010 ? 14 : 16;
                int immediate = fetchLong();
                int value = readL(dir);
                int result = immediate + value;
                writeL2(dir, result);
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.v = ((((immediate ^ result) & (value ^ result)) >> 24) & 0x80) != 0;
                cc.c = ((((immediate & value) | (~result & (immediate | value))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
                break;
            }
            case 0x20: {  // btst static
                int bit = fetchWord();
                if ((dir >> 3) == 0) {
                    cycles += 10;
                    cc.z = ((d[orig].l >>> (bit & 0x1f)) & 1) == 0;
                } else {
                    cycles += 8 + calcEaTBw(dir);
                    cc.z = ((readB(dir) >> (bit & 7)) & 1) == 0;
                }
                break;
            }
            case 0x21: {  // bchg static
                int bit = fetchWord();
                if ((dir >> 3) == 0) {
                    cycles += 12;
                    cc.z = ((d[orig].l >>> (bit & 0x1f)) & 1) == 0;
                    d[orig].l ^= 1 << (bit & 0x1f);
                } else {
                    cycles += 12 + calcEaTBw(dir);
                    int value = readB(dir);
                    cc.z = ((value >> (bit & 7)) & 1) == 0;
                    writeB2(dir, value ^ (1 << (bit & 7)));
                }
                break;
            }
            case 0x22: {  // bclr static
                int bit = fetchWord();
                if ((dir >> 3) == 0) {
                    cycles += 12;
                    cc.z = ((d[orig].l >>> (bit & 0x1f)) & 1) == 0;
                    d[orig].l &= ~(1 << (bit & 0x1f));
                } else {
                    cycles += 12 + calcEaTBw(dir);
                    int value = readB(dir);
                    cc.z = ((value >> (bit & 7)) & 1) == 0;
                    writeB2(dir, value & ~(1 << (bit & 7)));
                }
                break;
            }
            case 0x23: {  // bset static
                int bit = fetchWord();
                if ((dir >> 3) == 0) {
                    cycles += 12;
                    cc.z = ((d[orig].l >>> (bit & 0x1f)) & 1) == 0;
                    d[orig].l |= 1 << (bit & 0x1f);
                } else {
                    cycles += 12 + calcEaTBw(dir);
                    int value = readB(dir);
                    cc.z = ((value >> (bit & 7)) & 1) == 0;
                    writeB2(dir, value | (1 << (bit & 7)));
                }
                break;
            }
            case 0x28: {  // eori.b
                int immediate = fetchWord() & 0xff;
                if (dir != 0x3c) {
                    cycles += (dir >> 3) != 0 ? 12 + calcEaTBw(dir) : 8;
                    int result = (readB(dir) ^ immediate) & 0xff;
                    writeB2(dir, result);
                    cc.v = false;
                    cc.c = false;
                    cc.n = (result & 0x80) != 0;
                    cc.z = result == 0;
                } else {  // eori to ccr
                    cycles += 20;
                    int result = ((getFlags() & 0xff) ^ immediate) & 0xff;
                    cc.x = (result & 0x10) != 0;
                    cc.n = (result & 0x08) != 0;
                    cc.z = (result & 0x04) != 0;
                    cc.v = (result & 0x02) != 0;
                    cc.c = (result & 0x01) != 0;
                }
                break;
            }
            case 0x29: {  // eori.w
                int immediate = fetchWord();
                if (dir != 0x3c) {
                    cycles += (dir >> 3) != 0 ? 12 + calcEaTBw(dir) : 8;
                    int result = (readW(dir) ^ immediate) & 0xffff;
                    writeW2(dir, result);
                    cc.v = false;
                    cc.c = false;
                    cc.n = (result & 0x8000) != 0;
                    cc.z = result == 0;
                } else if (checkSupervisor()) {  // eori to sr
                    cycles += 20;
                    setFlags((getFlags() ^ immediate) & 0xffff);
                }
                break;
            }
            case 0x2a: {  // eori.l
                if ((dir >> 3) != 0) cycles += 20 + calcEaTL(dir);
                else cycles += type == Type.M68010 ? 14 : 16;
                int immediate = fetchLong();
                int result = readL(dir) ^ immediate;
                writeL2(dir, result);
                cc.v = false;
                cc.c = false;
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                break;
            }
            case 0x30: {  // cmpi.b
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 8;
                int immediate = fetchWord() & 0xff;
                int value = readB(dir);
                int result = (value - immediate) & 0xffff;
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.v = (((immediate ^ value) & (result ^ value)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                break;
            }
            case 0x31: {  // cmpi.w
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 8;
                int immediate = fetchWord();
                int value = readW(dir);
                int result = value - immediate;
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                cc.v = ((((immediate ^ value) & (result ^ value)) >> 8) & 0x80) != 0;
                cc.c = (result & 0x10000) != 0;
                break;
            }
            case 0x32: {  // cmpi.l
                if (type == Type.M68010 || (dir >> 3) != 0) cycles += 12 + calcEaTL(dir);
                else cycles += 14;
                int immediate = fetchLong();
                int value = readL(dir);
                int result = value - immediate;
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.v = ((((immediate ^ value) & (result ^ value)) >> 24) & 0x80) != 0;
                cc.c = ((((immediate & result) | (~value & (immediate | result))) >> 23) & 0x100) != 0;
                if (cmpildHandler != null && (dir >> 3) == 0) {
                    cmpildHandler.onCmpild(dir & 7, immediate);
                }
                break;
            }
            case 0x38: case 0x39: case 0x3a: {
                int ext = fetchWord();
                int reg = (ext >> 12) & 7;
                boolean isAddrReg = (ext & 0x0800) != 0;
                boolean regToEa = (ext & 0x8000) != 0;
                if (op == 0x38) {
                    cycles += 4 + calcEaTBw(dir);
                    if (regToEa) {
                        writeB2(dir, isAddrReg ? a[reg].l0() : d[reg].l0());
                    } else {
                        int v = readB(dir);
                        if (isAddrReg) a[reg].setL0(v); else d[reg].setL0(v);
                    }
                } else if (op == 0x39) {
                    cycles += 4 + calcEaTBw(dir);
                    if (regToEa) {
                        writeW2(dir, isAddrReg ? a[reg].wl() : d[reg].wl());
                    } else {
                        int v = readW(dir);
                        if (isAddrReg) a[reg].setWl(v); else d[reg].setWl(v);
                    }
                } else {
                    cycles += 4 + calcEaTL(dir);
                    if (regToEa) {
                        writeL2(dir, isAddrReg ? a[reg].l : d[reg].l);
                    } else {
                        int v = readL(dir);
                        if (isAddrReg) a[reg].l = v; else d[reg].l = v;
                    }
                }
                break;
            }
            default:
                illegal();
                break;
        }
    }

    private void groupB(int instruction) {
        int dir = instruction & 0x3f;
        int dest = (instruction >> 9) & 7;
        int orig = instruction & 7;

        switch ((instruction >> 6) & 7) {
            case 0x0: {  // cmp.b
                cycles += 4 + calcEaTBw(dir);
                int right = readB(dir);
                int left = d[dest].l0();
                int result = (left - right) & 0xffff;
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                break;
            }
            case 0x1: {  // cmp.w
                cycles += 4 + calcEaTBw(dir);
                int right = readW(dir);
                int left = d[dest].wl();
                int result = left - right;
                cc.n = (result & 0x8000) != 0;
                cc.z = (result & 0xffff) == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                cc.c = (result & 0x10000) != 0;
                break;
            }
            case 0x2: {  // cmp.l
                cycles += 6 + calcEaTL(dir);
                int right = readL(dir);
                int left = d[dest].l;
                int result = left - right;
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                break;
            }
            case 0x3: {  // cmpa.w
                cycles += 6 + calcEaTBw(dir);
                int right = (short) readW(dir);
                int left = a[dest].l;
                int result = left - right;
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                break;
            }
            case 0x4: {
                if ((dir >> 3) == 1) {  // cmpm.b
                    cycles += 12;
                    int right = getbyte(a[orig].l);
                    a[orig].l += 1;
                    int left = getbyte(a[dest].l);
                    a[dest].l += 1;
                    int result = (left - right) & 0xffff;
                    cc.n = (result & 0x80) != 0;
                    cc.z = (result & 0xff) == 0;
                    cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                    cc.c = (result & 0x100) != 0;
                } else {  // eor.b
                    cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                    int result = (readB(dir) ^ d[dest].l0()) & 0xff;
                    writeB2(dir, result);
                    cc.n = (result & 0x80) != 0;
                    cc.z = result == 0;
                    cc.v = false;
                    cc.c = false;
                }
                break;
            }
            case 0x5: {
                if ((dir >> 3) == 1) {  // cmpm.w
                    cycles += 12;
                    int right = getword(a[orig].l);
                    a[orig].l += 2;
                    int left = getword(a[dest].l);
                    a[dest].l += 2;
                    int result = left - right;
                    cc.n = (result & 0x8000) != 0;
                    cc.z = (result & 0xffff) == 0;
                    cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                    cc.c = (result & 0x10000) != 0;
                } else {  // eor.w
                    cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                    int result = (readW(dir) ^ d[dest].wl()) & 0xffff;
                    writeW2(dir, result);
                    cc.n = (result & 0x8000) != 0;
                    cc.z = result == 0;
                    cc.v = false;
                    cc.c = false;
                }
                break;
            }
            case 0x6: {
                if ((dir >> 3) == 1) {  // cmpm.l
                    cycles += 20;
                    int right = (getword(a[orig].l) << 16) | getword(a[orig].l + 2);
                    a[orig].l += 4;
                    int left = (getword(a[dest].l) << 16) | getword(a[dest].l + 2);
                    a[dest].l += 4;
                    int result = left - right;
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                    cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                } else {  // eor.l
                    cycles += (dir >> 3) != 0 ? 12 + calcEaTL(dir) : 8;
                    int result = readL(dir) ^ d[dest].l;
                    writeL2(dir, result);
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    cc.v = false;
                    cc.c = false;
                }
                break;
            }
            default: {  // cmpa.l
                cycles += 6 + calcEaTL(dir);
                int right = readL(dir);
                int left = a[dest].l;
                int result = left - right;
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                break;
            }
        }
    }

    private void groupC(int instruction) {
        int dir = instruction & 0x3f;
        int dest = (instruction >> 9) & 7;
        int orig = instruction & 7;

        switch ((instruction >> 6) & 7) {
            case 0x0: {  // and.b ea,Dn
                cycles += 4 + calcEaTBw(dir);
                int result = (d[dest].l0() & readB(dir)) & 0xff;
                d[dest].setL0(result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
                break;
            }
            case 0x1: {  // and.w ea,Dn
                cycles += 4 + calcEaTBw(dir);
                int result = (d[dest].wl() & readW(dir)) & 0xffff;
                d[dest].setWl(result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
                break;
            }
            case 0x2: {  // and.l ea,Dn
                cycles += 6 + calcEaTL(dir);
                int result = d[dest].l & readL(dir);
                d[dest].l = result;
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
                break;
            }
            case 0x3: {  // mulu
                cycles += type == Type.M68010 ? 30 : 54;
                if ((dir >> 3) == 0) cycles += calcEaTBw(dir);
                int result = (readW(dir) & 0xffff) * (d[dest].wl() & 0xffff);
                d[dest].l = result;
                cc.z = result == 0;
                cc.n = (result & 0x80000000) != 0;
                cc.v = false;
                cc.c = false;
                break;
            }
            case 0x4: {
                if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // abcd
                    int right = 0;
                    int left = 0;
                    if ((dir >> 3) == 0) {
                        cycles += 6;
                        right = d[orig].l0();
                        left = d[dest].l0();
                    } else {
                        cycles += 18;
                        a[orig].l -= 1;
                        right = getbyte(a[orig].l);
                        a[dest].l -= 1;
                        left = getbyte(a[dest].l);
                    }
                    int result = (right & 0x0f) + (left & 0x0f) + (cc.x ? 1 : 0);
                    int correction = Integer.compareUnsigned(result, 9) > 0 ? 6 : 0;
                    result += (right & 0xf0) + (left & 0xf0);
                    cc.v = (~result & 0x80) != 0;
                    result += correction;
                    cc.c = Integer.compareUnsigned(result, 0x9f) > 0;
                    cc.x = cc.c;
                    if (cc.c) result -= 0xa0;
                    cc.n = (result & 0x80) != 0;
                    cc.v = cc.v && cc.n;
                    cc.z = (result & 0xff) == 0;
                    if ((dir >> 3) == 0) d[dest].setL0(result);
                    else putbyte(a[dest].l, result);
                } else {  // and.b Dn,ea
                    cycles += 8 + calcEaTBw(dir);
                    int result = (readB(dir) & d[dest].l0()) & 0xff;
                    writeB2(dir, result);
                    cc.n = (result & 0x80) != 0;
                    cc.z = result == 0;
                    cc.c = false;
                    cc.v = false;
                }
                break;
            }
            case 0x5: {
                if ((dir >> 3) == 0) {  // exg Dx,Dy
                    cycles += 6;
                    int tmp = d[dest].l;
                    d[dest].l = d[orig].l;
                    d[orig].l = tmp;
                } else if ((dir >> 3) == 1) {  // exg Ax,Ay
                    cycles += 6;
                    int tmp = a[dest].l;
                    a[dest].l = a[orig].l;
                    a[orig].l = tmp;
                } else {  // and.w Dn,ea
                    cycles += 8 + calcEaTBw(dir);
                    int result = (readW(dir) & d[dest].wl()) & 0xffff;
                    writeW2(dir, result);
                    cc.n = (result & 0x8000) != 0;
                    cc.z = result == 0;
                    cc.c = false;
                    cc.v = false;
                }
                break;
            }
            case 0x6: {
                if ((dir >> 3) == 1) {  // exg Dx,Ay
                    cycles += 6;
                    int tmp = d[dest].l;
                    d[dest].l = a[orig].l;
                    a[orig].l = tmp;
                } else {  // and.l Dn,ea
                    cycles += 12 + calcEaTL(dir);
                    int result = readL(dir) & d[dest].l;
                    writeL2(dir, result);
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    cc.c = false;
                    cc.v = false;
                }
                break;
            }
            default: {  // muls
                cycles += type == Type.M68010 ? 32 : 54;
                if ((dir >> 3) == 0) cycles += calcEaTBw(dir);
                int right = (short) readW(dir);
                int left = (short) d[dest].wl();
                int result = right * left;
                d[dest].l = result;
                cc.z = result == 0;
                cc.n = (result & 0x80000000) != 0;
                cc.v = false;
                cc.c = false;
                break;
            }
        }
    }

    private void groupD(int instruction) {
        int dir = instruction & 0x3f;
        int dest = (instruction >> 9) & 7;
        int orig = instruction & 7;

        switch ((instruction >> 6) & 7) {
            case 0x0: {  // add.b ea,Dn
                cycles += 4 + calcEaTBw(dir);
                int right = readB(dir);
                int left = d[dest].l0();
                int result = (right + left) & 0xffff;
                d[dest].setL0(result);
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.v = (((right ^ result) & (left ^ result)) & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                break;
            }
            case 0x1: {  // add.w ea,Dn
                cycles += 4 + calcEaTBw(dir);
                int right = readW(dir);
                int left = d[dest].wl();
                int result = right + left;
                d[dest].setWl(result);
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((right ^ result) & (left ^ result)) >> 8) & 0x80) != 0;
                cc.z = (result & 0xffff) == 0;
                break;
            }
            case 0x2: {  // add.l ea,Dn
                cycles += 6 + calcEaTL(dir);
                int right = readL(dir);
                int left = d[dest].l;
                int result = right + left;
                d[dest].l = result;
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ result) & (left ^ result)) >> 24) & 0x80) != 0;
                cc.c = ((((right & left) | (~result & (right | left))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
                break;
            }
            case 0x3: {  // adda.w
                cycles += 8 + calcEaTBw(dir);
                a[dest].l += (short) readW(dir);
                break;
            }
            case 0x4: {
                if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // addx.b
                    int right = 0;
                    int left = 0;
                    if ((dir >> 3) == 0) {
                        cycles += 4;
                        right = d[orig].l0();
                        left = d[dest].l0();
                    } else {
                        cycles += 18;
                        a[orig].l -= 1;
                        right = getbyte(a[orig].l);
                        a[dest].l -= 1;
                        left = getbyte(a[dest].l);
                    }
                    int result = (right + left + (cc.x ? 1 : 0)) & 0xffff;
                    if ((dir >> 3) == 0) d[dest].setL0(result);
                    else putbyte(a[dest].l, result);
                    cc.n = (result & 0x80) != 0;
                    cc.z = (result & 0xff) == 0;
                    cc.v = (((right ^ result) & (left ^ result)) & 0x80) != 0;
                    cc.c = (result & 0x100) != 0;
                    cc.x = cc.c;
                } else {  // add.b Dn,ea
                    cycles += 8 + calcEaTBw(dir);
                    int right = d[dest].l0();
                    int left = readB(dir);
                    int result = (right + left) & 0xffff;
                    writeB2(dir, result);
                    cc.n = (result & 0x80) != 0;
                    cc.z = (result & 0xff) == 0;
                    cc.v = (((right ^ result) & (left ^ result)) & 0x80) != 0;
                    cc.c = (result & 0x100) != 0;
                    cc.x = cc.c;
                }
                break;
            }
            case 0x5: {
                if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // addx.w
                    int right = 0;
                    int left = 0;
                    if ((dir >> 3) == 0) {
                        cycles += 4;
                        right = d[orig].wl();
                        left = d[dest].wl();
                    } else {
                        cycles += 18;
                        a[orig].l -= 2;
                        right = getword(a[orig].l);
                        a[dest].l -= 2;
                        left = getword(a[dest].l);
                    }
                    int result = right + left + (cc.x ? 1 : 0);
                    if ((dir >> 3) == 0) d[dest].setWl(result);
                    else putword(a[dest].l, result);
                    cc.n = (result & 0x8000) != 0;
                    cc.z = (result & 0xffff) == 0;
                    cc.v = (((right ^ result) & (left ^ result)) & 0x8000) != 0;
                    cc.c = (result & 0x10000) != 0;
                    cc.x = cc.c;
                } else {  // add.w Dn,ea
                    cycles += 8 + calcEaTBw(dir);
                    int right = d[dest].wl();
                    int left = readW(dir);
                    int result = right + left;
                    writeW2(dir, result);
                    cc.n = (result & 0x8000) != 0;
                    cc.c = (result & 0x10000) != 0;
                    cc.x = cc.c;
                    cc.v = ((((right ^ result) & (left ^ result)) >> 8) & 0x80) != 0;
                    cc.z = (result & 0xffff) == 0;
                }
                break;
            }
            case 0x6: {
                if ((dir >> 3) == 0 || (dir >> 3) == 1) {  // addx.l
                    int right = 0;
                    int left = 0;
                    if ((dir >> 3) == 0) {
                        cycles += 8;
                        right = d[orig].l;
                        left = d[dest].l;
                    } else {
                        cycles += 30;
                        a[orig].l -= 4;
                        right = (getword(a[orig].l) << 16) | getword(a[orig].l + 2);
                        a[dest].l -= 4;
                        left = (getword(a[dest].l) << 16) | getword(a[dest].l + 2);
                    }
                    int result = right + left + (cc.x ? 1 : 0);
                    if ((dir >> 3) == 0) {
                        d[dest].l = result;
                    } else {
                        putword(a[dest].l, (result >>> 16) & 0xffff);
                        putword(a[dest].l + 2, result & 0xffff);
                    }
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    cc.v = ((((right ^ result) & (left ^ result)) >> 24) & 0x80) != 0;
                    cc.c = ((((right & left) | (~result & (right | left))) >> 23) & 0x100) != 0;
                    cc.x = cc.c;
                } else {  // add.l Dn,ea
                    cycles += 12 + calcEaTL(dir);
                    int right = d[dest].l;
                    int left = readL(dir);
                    int result = right + left;
                    writeL2(dir, result);
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    cc.v = ((((right ^ result) & (left ^ result)) >> 24) & 0x80) != 0;
                    cc.c = ((((right & left) | (~result & (right | left))) >> 23) & 0x100) != 0;
                    cc.x = cc.c;
                }
                break;
            }
            default: {  // adda.l
                cycles += 6 + calcEaTL(dir);
                a[dest].l += readL(dir);
                break;
            }
        }
    }

    private void groupE(int instruction) {
        int dir = instruction & 0x3f;
        int orig = instruction & 7;

        if (((instruction >> 6) & 3) == 3) {  // memory shifts, always one bit
            cycles += 8 + calcEaTBw(dir);
            int value = readW(dir);
            int result = 0;
            switch ((instruction >> 8) & 0x0f) {
                case 0x0:  // asr.w
                    cc.c = (value & 1) != 0;
                    result = ((value >> 1) | (value & 0x8000)) & 0xffff;
                    cc.x = cc.c;
                    cc.v = false;
                    break;
                case 0x1:  // asl.w
                    result = (value << 1) & 0xffff;
                    cc.c = (value & 0x8000) != 0;
                    cc.x = cc.c;
                    cc.v = (value & 0xc000) != 0 && (value & 0xc000) != 0xc000;
                    break;
                case 0x2:  // lsr.w
                    cc.c = (value & 1) != 0;
                    result = (value >>> 1) & 0xffff;
                    cc.x = cc.c;
                    cc.v = false;
                    break;
                case 0x3:  // lsl.w
                    cc.c = (value & 0x8000) != 0;
                    result = (value << 1) & 0xffff;
                    cc.x = cc.c;
                    cc.v = false;
                    break;
                case 0x4:  // roxr.w
                    cc.c = (value & 1) != 0;
                    result = ((value >>> 1) | (cc.x ? 0x8000 : 0)) & 0xffff;
                    cc.x = cc.c;
                    cc.v = false;
                    break;
                case 0x5:  // roxl.w
                    cc.c = (value & 0x8000) != 0;
                    result = ((value << 1) | (cc.x ? 1 : 0)) & 0xffff;
                    cc.x = cc.c;
                    cc.v = false;
                    break;
                case 0x6:  // ror.w
                    cc.c = (value & 1) != 0;
                    result = ((value >>> 1) | ((value & 1) << 15)) & 0xffff;
                    cc.v = false;
                    break;
                case 0x7:  // rol.w
                    cc.c = (value & 0x8000) != 0;
                    result = ((value << 1) | (cc.c ? 1 : 0)) & 0xffff;
                    cc.v = false;
                    break;
                default: {
                    if (type == Type.M68020) {
                        fetchWord();
                        cycles += 12;
                        cc.n = false;
                        cc.z = true;
                        cc.v = false;
                        cc.c = false;
                        return;
                    }
                    illegal();
                    return;
                }
            }
            writeW2(dir, result);
            cc.n = (result & 0x8000) != 0;
            cc.z = result == 0;
            return;
        }

        int count = ((instruction >> 5) & 1) == 1
                ? (d[(instruction >> 9) & 7].l & 0x3f)
                : (((((instruction >> 9) & 7) - 1) & 7) + 1);
        cycles += count * 2;
        int op = (instruction >> 3) & 0x3f;
        int size = (op & 0x18) == 0x00 ? 8 : ((op & 0x18) == 0x08 ? 16 : 32);
        boolean leftShift = (op & 0x20) != 0;
        cycles += size == 32 ? 8 : 6;

        int value = size == 8 ? d[orig].l0() : (size == 16 ? d[orig].wl() : d[orig].l);
        int original = value;
        int mask = size == 32 ? 0xffffffff : (1 << size) - 1;
        int sign = 1 << (size - 1);
        boolean overflow = false;

        switch (op & 0x23) {
            case 0x00:  // asr
                for (int step = 0; step < count; ++step) {
                    cc.c = (value & 1) != 0;
                    cc.x = cc.c;
                    value = ((value & sign) != 0) ? (sign | (value >>> 1)) : (value >>> 1);
                }
                break;
            case 0x01:  // lsr
                for (int step = 0; step < count; ++step) {
                    cc.c = (value & 1) != 0;
                    cc.x = cc.c;
                    value >>>= 1;
                }
                break;
            case 0x02:  // roxr
                for (int step = 0; step < count; ++step) {
                    boolean carry = (value & 1) != 0;
                    value = ((value >>> 1) | (cc.x ? sign : 0)) & mask;
                    cc.c = carry;
                    cc.x = carry;
                }
                break;
            case 0x03:  // ror
                for (int step = 0; step < count; ++step) {
                    cc.c = (value & 1) != 0;
                    value = ((value >>> 1) | (cc.c ? sign : 0)) & mask;
                }
                break;
            case 0x20:  // asl
                for (int step = 0; step < count; ++step) {
                    cc.c = (value & sign) != 0;
                    cc.x = cc.c;
                    value = (value << 1) & mask;
                }
                if (count != 0) {
                    int maskOut = size == 8 ? kShift8[Math.min(count + 1, 64)]
                            : (size == 16 ? shift16(count + 1) : shift32(count + 1));
                    int bits = original & maskOut;
                    overflow = bits != 0 && bits != maskOut;
                }
                break;
            case 0x21:  // lsl
                for (int step = 0; step < count; ++step) {
                    cc.c = (value & sign) != 0;
                    cc.x = cc.c;
                    value = (value << 1) & mask;
                }
                break;
            case 0x22:  // roxl
                for (int step = 0; step < count; ++step) {
                    boolean carry = (value & sign) != 0;
                    value = ((value << 1) | (cc.x ? 1 : 0)) & mask;
                    cc.c = carry;
                    cc.x = carry;
                }
                break;
            default:  // rol
                for (int step = 0; step < count; ++step) {
                    cc.c = (value & sign) != 0;
                    value = ((value << 1) | (cc.c ? 1 : 0)) & mask;
                }
                break;
        }

        value &= mask;
        if (size == 8) d[orig].setL0(value);
        else if (size == 16) d[orig].setWl(value);
        else d[orig].l = value;
        cc.n = (value & sign) != 0;
        cc.z = value == 0;
        if (!leftShift && (op & 0x03) == 0x01) cc.n = false;
        cc.v = overflow;
    }

    private void group8(int instruction) {
        int dir = instruction & 0x3f;
        int dest = (instruction >> 9) & 7;
        int orig = instruction & 7;

        switch ((instruction >> 6) & 7) {
            case 0x0: {  // or.b ea,Dn
                cycles += 4 + calcEaTBw(dir);
                int result = (d[dest].l0() | readB(dir)) & 0xff;
                d[dest].setL0(result);
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
                break;
            }
            case 0x1: {  // or.w ea,Dn
                cycles += 4 + calcEaTBw(dir);
                int result = (d[dest].wl() | readW(dir)) & 0xffff;
                d[dest].setWl(result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
                break;
            }
            case 0x2: {  // or.l ea,Dn
                cycles += (dir >> 3) != 0 ? 6 + calcEaTL(dir) : 8;
                int result = d[dest].l | readL(dir);
                d[dest].l = result;
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
                break;
            }
            case 0x3: {  // divu
                cycles += (type == Type.M68010 ? 108 : 140) + calcEaTBw(dir);
                int divisor = readW(dir);
                if (divisor == 0) {
                    ppc_.l = pc_.l;
                    exception(5, 38);
                    break;
                }
                cc.c = false;
                int quotient = Integer.divideUnsigned(d[dest].l, divisor);
                if (Integer.compareUnsigned(quotient, 0x10000) < 0) {
                    cc.z = quotient == 0;
                    cc.n = (quotient & 0x8000) != 0;
                    cc.v = false;
                    int remainder = Integer.remainderUnsigned(d[dest].l, divisor);
                    d[dest].l = (quotient & 0xffff) | ((remainder & 0xffff) << 16);
                } else {
                    cc.v = true;
                }
                break;
            }
            case 0x4: {
                if ((dir >> 3) == 0) {  // sbcd Dy,Dx
                    cycles += 6;
                    int left = d[dest].l0();
                    int right = d[orig].l0();
                    int result = (left & 0x0f) - (right & 0x0f) - (cc.x ? 1 : 0);
                    int correction = Integer.compareUnsigned(result, 0x0f) > 0 ? 6 : 0;
                    result += (left & 0xf0) - (right & 0xf0);
                    cc.v = result != 0;
                    if (Integer.compareUnsigned(result, 0xff) > 0) {
                        result += 0xa0;
                        cc.x = true;
                        cc.c = true;
                    } else if (Integer.compareUnsigned(result, correction) < 0) {
                        cc.x = true;
                        cc.c = true;
                    } else {
                        cc.x = false;
                        cc.c = false;
                        cc.n = false;
                    }
                    result = (result - correction) & 0xff;
                    cc.z = result == 0;
                    cc.n = (result & 0x80) != 0;
                    cc.v = cc.v && cc.n;
                    d[dest].setL0(result);
                } else if ((dir >> 3) == 1) {  // sbcd -(Ay),-(Ax)
                    cycles += 18;
                    a[orig].l -= 1;
                    int right = getbyte(a[orig].l);
                    a[dest].l -= 1;
                    int left = getbyte(a[dest].l);
                    int result = (left & 0x0f) - (right & 0x0f) - (cc.x ? 1 : 0);
                    int correction = Integer.compareUnsigned(result, 0x0f) > 0 ? 6 : 0;
                    result += (left & 0xf0) - (right & 0xf0);
                    cc.v = result != 0;
                    if (Integer.compareUnsigned(result, 0xff) > 0) {
                        result += 0xa0;
                        cc.x = true;
                        cc.c = true;
                    } else if (Integer.compareUnsigned(result, correction) < 0) {
                        cc.x = true;
                        cc.c = true;
                    } else {
                        cc.x = false;
                        cc.c = false;
                    }
                    result = (result - correction) & 0xff;
                    cc.z = result == 0;
                    cc.n = (result & 0x80) != 0;
                    cc.v = cc.v && cc.n;
                    putbyte(a[dest].l, result);
                } else {  // or.b Dn,ea
                    cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 12;
                    int result = (readB(dir) | d[dest].l0()) & 0xff;
                    writeB2(dir, result);
                    cc.n = (result & 0x80) != 0;
                    cc.z = result == 0;
                    cc.c = false;
                    cc.v = false;
                }
                break;
            }
            case 0x5: {  // or.w Dn,ea
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 12;
                int result = (readW(dir) | d[dest].wl()) & 0xffff;
                writeW2(dir, result);
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
                break;
            }
            case 0x6: {  // or.l Dn,ea
                cycles += (dir >> 3) != 0 ? 12 + calcEaTL(dir) : 20;
                int result = readL(dir) | d[dest].l;
                writeL2(dir, result);
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.c = false;
                cc.v = false;
                break;
            }
            default: {  // divs
                cycles += (type == Type.M68010 ? 122 : 158) + calcEaTBw(dir);
                int divisor = (short) readW(dir);
                if (divisor == 0) {
                    ppc_.l = pc_.l;
                    exception(5, 38);
                    break;
                }
                int value = d[dest].l;
                cc.c = false;
                if (value == 0x80000000 && divisor == -1) {
                    cc.z = true;
                    cc.n = false;
                    cc.v = false;
                    d[dest].l = 0;
                } else {
                    int quotient = value / divisor;
                    int remainder = value % divisor;
                    if (quotient == (short) quotient) {
                        cc.z = quotient == 0;
                        cc.n = (quotient & 0x8000) != 0;
                        cc.v = false;
                        d[dest].l = (quotient & 0xffff) | (remainder << 16);
                    } else {
                        cc.v = true;
                    }
                }
                break;
            }
        }
    }

    private void group9(int instruction) {
        int dir = instruction & 0x3f;
        int dest = (instruction >> 9) & 7;
        int orig = instruction & 7;

        switch ((instruction >> 6) & 7) {
            case 0x0: {  // sub.b ea,Dn
                cycles += 4 + calcEaTBw(dir);
                int right = readB(dir);
                int left = d[dest].l0();
                int result = (left - right) & 0xffff;
                d[dest].setL0(result);
                cc.n = (result & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                break;
            }
            case 0x1: {  // sub.w ea,Dn
                cycles += 4 + calcEaTBw(dir);
                int right = readW(dir);
                int left = d[dest].wl();
                int result = left - right;
                d[dest].setWl(result);
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                cc.z = (result & 0xffff) == 0;
                break;
            }
            case 0x2: {  // sub.l ea,Dn
                cycles += 6 + calcEaTL(dir);
                int right = readL(dir);
                int left = d[dest].l;
                int result = left - right;
                d[dest].l = result;
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                cc.x = cc.c;
                break;
            }
            case 0x3: {  // suba.w
                cycles += 8 + calcEaTBw(dir);
                a[dest].l -= (short) readW(dir);
                break;
            }
            case 0x4: {
                if ((dir >> 3) == 0) {  // subx.b Dy,Dx
                    cycles += 4;
                    int right = d[orig].l0();
                    int left = d[dest].l0();
                    int result = (left - right - (cc.x ? 1 : 0)) & 0xffff;
                    d[dest].setL0(result);
                    cc.n = (result & 0x80) != 0;
                    cc.c = (result & 0x100) != 0;
                    cc.x = cc.c;
                    cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                    cc.z = (result & 0xff) == 0;
                } else if ((dir >> 3) == 1) {  // subx.b -(Ay),-(Ax)
                    cycles += 18;
                    a[orig].l -= 1;
                    int right = getbyte(a[orig].l);
                    a[dest].l -= 1;
                    int left = getbyte(a[dest].l);
                    int result = (left - right - (cc.x ? 1 : 0)) & 0xffff;
                    putbyte(a[dest].l, result);
                    cc.n = (result & 0x80) != 0;
                    cc.c = (result & 0x100) != 0;
                    cc.x = cc.c;
                    cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                    cc.z = (result & 0xff) == 0;
                } else {  // sub.b Dn,ea
                    cycles += 8 + calcEaTBw(dir);
                    int right = d[dest].l0();
                    int left = readB(dir);
                    int result = (left - right) & 0xffff;
                    writeB2(dir, result);
                    cc.n = (result & 0x80) != 0;
                    cc.c = (result & 0x100) != 0;
                    cc.x = cc.c;
                    cc.v = (((right ^ left) & (result ^ left)) & 0x80) != 0;
                    cc.z = (result & 0xff) == 0;
                }
                break;
            }
            case 0x5: {
                if ((dir >> 3) == 0) {  // subx.w Dy,Dx
                    cycles += 4;
                    int right = d[orig].wl();
                    int left = d[dest].wl();
                    int result = left - right - (cc.x ? 1 : 0);
                    d[dest].setWl(result);
                    cc.n = (result & 0x8000) != 0;
                    cc.c = (result & 0x10000) != 0;
                    cc.x = cc.c;
                    cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                    cc.z = (result & 0xffff) == 0;
                } else if ((dir >> 3) == 1) {  // subx.w -(Ay),-(Ax)
                    cycles += 18;
                    a[orig].l -= 2;
                    int right = getword(a[orig].l);
                    a[dest].l -= 2;
                    int left = getword(a[dest].l);
                    int result = left - right - (cc.x ? 1 : 0);
                    putword(a[dest].l, result);
                    cc.n = (result & 0x8000) != 0;
                    cc.c = (result & 0x10000) != 0;
                    cc.x = cc.c;
                    cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                    cc.z = (result & 0xffff) == 0;
                } else {  // sub.w Dn,ea
                    cycles += 8 + calcEaTBw(dir);
                    int right = d[dest].wl();
                    int left = readW(dir);
                    int result = left - right;
                    writeW2(dir, result);
                    cc.n = (result & 0x8000) != 0;
                    cc.c = (result & 0x10000) != 0;
                    cc.x = cc.c;
                    cc.v = ((((right ^ left) & (result ^ left)) >> 8) & 0x80) != 0;
                    cc.z = (result & 0xffff) == 0;
                }
                break;
            }
            case 0x6: {
                if ((dir >> 3) == 0) {  // subx.l Dy,Dx
                    cycles += 8;
                    int right = d[orig].l;
                    int left = d[dest].l;
                    int result = left - right - (cc.x ? 1 : 0);
                    d[dest].l = result;
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                    cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                    cc.x = cc.c;
                } else if ((dir >> 3) == 1) {  // subx.l -(Ay),-(Ax)
                    cycles += 30;
                    a[orig].l -= 4;
                    int right = (getword(a[orig].l) << 16) | getword(a[orig].l + 2);
                    a[dest].l -= 4;
                    int left = (getword(a[dest].l) << 16) | getword(a[dest].l + 2);
                    int result = left - right - (cc.x ? 1 : 0);
                    putword(a[dest].l, (result >>> 16) & 0xffff);
                    putword(a[dest].l + 2, result & 0xffff);
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                    cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                    cc.x = cc.c;
                } else {  // sub.l Dn,ea
                    cycles += 12 + calcEaTL(dir);
                    int right = d[dest].l;
                    int left = readL(dir);
                    int result = left - right;
                    writeL2(dir, result);
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    cc.v = ((((right ^ left) & (result ^ left)) >> 24) & 0x80) != 0;
                    cc.c = ((((right & result) | (~left & (right | result))) >> 23) & 0x100) != 0;
                    cc.x = cc.c;
                }
                break;
            }
            default: {  // suba.l
                cycles += 6 + calcEaTL(dir);
                a[dest].l -= readL(dir);
                break;
            }
        }
    }

    private void group5(int instruction) {
        int dir = instruction & 0x3f;
        int orig = instruction & 7;
        int quick = (((((instruction >> 9) & 7) - 1) & 7) + 1);

        switch ((instruction >> 6) & 7) {
            case 0x0: {  // addq.b
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                int value = readB(dir);
                int result = (quick + value) & 0xffff;
                writeB2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.v = (((quick ^ result) & (value ^ result)) & 0x80) != 0;
                break;
            }
            case 0x1: {  // addq.w
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                if ((dir >> 3) != 1) {
                    int value = readW(dir);
                    int result = value + quick;
                    writeW2(dir, result);
                    cc.n = (result & 0x8000) != 0;
                    cc.z = (result & 0xffff) == 0;
                    cc.c = (result & 0x10000) != 0;
                    cc.x = cc.c;
                    cc.v = ((((quick ^ result) & (value ^ result)) >> 8) & 0x80) != 0;
                } else {
                    a[orig].l += quick;
                }
                break;
            }
            case 0x2: {  // addq.l
                cycles += (dir >> 3) != 0 ? 12 + calcEaTL(dir) : 8;
                if ((dir >> 3) != 1) {
                    int value = readL(dir);
                    int result = value + quick;
                    writeL2(dir, result);
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    cc.c = ((((quick & value) | (~result & (quick | value))) >> 23) & 0x100) != 0;
                    cc.x = cc.c;
                    cc.v = ((((quick ^ result) & (value ^ result)) >> 24) & 0x80) != 0;
                } else {
                    a[orig].l += quick;
                }
                break;
            }
            case 0x3: case 0x7: {
                if (((dir >> 3) & 7) == 1) {  // dbcc
                    cycles += 12;
                    if (!condition((instruction >> 8) & 0x0f)) {
                        d[orig].setWl(d[orig].wl() - 1);
                        if (d[orig].wl() != 0xffff) {
                            cycles -= 2;
                            pc_.l += (short) progWord(pc_.l);
                        } else {
                            pc_.l += 2;
                        }
                    } else {
                        pc_.l += 2;
                    }
                } else {  // scc
                    cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                    writeB(dir, condition((instruction >> 8) & 0x0f) ? 0xff : 0x00);
                }
                break;
            }
            case 0x4: {  // subq.b
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                int value = readB(dir);
                int result = (value - quick) & 0xffff;
                writeB2(dir, result);
                cc.n = (result & 0x80) != 0;
                cc.z = (result & 0xff) == 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.v = (((quick ^ value) & (result ^ value)) & 0x80) != 0;
                break;
            }
            case 0x5: {  // subq.w
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                if ((dir >> 3) != 1) {
                    int value = readW(dir);
                    int result = value - quick;
                    writeW2(dir, result);
                    cc.n = (result & 0x8000) != 0;
                    cc.z = (result & 0xffff) == 0;
                    cc.c = (result & 0x10000) != 0;
                    cc.x = cc.c;
                    cc.v = ((((quick ^ value) & (result ^ value)) >> 8) & 0x80) != 0;
                } else {
                    a[orig].l -= quick;
                }
                break;
            }
            default: {  // subq.l
                cycles += (dir >> 3) != 0 ? 12 + calcEaTL(dir) : 8;
                if ((dir >> 3) != 1) {
                    int value = readL(dir);
                    int result = value - quick;
                    writeL2(dir, result);
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    cc.v = ((((quick ^ value) & (result ^ value)) >> 24) & 0x80) != 0;
                    cc.c = ((((quick & result) | (~value & (quick | result))) >> 23) & 0x100) != 0;
                    cc.x = cc.c;
                } else {
                    a[orig].l -= quick;
                }
                break;
            }
        }
    }

    private void group6(int instruction) {
        int offset = instruction & 0xff;
        int code = (instruction >> 8) & 0x0f;
        if (code == 1) {
            cycles += 18;
            if (offset == 0x00) {
                int displacement = progWord(pc_.l);
                a[7].l -= 4;
                putword(a[7].l, ((pc_.l + 2) >>> 16) & 0xffff);
                putword(a[7].l + 2, (pc_.l + 2) & 0xffff);
                pc_.l += (short) displacement;
            } else if (offset == 0xff && type != Type.M68000) {
                int hi = progWord(pc_.l), lo = progWord(pc_.l + 2);
                int ret = pc_.l + 4;
                a[7].l -= 4;
                putword(a[7].l, (ret >>> 16) & 0xffff);
                putword(a[7].l + 2, ret & 0xffff);
                pc_.l = pc_.l + ((hi << 16) | lo);
            } else {
                a[7].l -= 4;
                putword(a[7].l, (pc_.l >>> 16) & 0xffff);
                putword(a[7].l + 2, pc_.l & 0xffff);
                pc_.l += (byte) offset;
            }
            return;
        }
        if (condition(code)) {
            cycles += 10;
            if (offset == 0x00) pc_.l += (short) progWord(pc_.l);
            else if (offset == 0xff && type != Type.M68000) {
                int hi = progWord(pc_.l), lo = progWord(pc_.l + 2);
                pc_.l = pc_.l + ((hi << 16) | lo);
            } else pc_.l += (byte) offset;
        } else {
            cycles += 8;
            if (offset == 0x00) pc_.l += 2;
            else if (offset == 0xff && type != Type.M68000) pc_.l += 4;
        }
    }

    private void group7(int instruction) {
        cycles += 4;
        if ((instruction & 0xFF00) == 0x7100 && emulOpHandler != null) {
            emulOpHandler.onEmulOp(instruction);
            return;
        }
        int dest = (instruction >> 9) & 7;
        d[dest].l = (byte) (instruction & 0xff);
        cc.c = false;
        cc.v = false;
        cc.z = d[dest].l == 0;
        cc.n = (d[dest].l & 0x80000000) != 0;
    }

    private void moveBw(int instruction, boolean byteSize) {
        int dir = instruction & 0x3f;
        int target = ((instruction >> 9) & 7) | (((instruction >> 6) & 7) << 3);
        if (byteSize) {
            int value = readB(dir);
            writeB(target, value);
            cycles += calcMoveT(dir, target, false);
            cc.n = (value & 0x80) != 0;
            cc.z = value == 0;
        } else {
            int value = readW(dir);
            writeW(target, value);
            cycles += calcMoveT(dir, target, false);
            cc.n = (value & 0x8000) != 0;
            cc.z = value == 0;
        }
        cc.v = false;
        cc.c = false;
    }

    private void moveL(int instruction) {
        int dir = instruction & 0x3f;
        int target = ((instruction >> 9) & 7) | (((instruction >> 6) & 7) << 3);
        int value = readL(dir);
        writeL(target, value);
        cycles += calcMoveT(dir, target, true);
        cc.v = false;
        cc.c = false;
        cc.n = (value & 0x80000000) != 0;
        cc.z = value == 0;
    }

    private void group1(int instruction) {
        moveBw(instruction, true);
    }

    private void group2(int instruction) {
        int dir = instruction & 0x3f;
        if (((instruction >> 6) & 7) == 1) {  // movea.l
            cycles += (dir >> 3) > 1 ? 4 + calcEaTL(dir) : 4;
            a[(instruction >> 9) & 7].l = readL(dir);
        } else {
            moveL(instruction);
        }
    }

    private void group3(int instruction) {
        int dir = instruction & 0x3f;
        if (((instruction >> 6) & 7) == 1) {  // movea.w
            cycles += (dir >> 3) > 1 ? 4 + calcEaTBw(dir) : 4;
            a[(instruction >> 9) & 7].l = (short) readW(dir);
        } else {
            moveBw(instruction, false);
        }
    }

    private int readCr(int c) {
        switch (c) {
            case 0x800:
            case 0x803:
                return otherSp_.l;
            case 0x801:
                return vbr;
            case 0x804:
                return a[7].l;
            default:
                return 0;
        }
    }

    private void writeCr(int c, int v) {
        switch (c) {
            case 0x800:
            case 0x803:
                otherSp_.l = v;
                break;
            case 0x801:
                vbr = v & ~3;
                break;
            case 0x804:
                a[7].l = v;
                break;
            default:
                break;
        }
    }

    private void group4(int instruction) {
        int dir = instruction & 0x3f;
        int dest = (instruction >> 9) & 7;
        int orig = instruction & 7;
        int op = (instruction >> 6) & 0x3f;

        switch (op) {
            case 0x00: {  // negx.b
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                int value = readB(dir);
                int result = (0 - value - (cc.x ? 1 : 0)) & 0xffff;
                cc.n = (result & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                if ((result & 0xff) != 0) cc.z = false;
                cc.v = ((value & result) & 0x80) != 0;
                writeB2(dir, result);
                break;
            }
            case 0x01: {  // negx.w
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                int value = readW(dir);
                int result = 0 - value - (cc.x ? 1 : 0);
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                if ((result & 0xffff) != 0) cc.z = false;
                cc.v = (((value & result) >> 8) & 0x80) != 0;
                writeW2(dir, result);
                break;
            }
            case 0x02: {  // negx.l
                cycles += (dir >> 3) != 0 ? 12 + calcEaTL(dir) : 6;
                int value = readL(dir);
                int result = 0 - value - (cc.x ? 1 : 0);
                cc.n = (result & 0x80000000) != 0;
                cc.c = ((((value & result) | (value | result)) >> 23) & 0x100) != 0;
                cc.x = cc.c;
                cc.z = result == 0;
                cc.v = (((value & result) >> 24) & 0x80) != 0;
                writeL2(dir, result);
                break;
            }
            case 0x03: {  // move from sr
                if (type == Type.M68000) {
                    cycles += (dir >> 3) == 0 ? 6 : 8 + calcEaTBw(dir);
                    writeW(dir, getFlags());
                } else if (checkSupervisor()) {
                    cycles += (dir >> 3) == 0 ? 4 : 8 + calcEaTBw(dir);
                    writeW(dir, getFlags());
                }
                break;
            }
            case 0x06: case 0x0e: case 0x16: case 0x1e:
            case 0x26: case 0x2e: case 0x36: case 0x3e: {  // chk
                cycles += 10 + calcEaTBw(dir);
                int bound = (short) readW(dir);
                int value = (short) d[dest].wl();
                cc.n = value < 0;
                if (value < 0 || value > bound) {
                    ppc_.l = pc_.l;
                    exception(6, 30);
                }
                break;
            }
            case 0x07: case 0x0f: case 0x17: case 0x1f:
            case 0x27: case 0x2f: case 0x37: case 0x3f: {  // lea
                a[dest].l = readEa(dir);
                if (dir <= 0x17) cycles += 4;
                else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles += 8;
                else if ((dir >= 0x30 && dir <= 0x37) || dir == 0x3b || dir == 0x39) cycles += 12;
                break;
            }
            case 0x08: {  // clr.b
                if (type == Type.M68000) cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                else cycles += 4 + calcEaTBw(dir);
                writeB(dir, 0);
                cc.n = false;
                cc.v = false;
                cc.c = false;
                cc.z = true;
                break;
            }
            case 0x09: {  // clr.w
                if (type == Type.M68000) cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                else cycles += 4 + calcEaTBw(dir);
                writeW(dir, 0);
                cc.n = false;
                cc.v = false;
                cc.c = false;
                cc.z = true;
                break;
            }
            case 0x0a: {  // clr.l
                if (type == Type.M68000) cycles += (dir >> 3) != 0 ? 12 + calcEaTL(dir) : 6;
                else cycles += (dir >> 3) != 0 ? 4 + calcEaTL(dir) : 6;
                writeL(dir, 0);
                cc.n = false;
                cc.v = false;
                cc.c = false;
                cc.z = true;
                break;
            }
            case 0x10: {  // neg.b
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                int value = readB(dir);
                int result = (0 - value) & 0xffff;
                cc.n = (result & 0x80) != 0;
                cc.c = (result & 0x100) != 0;
                cc.x = cc.c;
                cc.z = (result & 0xff) == 0;
                cc.v = ((value & result) & 0x80) != 0;
                writeB2(dir, result);
                break;
            }
            case 0x11: {  // neg.w
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                int value = readW(dir);
                int result = 0 - value;
                cc.n = (result & 0x8000) != 0;
                cc.c = (result & 0x10000) != 0;
                cc.x = cc.c;
                cc.z = (result & 0xffff) == 0;
                cc.v = (((value & result) >> 8) & 0x80) != 0;
                writeW2(dir, result);
                break;
            }
            case 0x12: {  // neg.l
                cycles += (dir >> 3) != 0 ? 12 + calcEaTL(dir) : 6;
                int value = readL(dir);
                int result = 0 - value;
                cc.n = (result & 0x80000000) != 0;
                cc.c = ((((value & result) | (value | result)) >> 23) & 0x100) != 0;
                cc.x = cc.c;
                cc.z = result == 0;
                cc.v = (((value & result) >> 24) & 0x80) != 0;
                writeL2(dir, result);
                break;
            }
            case 0x13: {  // move to ccr
                cycles += 12 + calcEaTBw(dir);
                int value = readW(dir);
                cc.x = (value & 0x10) != 0;
                cc.n = (value & 0x08) != 0;
                cc.z = (value & 0x04) != 0;
                cc.v = (value & 0x02) != 0;
                cc.c = (value & 0x01) != 0;
                break;
            }
            case 0x18: {  // not.b
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                int result = (~readB(dir)) & 0xff;
                writeB2(dir, result);
                cc.c = false;
                cc.v = false;
                cc.n = (result & 0x80) != 0;
                cc.z = result == 0;
                break;
            }
            case 0x19: {  // not.w
                cycles += (dir >> 3) != 0 ? 8 + calcEaTBw(dir) : 4;
                int result = (~readW(dir)) & 0xffff;
                writeW2(dir, result);
                cc.c = false;
                cc.v = false;
                cc.n = (result & 0x8000) != 0;
                cc.z = result == 0;
                break;
            }
            case 0x1a: {  // not.l
                cycles += (dir >> 3) != 0 ? 12 + calcEaTL(dir) : 6;
                int result = ~readL(dir);
                writeL2(dir, result);
                cc.c = false;
                cc.v = false;
                cc.n = (result & 0x80000000) != 0;
                cc.z = result == 0;
                break;
            }
            case 0x1b: {  // move to sr
                if (checkSupervisor()) {
                    cycles += 12 + calcEaTBw(dir);
                    setFlags(readW(dir));
                }
                break;
            }
            case 0x21: {  // swap / pea
                if (dir <= 0x07) {  // swap
                    cycles += 4;
                    int result = (d[orig].wl() << 16) | d[orig].wh();
                    cc.c = false;
                    cc.v = false;
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    d[orig].l = result;
                } else {  // pea
                    int address = readEa(dir);
                    if (dir <= 0x17) cycles += 12;
                    else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles += 16;
                    else if (dir >= 0x30 && dir <= 0x37) cycles += 20;
                    else if (dir == 0x39) cycles += 20;
                    a[7].l -= 4;
                    putword(a[7].l, (address >>> 16) & 0xffff);
                    putword(a[7].l + 2, address & 0xffff);
                }
                break;
            }
            case 0x22: {  // ext.w / movem.w register to memory
                if ((dir >> 3) == 0) {
                    cycles += 4;
                    int result = (short) (byte) d[orig].l0() & 0xffff;
                    cc.c = false;
                    cc.v = false;
                    cc.n = (result & 0x8000) != 0;
                    cc.z = result == 0;
                    d[orig].setWl(result);
                } else {
                    int mask = fetchWord();
                    int count = 0;
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1 << bit)) != 0) ++count;
                    }
                    cycles += count << 2;
                    if (dir <= 0x27) cycles += 12;
                    else if (dir <= 0x2f || dir == 0x38) cycles += 16;
                    else if (dir <= 0x37) cycles += 18;
                    else if (dir == 0x39) cycles += 20;
                    if (dir >= 0x20 && dir <= 0x27) {
                        for (int bit = 0; bit < 16; ++bit) {
                            if ((mask & (1 << bit)) == 0) continue;
                            int index = 15 - bit;
                            writeW(dir, index < 8 ? d[index].wl() : a[index - 8].wl());
                        }
                    } else {
                        int address = readEa(dir);
                        for (int bit = 0; bit < 16; ++bit) {
                            if ((mask & (1 << bit)) == 0) continue;
                            putword(address, bit < 8 ? d[bit].wl() : a[bit - 8].wl());
                            address += 2;
                        }
                    }
                }
                break;
            }
            case 0x23: {  // ext.l / movem.l register to memory
                if ((dir >> 3) == 0) {
                    cycles += 4;
                    int result = (short) d[orig].wl();
                    cc.c = false;
                    cc.v = false;
                    cc.n = (result & 0x80000000) != 0;
                    cc.z = result == 0;
                    d[orig].l = result;
                } else {
                    int mask = fetchWord();
                    int count = 0;
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1 << bit)) != 0) ++count;
                    }
                    cycles += count << 3;
                    if (dir <= 0x27) cycles += 8;
                    else if (dir <= 0x2f || dir == 0x38) cycles += 12;
                    else if (dir <= 0x37) cycles += 14;
                    else if (dir == 0x39) cycles += 16;
                    if (dir >= 0x20 && dir <= 0x27) {
                        for (int bit = 0; bit < 16; ++bit) {
                            if ((mask & (1 << bit)) == 0) continue;
                            int index = 15 - bit;
                            writeL(dir, index < 8 ? d[index].l : a[index - 8].l);
                        }
                    } else {
                        int address = readEa(dir);
                        for (int bit = 0; bit < 16; ++bit) {
                            if ((mask & (1 << bit)) == 0) continue;
                            int value = bit < 8 ? d[bit].l : a[bit - 8].l;
                            putword(address, (value >>> 16) & 0xffff);
                            putword(address + 2, value & 0xffff);
                            address += 4;
                        }
                    }
                }
                break;
            }
            case 0x28: {  // tst.b
                cycles += 4 + calcEaTBw(dir);
                int value = readB(dir);
                cc.v = false;
                cc.c = false;
                cc.n = (value & 0x80) != 0;
                cc.z = value == 0;
                break;
            }
            case 0x29: {  // tst.w
                cycles += 4 + calcEaTBw(dir);
                int value = readW(dir);
                cc.v = false;
                cc.c = false;
                cc.n = (value & 0x8000) != 0;
                cc.z = value == 0;
                break;
            }
            case 0x2a: {  // tst.l
                cycles += 4 + calcEaTL(dir);
                int value = readL(dir);
                cc.v = false;
                cc.c = false;
                cc.n = (value & 0x80000000) != 0;
                cc.z = value == 0;
                break;
            }
            case 0x2b: {  // tas
                cycles += (dir >> 3) != 0 ? 14 + calcEaTBw(dir) : 4;
                int value = readB(dir);
                cc.z = value == 0;
                cc.n = (value & 0x80) != 0;
                cc.v = false;
                cc.c = false;
                writeB2(dir, value | 0x80);
                break;
            }
            case 0x30: {
                if (type != Type.M68020) {
                    illegal();
                    break;
                }
                int ext = fetchWord();
                int dq = ext & 7;
                int dr = (ext >> 12) & 7;
                boolean size64 = (ext & 0x0400) != 0;
                boolean isSigned = (ext & 0x0800) != 0;
                int eaVal = (dir <= 7) ? d[dir].l : readL(dir);
                cycles += 40;
                if (isSigned) {
                    long r = (long) d[dq].l * (long) eaVal;
                    d[dq].l = (int) r;
                    if (size64) d[dr].l = (int) (r >>> 32);
                    cc.n = r < 0;
                    cc.z = r == 0;
                } else {
                    long r = (d[dq].l & 0xffffffffL) * (eaVal & 0xffffffffL);
                    d[dq].l = (int) r;
                    if (size64) d[dr].l = (int) (r >>> 32);
                    cc.n = size64 ? ((d[dr].l & 0x80000000) != 0) : ((d[dq].l & 0x80000000) != 0);
                    cc.z = r == 0;
                }
                cc.v = false;
                cc.c = false;
                break;
            }
            case 0x32: {  // movem.w memory to register
                int mask = fetchWord();
                int count = 0;
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1 << bit)) != 0) ++count;
                }
                cycles += count << 2;
                if (dir <= 0x1f) cycles += 12;
                else if (dir <= 0x2f || dir == 0x38 || dir == 0x3a) cycles += 16;
                else if (dir <= 0x37 || dir == 0x3b) cycles += 18;
                else if (dir == 0x39) cycles += 20;
                if (dir >= 0x18 && dir <= 0x1f) {
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1 << bit)) == 0) continue;
                        int value = (short) readW(dir);
                        if (bit < 8) d[bit].l = value;
                        else a[bit - 8].l = value;
                    }
                } else {
                    int address = readEa(dir);
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1 << bit)) == 0) continue;
                        int value = (short) getword(address);
                        if (bit < 8) d[bit].l = value;
                        else a[bit - 8].l = value;
                        address += 2;
                    }
                }
                break;
            }
            case 0x33: {  // movem.l memory to register
                int mask = fetchWord();
                int count = 0;
                for (int bit = 0; bit < 16; ++bit) {
                    if ((mask & (1 << bit)) != 0) ++count;
                }
                cycles += count << 3;
                if (dir <= 0x1f) cycles += 12;
                else if (dir <= 0x2f || dir == 0x38 || dir == 0x3a) cycles += 16;
                else if (dir <= 0x37 || dir == 0x3b) cycles += 18;
                else if (dir == 0x39) cycles += 20;
                if (dir >= 0x18 && dir <= 0x1f) {
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1 << bit)) == 0) continue;
                        int value = readL(dir);
                        if (bit < 8) d[bit].l = value;
                        else a[bit - 8].l = value;
                    }
                } else {
                    int address = readEa(dir);
                    for (int bit = 0; bit < 16; ++bit) {
                        if ((mask & (1 << bit)) == 0) continue;
                        int value = (getword(address) << 16) | getword(address + 2);
                        if (bit < 8) d[bit].l = value;
                        else a[bit - 8].l = value;
                        address += 4;
                    }
                }
                break;
            }
            case 0x39: {  // trap, link, unlk, usp, reset, nop, stop, rte, rts, rtr
                if (dir <= 0x0f) {  // trap
                    cycles += 38;
                    int flags = getFlags();
                    setFlags(flags | 0x2000);
                    cc.t = false;
                    a[7].l -= 6;
                    putword(a[7].l + 4, pc_.wl());
                    putword(a[7].l + 2, pc_.wh());
                    putword(a[7].l, flags);
                    opcode = false;
                    int vecAddr = vbr + 0x80 + (instruction & 0x0f) * 4;
                    pc_.setWh(getword(vecAddr));
                    pc_.setWl(getword(vecAddr + 2));
                    opcode = true;
                } else if (dir <= 0x17) {  // link
                    cycles += 16;
                    int displacement = (short) fetchWord();
                    a[7].l -= 4;
                    putword(a[7].l, a[orig].wh());
                    putword(a[7].l + 2, a[orig].wl());
                    a[orig].l = a[7].l;
                    a[7].l += displacement;
                } else if (dir <= 0x1f) {  // unlk
                    cycles += 12;
                    a[7].l = a[orig].l;
                    a[orig].setWh(getword(a[7].l));
                    a[orig].setWl(getword(a[7].l + 2));
                    a[7].l += 4;
                } else if (dir <= 0x2f) {  // move usp
                    cycles += 4;
                    if (checkSupervisor()) {
                        if (((dir >> 3) & 1) == 1) a[orig].l = otherSp_.l;
                        else otherSp_.l = a[orig].l;
                    }
                } else {
                    switch (dir) {
                        case 0x30:  // reset
                            if (checkSupervisor()) {
                                cycles += 40;
                                if (resetInstructionHandler != null) resetInstructionHandler.onResetInstruction();
                            }
                            break;
                        case 0x31:  // nop
                            cycles += 4;
                            break;
                        case 0x32:  // stop
                            if (checkSupervisor()) {
                                setFlags(fetchWord());
                                cycles += 4;
                                halted = true;
                            }
                            break;
                        case 0x33:  // rte
                            if (checkSupervisor()) {
                                if (type == Type.M68000) {
                                    cycles += 20;
                                    int flags = getword(a[7].l);
                                    pc_.setWh(getword(a[7].l + 2));
                                    pc_.setWl(getword(a[7].l + 4));
                                    a[7].l += 6;
                                    setFlags(flags);
                                } else {
                                    cycles += 24;
                                    int flags = getword(a[7].l);
                                    pc_.setWh(getword(a[7].l + 2));
                                    pc_.setWl(getword(a[7].l + 4));
                                    a[7].l += 8;
                                    setFlags(flags);
                                }
                                if (rteHandler != null) rteHandler.onRte();
                            }
                            break;
                        case 0x35:  // rts
                            cycles += 16;
                            pc_.setWh(getword(a[7].l));
                            pc_.setWl(getword(a[7].l + 2));
                            a[7].l += 4;
                            break;
                        case 0x36:  // trapv
                            if (cc.v) {
                                cycles += 34;
                                int flags = getFlags();
                                setFlags(flags | 0x2000);
                                cc.t = false;
                                if (type != Type.M68000) {
                                    a[7].l -= 2;
                                    putword(a[7].l, 7 << 2);
                                }
                                a[7].l -= 6;
                                putword(a[7].l + 4, pc_.wl());
                                putword(a[7].l + 2, pc_.wh());
                                putword(a[7].l, flags);
                                opcode = false;
                                int vecAddr = vbr + 7 * 4;
                                pc_.setWh(getword(vecAddr));
                                pc_.setWl(getword(vecAddr + 2));
                                opcode = true;
                            } else {
                                cycles += 4;
                            }
                            break;
                        case 0x37:  // rtr
                            cycles += 20;
                            setFlags(getword(a[7].l));
                            pc_.setWh(getword(a[7].l + 2));
                            pc_.setWl(getword(a[7].l + 4));
                            a[7].l += 6;
                            break;
                        case 0x3a:
                        case 0x3b: {
                            if (type == Type.M68000) {
                                illegal();
                                break;
                            }
                            if (!checkSupervisor()) break;
                            cycles += 12;
                            int ext = fetchWord();
                            int reg = (ext >> 12) & 0xf;
                            int cr = ext & 0xfff;
                            if ((instruction & 1) == 0) {
                                int v = readCr(cr);
                                if ((reg & 8) != 0) a[reg & 7].l = v;
                                else d[reg & 7].l = v;
                            } else {
                                int v = ((reg & 8) != 0) ? a[reg & 7].l : d[reg & 7].l;
                                writeCr(cr, v);
                            }
                            break;
                        }
                        default:
                            illegal();
                            break;
                    }
                }
                break;
            }
            case 0x3a: {  // jsr
                int address = readEa(dir);
                a[7].l -= 4;
                putword(a[7].l, pc_.wh());
                putword(a[7].l + 2, pc_.wl());
                pc_.l = address;
                if (dir <= 0x17) cycles += 16;
                else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles += 18;
                else if ((dir >= 0x30 && dir <= 0x37) || dir == 0x3b) cycles += 22;
                else if (dir == 0x39) cycles += 20;
                break;
            }
            case 0x3b: {  // jmp
                pc_.l = readEa(dir);
                if (dir <= 0x17) cycles += 8;
                else if ((dir >= 0x28 && dir <= 0x2f) || dir == 0x38 || dir == 0x3a) cycles += 10;
                else if ((dir >= 0x30 && dir <= 0x37) || dir == 0x3b) cycles += 14;
                else if (dir == 0x39) cycles += 12;
                break;
            }
            default:
                illegal();
                break;
        }
    }

    private void groupA(int instruction) {
        cycles += 34;
        if (alineHandler != null && alineHandler.onAline(instruction, ppc_.l)) return;
        exception(10, 34);
    }

    private void groupF(int instruction) {
        // F-line opcodes trap unconditionally (vector 11); length is the handler's job.
        exception(11, 34);
    }

    private boolean takeIrq() {
        for (int level = 7; level >= 1; --level) {
            if (cc.im >= level || irq[level] == IrqLine.CLEAR) continue;
            halted = false;
            cycles += 44;
            int flags = getFlags();
            setFlags(flags | 0x2000);
            cc.t = false;
            if (type != Type.M68000) {
                a[7].l -= 2;
                putword(a[7].l, level << 2);
            }
            a[7].l -= 6;
            putword(a[7].l, flags);
            putword(a[7].l + 2, pc_.wh());
            putword(a[7].l + 4, pc_.wl());
            if (irqTakenHandler != null) irqTakenHandler.onIrqTaken(level);
            opcode = false;
            int vecAddr = 0x64 + ((level - 1) * 4);
            if (irqAck != null) {
                int vec = irqAck.acknowledge(level);
                if (vec >= 0) vecAddr = vec * 4;
            }
            pc_.setWh(getword(vbr + vecAddr));
            pc_.setWl(getword(vbr + vecAddr + 2));
            opcode = true;
            if (irq[level] == IrqLine.HOLD) irq[level] = IrqLine.CLEAR;
            cc.im = level;
            return true;
        }
        return false;
    }

    public int run(int limit) {
        cycles = 0;
        while (cycles < limit) {
            if (resetRequest != IrqLine.CLEAR) {
                IrqLine request = resetRequest;
                reset();
                if (request == IrqLine.ASSERT) resetRequest = IrqLine.ASSERT;
                cycles = limit;
                break;
            }
            if (haltRequest != IrqLine.CLEAR) {
                cycles += 4;
                if (cycleHandler != null) cycleHandler.onCycles(4);
                continue;
            }

            int start = cycles;
            takeIrq();
            if (halted) {
                cycles += 4;
                if (cycleHandler != null) cycleHandler.onCycles(4);
                continue;
            }

            ppc_.l = pc_.l;
            if ((pc_.l & 1) != 0) {
                exception(3, 34);
                continue;
            }
            boolean tracing = cc.t;
            int exceptionsBefore = exceptions;
            if (instructionHook != null) instructionHook.beforeOpcode(pc_.l);
            if (prefetch && (pqCount == 0 || pqAddr != pc_.l)) pqCount = 0;
            int instruction = fetchWord();
            switch (instruction >> 12) {
                case 0x0: group0(instruction); break;
                case 0x1: group1(instruction); break;
                case 0x2: group2(instruction); break;
                case 0x3: group3(instruction); break;
                case 0x4: group4(instruction); break;
                case 0x5: group5(instruction); break;
                case 0x6: group6(instruction); break;
                case 0x7: group7(instruction); break;
                case 0x8: group8(instruction); break;
                case 0x9: group9(instruction); break;
                case 0xa: groupA(instruction); break;
                case 0xb: groupB(instruction); break;
                case 0xc: groupC(instruction); break;
                case 0xd: groupD(instruction); break;
                case 0xe: groupE(instruction); break;
                case 0xf: groupF(instruction); break;
                default:
                    illegal();
                    break;
            }
            if (prefetch && pqCount == 2 && (instruction >> 12) >= 1 && (instruction >> 12) <= 3) pqCount = 1;
            if (tracing && exceptions == exceptionsBefore && !halted) {
                ppc_.l = pc_.l;
                exception(9, 34);
            }
            if (cycleHandler != null) cycleHandler.onCycles(cycles - start);
        }
        return cycles;
    }
}
