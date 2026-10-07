package dsp;

import dsp.cpu.IrqLine;
import dsp.cpu.M68000;
import dsp.cpu.Z80;
import dsp.machine.BagmanPal;
import dsp.sound.AY8910;
import dsp.sound.OKIM6295;
import dsp.video.GfxLayout;
import dsp.video.GfxSet;
import dsp.video.Palette;
import dsp.video.ResistorNet;

import java.util.Arrays;

/** Unit tests ported from dsp-cpp {@code tests/tests.cpp} for the Bagman and Pirates stacks. */
public final class Tests {
    private static int failed;
    private static int[] m68kMemory;

    public static void main(String[] args) {
        testZ80Arithmetic();
        testZ80NmiHeldDoesNotBlockIrq();
        testZ80FlagsAndBlocks();
        testZ80RRegisterIgnoredPrefix();
        testZ80Interrupt();
        testM68000ResetAndMoves();
        testM68000BranchesAndSubroutines();
        testM68000Traps();
        testM68000Interrupt();
        testBagmanPal();
        testGfxDecode();
        testPaletteWeights();
        testAy8910();
        testOkim6295();
        String bagman = System.getenv("BAGMAN_ZIP");
        if (bagman != null && !bagman.isBlank()) {
            testBagmanBoot(bagman);
        }
        String pirates = firstEnv("PIRATES_ZIP", "JDSP_PIRATES_ZIP");
        if (pirates == null || pirates.isBlank()) {
            pirates = "/tmp/roms/pirates.zip";
        }
        java.nio.file.Path piratesPath = java.nio.file.Path.of(pirates);
        if (java.nio.file.Files.isRegularFile(piratesPath)) {
            testPiratesBoot(pirates);
        }
        if (failed != 0) {
            System.err.println(failed + " test(s) failed");
            System.exit(1);
        }
        System.out.println("all tests passed");
    }

    private static int[] makeMemory() {
        int[] memory = new int[0x10000];
        Arrays.fill(memory, 0);
        return memory;
    }

    private static Z80 makeCpu(int[] memory) {
        Z80 cpu = new Z80(3_072_000);
        cpu.setMemoryHandlers(address -> memory[address & 0xffff],
                (address, value) -> memory[address & 0xffff] = value & 0xff);
        return cpu;
    }

    private static void testZ80Arithmetic() {
        int[] memory = makeMemory();
        Z80 cpu = makeCpu(memory);
        put(memory, 0, 0x3e, 0x0f, 0xc6, 0x01, 0x27);
        cpu.run(7 + 7 + 4);
        check(cpu.a == 0x16, "daa converts 0x10 to bcd 0x16");
        check((cpu.f & Z80.NF) == 0, "daa keeps N clear after an add");
    }

    private static void testZ80NmiHeldDoesNotBlockIrq() {
        int[] memory = makeMemory();
        Z80 cpu = makeCpu(memory);
        put(memory, 0, 0xed, 0x56, 0xfb, 0x18, 0xfe);
        put(memory, 0x38, 0x3a, 0x00, 0x80, 0x3c, 0x32, 0x00, 0x80, 0xfb, 0xed, 0x4d);
        put(memory, 0x66, 0x3a, 0x01, 0x80, 0x3c, 0x32, 0x01, 0x80, 0xed, 0x45);
        cpu.run(100);
        cpu.setNmi(IrqLine.ASSERT);
        cpu.setIrq(IrqLine.ASSERT);
        cpu.run(2000);
        check(memory[0x8001] == 1, "Z80 takes a held NMI exactly once (edge triggered)");
        check(memory[0x8000] > 1, "Z80 still services maskable IRQs while NMI is held low");
    }

    private static void testZ80FlagsAndBlocks() {
        int[] memory = makeMemory();
        Z80 cpu = makeCpu(memory);
        put(memory, 0, 0x21, 0x00, 0x20, 0x11, 0x00, 0x30, 0x01, 0x04, 0x00, 0xed, 0xb0, 0x76);
        for (int i = 0; i < 4; i++) {
            memory[0x2000 + i] = 0xa0 + i;
        }
        cpu.run(200);
        check(memory[0x3000] == 0xa0 && memory[0x3003] == 0xa3, "ldir copies the block");
        check(cpu.halted, "halt stops execution");
    }

    private static void testZ80RRegisterIgnoredPrefix() {
        int[] memory = makeMemory();
        Z80 cpu = makeCpu(memory);
        put(memory, 0, 0xdd, 0x00, 0xed, 0x5f, 0x76);
        cpu.run(4 + 4 + 9);
        check(cpu.a == 4, "Z80 R counts an ignored DD prefix and its opcode once each");
    }

    private static void testZ80Interrupt() {
        int[] memory = makeMemory();
        Z80 cpu = makeCpu(memory);
        put(memory, 0, 0xed, 0x56, 0xfb, 0x00, 0x00, 0x00);
        memory[0x0038] = 0x3e;
        memory[0x0039] = 0x42;
        memory[0x003a] = 0xed;
        memory[0x003b] = 0x4d;
        cpu.sp = 0xf000;
        cpu.run(12);
        cpu.setIrq(IrqLine.HOLD);
        cpu.run(40);
        check(cpu.a == 0x42, "mode 1 interrupt vectors through 0x0038");
    }

    private static void testBagmanPal() {
        BagmanPal pal = new BagmanPal();
        pal.reset();
        int value = pal.read();
        check(value <= 0x3f, "the PAL only drives six data bits");
        pal.write(0, 0);
        pal.write(1, 0);
        int other = pal.read();
        check(other <= 0x3f, "the PAL stays within six data bits after writes");
        check(other != value || value == 0, "changing the PAL inputs changes its output");
    }

    private static void testGfxDecode() {
        byte[] rom = new byte[16];
        rom[0] = (byte) 0xff;
        for (int y = 0; y < 8; y++) {
            rom[8 + y] = (byte) 0x80;
        }

        GfxLayout layout = new GfxLayout();
        layout.width = 8;
        layout.height = 8;
        layout.total = 1;
        layout.planes = 2;
        layout.charIncrement = 64;
        layout.planeOffsets = new int[] {0, 64};
        layout.xOffsets = new int[] {0, 1, 2, 3, 4, 5, 6, 7};
        layout.yOffsets = new int[] {0, 8, 16, 24, 32, 40, 48, 56};

        GfxSet gfx = new GfxSet();
        gfx.decode(layout, rom);
        byte[] pixels = gfx.pixels();
        check(pixels[0] == 3, "overlapping planes produce colour 3");
        check(pixels[1] == 2, "plane 0 alone produces colour 2");
        check(pixels[8] == 1, "plane 1 alone produces colour 1");
        check(pixels[9] == 0, "empty pixels stay transparent");

        layout.rotateCw = true;
        gfx.decode(layout, rom);
        pixels = gfx.pixels();
        check(pixels[7] == 3, "rotation moves the top left pixel to the top right");

        layout.rotateCw = false;
        layout.rotateCcw = true;
        gfx.decode(layout, rom);
        pixels = gfx.pixels();
        check(pixels[7 * 8] == 3, "ccw rotation moves the top left pixel to the bottom left");
        check(pixels[0] == 2, "ccw rotation moves the old top right pixel to the top left");

        byte[] cpsRom = new byte[8];
        cpsRom[3] = (byte) 0x80;
        GfxLayout cps = new GfxLayout();
        cps.width = 8;
        cps.height = 1;
        cps.total = 1;
        cps.planes = 4;
        cps.charIncrement = 64;
        cps.lsbFirst = true;
        cps.planeOffsets = new int[] {24, 16, 8, 0};
        cps.xOffsets = new int[] {0, 1, 2, 3, 4, 5, 6, 7};
        cps.yOffsets = new int[] {0};
        GfxSet cpsGfx = new GfxSet();
        cpsGfx.decode(cps, cpsRom);
        check(cpsGfx.pixels()[0] == 1, "CPS1 plane 0 is the pen LSB");
        check(cpsGfx.pixels()[1] == 0, "CPS1 neighbouring pixel stays empty");
    }

    private static void testPaletteWeights() {
        double[][] weights = Palette.computeResistorWeights(0, 255, -1.0, new ResistorNet[] {
                new ResistorNet(new int[] {1000, 470, 220}, 470, 0),
                new ResistorNet(new int[] {1000, 470, 220}, 470, 0),
                new ResistorNet(new int[] {470, 220}, 470, 0)
        });
        check(weights.length == 3, "three resistor networks are returned");
        int white = Palette.combineWeights(weights[0], new int[] {1, 1, 1});
        check(white == 255, "all bits set gives full intensity");
        check(Palette.combineWeights(weights[0], new int[] {0, 0, 0}) == 0, "no bits set gives black");
    }

    private static void testAy8910() {
        AY8910 psg = new AY8910(1_536_000);
        psg.reset();
        psg.control(7);
        psg.write(0x3e);
        psg.control(0);
        psg.write(0x40);
        psg.control(8);
        psg.write(0x0f);
        boolean nonZero = false;
        for (int i = 0; i < 4410; i++) {
            if (psg.update() != 0) {
                nonZero = true;
            }
        }
        check(nonZero, "the PSG generates a tone");
    }

    private static M68000 makeM68k() {
        m68kMemory = new int[0x10000];
        M68000 cpu = new M68000(12_000_000, M68000.Type.M68000);
        cpu.setMemoryHandlers(
                address -> {
                    int a = address & 0xfffe;
                    return ((m68kMemory[a] & 0xff) << 8) | (m68kMemory[a + 1] & 0xff);
                },
                (address, value) -> {
                    int a = address & 0xfffe;
                    m68kMemory[a] = (value >> 8) & 0xff;
                    m68kMemory[a + 1] = value & 0xff;
                });
        return cpu;
    }

    private static void putWord(int address, int value) {
        m68kMemory[address] = (value >> 8) & 0xff;
        m68kMemory[address + 1] = value & 0xff;
    }

    private static void putLong(int address, int value) {
        putWord(address, (value >>> 16) & 0xffff);
        putWord(address + 2, value & 0xffff);
    }

    private static void testM68000ResetAndMoves() {
        M68000 cpu = makeM68k();
        putLong(0x0000, 0x00001000);
        putLong(0x0004, 0x00000400);
        putWord(0x0400, 0x7aff);
        putWord(0x0402, 0x7c2a);
        putWord(0x0404, 0x0686);
        putLong(0x0406, 0x00000100);
        putWord(0x040a, 0x60fe);
        cpu.reset();
        check(cpu.a[7].l == 0x1000, "the 68000 loads the stack pointer from vector 0");
        check(cpu.pc() == 0x400, "the 68000 loads the program counter from vector 1");
        cpu.run(200);
        check(cpu.d[5].l == 0xffffffff, "moveq sign extends into the whole register");
        check(cpu.d[6].l == 0x12a, "addi.l adds a long immediate");
    }

    private static void testM68000BranchesAndSubroutines() {
        M68000 cpu = makeM68k();
        putLong(0x0000, 0x00001000);
        putLong(0x0004, 0x00000400);
        putWord(0x0400, 0x7003);
        putWord(0x0402, 0x6106);
        putWord(0x0404, 0x60fe);
        putWord(0x040a, 0x5280);
        putWord(0x040c, 0x4e75);
        cpu.reset();
        cpu.run(200);
        check(cpu.d[0].l == 4, "bsr/rts execute the subroutine once");
        check(cpu.a[7].l == 0x1000, "rts restores the stack pointer");
    }

    private static void testM68000Traps() {
        {
            M68000 cpu = makeM68k();
            putLong(0x0000, 0x00001000);
            putLong(0x0004, 0x00000400);
            putLong(0x001c, 0x00000500);
            putWord(0x0400, 0x203c);
            putLong(0x0402, 0x10000000);
            putWord(0x0406, 0x7201);
            putWord(0x0408, 0x81c1);
            putWord(0x040a, 0x4e76);
            putWord(0x040c, 0x60fe);
            putWord(0x0500, 0x7e07);
            putWord(0x0502, 0x4e73);
            cpu.reset();
            cpu.run(1000);
            check(cpu.d[7].l == 7 && cpu.pc() == 0x40c,
                    "68000 TRAPV traps on overflow and returns after it");
        }
        {
            M68000 cpu = makeM68k();
            putLong(0x0000, 0x00001000);
            putLong(0x0004, 0x00000400);
            putLong(0x0014, 0x00000500);
            putWord(0x0400, 0x7200);
            putWord(0x0402, 0x7005);
            putWord(0x0404, 0x80c1);
            putWord(0x0406, 0x60fe);
            putWord(0x0500, 0x5287);
            putWord(0x0502, 0x4e73);
            cpu.reset();
            cpu.run(2000);
            check(cpu.d[7].l == 1 && cpu.pc() == 0x406,
                    "68000 divide-by-zero trap stacks the next instruction");
        }
    }

    private static void testM68000Interrupt() {
        M68000 cpu = makeM68k();
        putLong(0x0000, 0x00001000);
        putLong(0x0004, 0x00000400);
        putLong(0x0078, 0x00000500);
        putWord(0x0400, 0x027c);
        putWord(0x0402, 0xf8ff);
        putWord(0x0404, 0x60fe);
        putWord(0x0500, 0x7201);
        putWord(0x0502, 0x60fe);
        cpu.reset();
        cpu.run(40);
        cpu.setIrq(6, IrqLine.ASSERT);
        cpu.run(200);
        check(cpu.d[1].l == 1, "the 68000 takes an autovectored level 6 interrupt");
    }

    private static void testOkim6295() {
        OKIM6295 chip = new OKIM6295(1_056_000, true);
        check(chip.sampleFrequency() == 8000, "pin 7 high selects the /132 divider");
        byte[] rom = new byte[0x1000];
        rom[0x08] = 0x00;
        rom[0x09] = 0x02;
        rom[0x0a] = 0x00;
        rom[0x0b] = 0x00;
        rom[0x0c] = 0x02;
        rom[0x0d] = 0x20;
        for (int index = 0x200; index <= 0x220; index++) {
            rom[index] = 0x77;
        }
        chip.setRom(rom);
        chip.reset();
        check(chip.update() == 0, "an idle OKI6295 is silent");
        chip.write(0x81);
        chip.write(0x10);
        check((chip.read() & 0x01) != 0, "the status byte reports the busy voice");
        int sample = 0;
        for (int index = 0; index < 16; index++) {
            sample = chip.update();
        }
        check(sample != 0, "the OKI6295 decodes the selected sample");
        chip.write(0x08);
        check((chip.read() & 0x01) == 0, "the silence command stops the voice");
        chip.setPin7(false);
        check(chip.sampleFrequency() == 1_056_000 / 165, "pin 7 low selects the /165 divider");
    }

    private static void testPiratesBoot(String romPath) {
        dsp.drivers.arcade.Pirates machine = new dsp.drivers.arcade.Pirates();
        StringBuilder error = new StringBuilder();
        check(machine.init(romPath, error), "Pirates loads the ROM set: " + error);
        if (error.length() > 0 && failed != 0) {
            return;
        }
        dsp.core.MachineInputs inputs = new dsp.core.MachineInputs();
        int coloured = 0;
        for (int frame = 0; frame < 180; frame++) {
            machine.setInputs(inputs);
            machine.runFrame();
        }
        for (int pixel : machine.framebuffer()) {
            if ((pixel & 0x00ffffff) != 0) {
                coloured++;
            }
        }
        check(coloured > 1000, "Pirates title screen has visible pixels (" + coloured + ")");
        check("Pirates".equals(machine.title()), "Pirates reports its title");
        check(machine.screenWidth() == 288 && machine.screenHeight() == 224,
                "Pirates framebuffer is 288x224");
        check(machine.debugPc() != 0, "Pirates 68000 is executing (" + Integer.toHexString(machine.debugPc()) + ")");
    }

    private static String firstEnv(String... names) {
        for (String name : names) {
            String value = System.getenv(name);
            if (value != null && !value.isBlank()) {
                return value;
            }
        }
        return null;
    }

    private static void testBagmanBoot(String romPath) {
        dsp.drivers.arcade.Bagman machine = new dsp.drivers.arcade.Bagman();
        StringBuilder error = new StringBuilder();
        check(machine.init(romPath, error), "Bagman loads the ROM set: " + error);
        if (failed != 0 && error.length() > 0) {
            return;
        }
        dsp.core.MachineInputs inputs = new dsp.core.MachineInputs();
        int coloured = 0;
        for (int frame = 0; frame < 180; frame++) {
            machine.setInputs(inputs);
            machine.runFrame();
        }
        for (int pixel : machine.framebuffer()) {
            if ((pixel & 0x00ffffff) != 0) {
                coloured++;
            }
        }
        check(coloured > 1000, "Bagman title screen has visible pixels (" + coloured + ")");
        check("Bagman".equals(machine.title()), "Bagman reports its title");
        check(machine.screenWidth() == 224 && machine.screenHeight() == 256,
                "Bagman framebuffer is 224x256");
    }

    private static void put(int[] memory, int offset, int... bytes) {
        for (int i = 0; i < bytes.length; i++) {
            memory[offset + i] = bytes[i] & 0xff;
        }
    }

    private static void check(boolean condition, String message) {
        if (!condition) {
            failed++;
            System.err.println("FAIL: " + message);
        }
    }
}
