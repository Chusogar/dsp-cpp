package dsp.drivers.arcade;

import dsp.core.InputState;
import dsp.core.Machine;
import dsp.core.MachineInputs;
import dsp.core.RomEntry;
import dsp.core.RomLoader;
import dsp.cpu.IrqLine;
import dsp.cpu.Z80;
import dsp.machine.BagmanPal;
import dsp.sound.AY8910;
import dsp.video.GfxLayout;
import dsp.video.GfxSet;
import dsp.video.Palette;
import dsp.video.ResistorNet;

import java.util.Arrays;
import java.util.List;

/**
 * Bagman (Valadon Automation, 1982), ported from dsp-cpp {@code drivers/arcade/bagman.cpp}
 * (originally bagman_hw.pas).
 */
public final class Bagman extends Machine {
    public static final int SCREEN_WIDTH = 224;
    public static final int SCREEN_HEIGHT = 256;
    public static final double FRAMES_PER_SECOND = 60.60606060;
    public static final int SCANLINES = 264;
    public static final int CPU_CLOCK = 3_072_000;
    public static final int AY_CLOCK = 1_536_000;

    private static final List<RomEntry> MAIN_ROMS = List.of(
            new RomEntry("e9_b05.bin", 0x1000, 0x0000, 0xe0156191),
            new RomEntry("f9_b06.bin", 0x1000, 0x1000, 0x7b758982),
            new RomEntry("f9_b07.bin", 0x1000, 0x2000, 0x302a077b),
            new RomEntry("k9_b08.bin", 0x1000, 0x3000, 0xf04293cb),
            new RomEntry("m9_b09s.bin", 0x1000, 0x4000, 0x68e83e4f),
            new RomEntry("n9_b10.bin", 0x1000, 0x5000, 0x1d6579f7)
    );

    private static final List<RomEntry> PALETTE_ROMS = List.of(
            new RomEntry("p3.bin", 0x20, 0x00, 0x2a855523),
            new RomEntry("r3.bin", 0x20, 0x20, 0xae6f1019)
    );

    private static final List<RomEntry> CHAR_ROMS = List.of(
            new RomEntry("e1_b02.bin", 0x1000, 0x0000, 0x4a0a6b55),
            new RomEntry("j1_b04.bin", 0x1000, 0x1000, 0xc680ef04)
    );

    private static final List<RomEntry> SPRITE_ROMS = List.of(
            new RomEntry("c1_b01.bin", 0x1000, 0x0000, 0x705193b2),
            new RomEntry("f1_b03s.bin", 0x1000, 0x1000, 0xdba1eda7)
    );

    private final Z80 cpu = new Z80(CPU_CLOCK);
    private final AY8910 psg = new AY8910(AY_CLOCK);
    private final BagmanPal pal = new BagmanPal();

    private final int[] memory = new int[0x10000];
    private final boolean[] dirty = new boolean[0x400];
    private final int[] palette = new int[256];

    private final GfxSet chars = new GfxSet();
    private final GfxSet sprites = new GfxSet();
    private final GfxSet charsBank1 = new GfxSet();

    private final int[] tilemap = new int[256 * 256];
    private final int[] composite = new int[256 * 256];
    private final int[] framebuffer = new int[SCREEN_WIDTH * SCREEN_HEIGHT];

    private boolean irqEnable = true;
    private boolean videoEnable = true;
    private boolean flipScreen;
    private int in0 = 0xff;
    private int in1 = 0xff;
    private int dsw = 0xfe;
    private long audioAccumulator;
    private final java.util.ArrayList<Short> audio = new java.util.ArrayList<>();

    public Bagman() {
        Arrays.fill(framebuffer, 0xff000000);
        cpu.setMemoryHandlers(this::readByte, this::writeByte);
        cpu.setIoHandlers(this::readPort, this::writePort);
        cpu.setCycleHandler(this::onCycles);
        psg.setPortHandlers(() -> in0, () -> in1, null, null);
    }

    @Override
    public boolean init(String romPath, StringBuilder error) {
        RomLoader loader = new RomLoader();
        try {
            if (!loader.open(romPath, error)) {
                return false;
            }
            byte[] mainRom = new byte[0x6000];
            if (!loader.load(MAIN_ROMS, mainRom, error)) {
                return false;
            }
            for (int i = 0; i < mainRom.length; i++) {
                memory[i] = mainRom[i] & 0xff;
            }

            byte[] charRom = new byte[0x2000];
            if (!loader.load(CHAR_ROMS, charRom, error)) {
                return false;
            }
            byte[] spriteRom = new byte[0x2000];
            if (!loader.load(SPRITE_ROMS, spriteRom, error)) {
                return false;
            }
            byte[] prom = new byte[0x40];
            if (!loader.load(PALETTE_ROMS, prom, error)) {
                return false;
            }

            decodeGraphics(charRom, spriteRom);
            buildPalette(prom);
            warnings.clear();
            warnings.addAll(loader.warnings());
            reset();
            return true;
        } finally {
            loader.close();
        }
    }

    @Override
    public void reset() {
        cpu.reset();
        psg.reset();
        pal.reset();
        irqEnable = true;
        videoEnable = true;
        flipScreen = false;
        in0 = 0xff;
        in1 = 0xff;
        Arrays.fill(dirty, true);
        Arrays.fill(tilemap, 0xff000000);
        Arrays.fill(composite, 0xff000000);
        audioAccumulator = 0;
        audio.clear();
    }

    @Override
    public void runFrame() {
        int cyclesPerLine = (int) (CPU_CLOCK / FRAMES_PER_SECOND / SCANLINES);
        for (int line = 0; line < SCANLINES; line++) {
            if (line == 240) {
                if (irqEnable) {
                    cpu.setIrq(IrqLine.HOLD);
                }
                updateVideo();
            }
            cpu.run(cyclesPerLine);
        }
    }

    @Override
    public void setInputs(MachineInputs inputs) {
        InputState player1 = inputs.player1;
        InputState player2 = inputs.player2;
        in0 = 0xff;
        in1 = 0xff;
        if (inputs.coin1) {
            in0 &= 0xfe;
        }
        if (inputs.coin2) {
            in0 &= 0xfd;
        }
        if (player1.start) {
            in0 &= 0xfb;
        }
        if (player1.left) {
            in0 &= 0xf7;
        }
        if (player1.right) {
            in0 &= 0xef;
        }
        if (player1.up) {
            in0 &= 0xdf;
        }
        if (player1.down) {
            in0 &= 0xbf;
        }
        if (player1.button1) {
            in0 &= 0x7f;
        }

        if (player2.start) {
            in1 &= 0xfb;
        }
        if (player2.left) {
            in1 &= 0xf7;
        }
        if (player2.right) {
            in1 &= 0xef;
        }
        if (player2.up) {
            in1 &= 0xdf;
        }
        if (player2.down) {
            in1 &= 0xbf;
        }
        if (player2.button1) {
            in1 &= 0x7f;
        }
    }

    @Override
    public void setDipSwitch(int bank, int value) {
        if (bank == 0) {
            dsw = value & 0xff;
        }
    }

    @Override
    public int[] framebuffer() {
        return framebuffer;
    }

    @Override
    public int screenWidth() {
        return SCREEN_WIDTH;
    }

    @Override
    public int screenHeight() {
        return SCREEN_HEIGHT;
    }

    @Override
    public double framesPerSecond() {
        return FRAMES_PER_SECOND;
    }

    @Override
    public void drainAudio(List<Short> out) {
        out.addAll(audio);
        audio.clear();
    }

    @Override
    public int sampleRate() {
        return AY8910.SAMPLE_RATE;
    }

    @Override
    public String title() {
        return "Bagman";
    }

    private int readByte(int address) {
        address &= 0xffff;
        if (address <= 0x67ff || (address >= 0x9000 && address <= 0x93ff)
                || (address >= 0x9800 && address <= 0x9bff) || address >= 0xc000) {
            return memory[address];
        }
        if (address == 0xa000) {
            return pal.read();
        }
        if (address == 0xb000) {
            return dsw;
        }
        return 0xff;
    }

    private void writeByte(int address, int value) {
        address &= 0xffff;
        value &= 0xff;
        if (address <= 0x5fff || address >= 0xc000) {
            return;
        }
        if (address >= 0x6000 && address <= 0x67ff) {
            memory[address] = value;
            return;
        }
        if ((address >= 0x9000 && address <= 0x93ff) || (address >= 0x9800 && address <= 0x9bff)) {
            if (memory[address] != value) {
                dirty[address & 0x3ff] = true;
                memory[address] = value;
            }
            return;
        }
        switch (address) {
            case 0xa000:
                irqEnable = (value & 1) != 0;
                break;
            case 0xa001:
            case 0xa002:
                flipScreen = (value & 1) != 1;
                break;
            case 0xa003:
                boolean enable = (value & 1) != 0;
                if (videoEnable != enable) {
                    videoEnable = enable;
                    if (videoEnable) {
                        Arrays.fill(dirty, true);
                    }
                }
                break;
            default:
                if (address >= 0xa800 && address <= 0xa805) {
                    pal.write(address & 7, value);
                }
                break;
        }
    }

    private int readPort(int port) {
        if ((port & 0xff) == 0x0c) {
            return psg.read();
        }
        return 0xff;
    }

    private void writePort(int port, int value) {
        switch (port & 0xff) {
            case 0x08:
                psg.control(value);
                break;
            case 0x09:
                psg.write(value);
                break;
            default:
                break;
        }
    }

    private void onCycles(int cycles) {
        audioAccumulator += (long) cycles * AY8910.SAMPLE_RATE;
        while (audioAccumulator >= CPU_CLOCK) {
            audioAccumulator -= CPU_CLOCK;
            int sample = psg.update();
            if (sample < -32768) {
                sample = -32768;
            } else if (sample > 32767) {
                sample = 32767;
            }
            audio.add((short) sample);
        }
    }

    private void decodeGraphics(byte[] charRom, byte[] spriteRom) {
        chars.decode(charLayout(), charRom);
        sprites.decode(spriteLayout(), charRom);
        charsBank1.decode(charLayout(), spriteRom);
    }

    private void buildPalette(byte[] prom) {
        int[] rg = {1000, 470, 220};
        int[] b = {470, 220};
        double[][] weights = Palette.computeResistorWeights(0, 255, -1.0, new ResistorNet[] {
                new ResistorNet(rg, 470, 0),
                new ResistorNet(rg, 470, 0),
                new ResistorNet(b, 470, 0)
        });
        Arrays.fill(palette, 0xff000000);
        for (int index = 0; index < 0x40; index++) {
            int data = prom[index] & 0xff;
            int red = Palette.combineWeights(weights[0], new int[] {
                    (data >> 0) & 1, (data >> 1) & 1, (data >> 2) & 1
            });
            int green = Palette.combineWeights(weights[1], new int[] {
                    (data >> 3) & 1, (data >> 4) & 1, (data >> 5) & 1
            });
            int blue = Palette.combineWeights(weights[2], new int[] {
                    (data >> 6) & 1, (data >> 7) & 1
            });
            palette[index] = 0xff000000 | (red << 16) | (green << 8) | blue;
        }
    }

    private void drawTile(int offset) {
        int attrib = memory[0x9800 + offset];
        int tileX = 31 - (offset / 32);
        int tileY = offset % 32;
        int code = memory[0x9000 + offset] + ((attrib & 0x20) << 3);
        GfxSet gfx = ((attrib & 0x10) != 0) ? charsBank1 : chars;
        int color = (attrib & 0x0f) << 2;
        byte[] pixels = gfx.pixels();
        int base = gfx.elementOffset(code);
        for (int y = 0; y < 8; y++) {
            int target = (tileY * 8 + y) * 256 + tileX * 8;
            for (int x = 0; x < 8; x++) {
                tilemap[target + x] = palette[(pixels[base + y * 8 + x] & 0xff) + color];
            }
        }
    }

    private void drawSprite(int index) {
        int entry = 0x9800 + index * 4;
        int attrib = memory[entry];
        int color = (memory[entry + 1] & 0x1f) << 2;
        int code = (attrib & 0x3f) + ((memory[entry + 1] & 0x20) << 1);
        int posX = memory[entry + 2];
        int posY = memory[entry + 3];
        if (posX == 0 || posY == 0) {
            return;
        }
        boolean flipX = (attrib & 0x80) != 0;
        boolean flipY = (attrib & 0x40) != 0;
        byte[] pixels = sprites.pixels();
        int base = sprites.elementOffset(code);
        for (int y = 0; y < 16; y++) {
            int screenY = posY - 1 + y;
            if (screenY < 0 || screenY >= 256) {
                continue;
            }
            int sourceY = flipY ? (15 - y) : y;
            for (int x = 0; x < 16; x++) {
                int sourceX = flipX ? (15 - x) : x;
                int pixel = pixels[base + sourceY * 16 + sourceX] & 0xff;
                if (pixel == 0) {
                    continue;
                }
                int screenX = (posX + 1 + x) & 0xff;
                composite[screenY * 256 + screenX] = palette[pixel + color];
            }
        }
    }

    private void updateVideo() {
        if (videoEnable) {
            for (int offset = 0; offset < 0x400; offset++) {
                if (!dirty[offset]) {
                    continue;
                }
                drawTile(offset);
                dirty[offset] = false;
            }
            System.arraycopy(tilemap, 0, composite, 0, tilemap.length);
            for (int index = 7; index >= 0; index--) {
                drawSprite(index);
            }
        } else {
            Arrays.fill(composite, 0xff000000);
        }

        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                int pixel = composite[y * 256 + x + 16];
                int target = flipScreen
                        ? (SCREEN_HEIGHT - 1 - y) * SCREEN_WIDTH + (SCREEN_WIDTH - 1 - x)
                        : y * SCREEN_WIDTH + x;
                framebuffer[target] = pixel;
            }
        }
    }

    private static GfxLayout charLayout() {
        GfxLayout layout = new GfxLayout();
        layout.width = 8;
        layout.height = 8;
        layout.total = 512;
        layout.planes = 2;
        layout.charIncrement = 8 * 8;
        layout.rotateCw = true;
        layout.planeOffsets = new int[] {0, 512 * 8 * 8};
        layout.xOffsets = new int[] {0, 1, 2, 3, 4, 5, 6, 7};
        layout.yOffsets = new int[] {0, 8, 16, 24, 32, 40, 48, 56};
        return layout;
    }

    private static GfxLayout spriteLayout() {
        GfxLayout layout = new GfxLayout();
        layout.width = 16;
        layout.height = 16;
        layout.total = 128;
        layout.planes = 2;
        layout.charIncrement = 32 * 8;
        layout.rotateCw = true;
        layout.planeOffsets = new int[] {0, 128 * 16 * 16};
        layout.xOffsets = new int[] {0, 1, 2, 3, 4, 5, 6, 7, 64, 65, 66, 67, 68, 69, 70, 71};
        layout.yOffsets = new int[] {0, 8, 16, 24, 32, 40, 48, 56, 128, 136, 144, 152, 160, 168, 176, 184};
        return layout;
    }
}
