package dsp;

import dsp.cpu.IrqLine;
import dsp.cpu.Z80;
import dsp.machine.BagmanPal;
import dsp.sound.AY8910;
import dsp.video.GfxLayout;
import dsp.video.GfxSet;
import dsp.video.Palette;
import dsp.video.ResistorNet;

import java.util.Arrays;

/** Unit tests ported from dsp-cpp {@code tests/tests.cpp} for the Bagman stack. */
public final class Tests {
    private static int failed;

    public static void main(String[] args) {
        testZ80Arithmetic();
        testZ80NmiHeldDoesNotBlockIrq();
        testZ80FlagsAndBlocks();
        testZ80RRegisterIgnoredPrefix();
        testZ80Interrupt();
        testBagmanPal();
        testGfxDecode();
        testPaletteWeights();
        testAy8910();
        String rom = System.getenv("BAGMAN_ZIP");
        if (rom != null && !rom.isBlank()) {
            testBagmanBoot(rom);
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
