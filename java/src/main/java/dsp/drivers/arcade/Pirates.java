package dsp.drivers.arcade;

import dsp.core.Machine;
import dsp.core.MachineInputs;
import dsp.core.RomEntry;
import dsp.core.RomLoader;
import dsp.cpu.IrqLine;
import dsp.cpu.M68000;
import dsp.machine.Eeprom93C46;
import dsp.sound.OKIM6295;
import dsp.video.GfxLayout;
import dsp.video.GfxSet;

import java.util.Arrays;
import java.util.List;
import java.util.function.IntUnaryOperator;

/**
 * NIX "Pirates" hardware (also used by "Genix Family"), ported from dsp-cpp
 * {@code drivers/arcade/pirates.cpp} (originally pirates_hw.pas).
 *
 * One M68000 running everything (there is no separate sound CPU): a single
 * OKI M6295 is driven straight from the main CPU's address space, sample
 * generation is paced from the CPU's cycle handler. A 93C46 serial EEPROM
 * backs settings/high scores. Three tile layers share one 8x8 4bpp graphics
 * set (a fixed "text" layer plus two independently scrolling background
 * layers that share a single X scroll register) and one bank of 16x16 4bpp
 * sprites, all with heavy per-plane bit/address scrambling on the ROMs that
 * is undone once at load time.
 */
public final class Pirates extends Machine {
    public enum Game { PIRATES, GENIX }

    public static final int SCREEN_WIDTH = 288;
    public static final int SCREEN_HEIGHT = 224;
    public static final int SCANLINES = 256;
    public static final int VBLANK_LINE = 240;
    public static final double FRAMES_PER_SECOND = 60.0;
    public static final int MAIN_CLOCK = 16_000_000;
    public static final int OKI_CLOCK = 1_333_333;

    private static final int PLANE_SIZE = 0x80000;

    private static final List<RomEntry> PIRATES_PROGRAM_LOW =
            List.of(new RomEntry("r_449b.bin", 0x80000, 0, 0x224aeeda));
    private static final List<RomEntry> PIRATES_PROGRAM_HIGH =
            List.of(new RomEntry("l_5c1e.bin", 0x80000, 0, 0x46740204));
    private static final List<RomEntry> PIRATES_GFX = List.of(
            new RomEntry("p4_4d48.bin", 0x80000, 0 * PLANE_SIZE, 0x89fda216),
            new RomEntry("p2_5d74.bin", 0x80000, 1 * PLANE_SIZE, 0x40e069b4),
            new RomEntry("p1_7b30.bin", 0x80000, 2 * PLANE_SIZE, 0x26d78518),
            new RomEntry("p8_9f4f.bin", 0x80000, 3 * PLANE_SIZE, 0xf31696ea)
    );
    private static final List<RomEntry> PIRATES_SPRITES = List.of(
            new RomEntry("s1_6e89.bin", 0x80000, 0 * PLANE_SIZE, 0xc78a276f),
            new RomEntry("s2_6df3.bin", 0x80000, 1 * PLANE_SIZE, 0x9f0bad96),
            new RomEntry("s4_fdcc.bin", 0x80000, 2 * PLANE_SIZE, 0x8916ddb5),
            new RomEntry("s8_4b7c.bin", 0x80000, 3 * PLANE_SIZE, 0x1c41bd2c)
    );
    private static final List<RomEntry> PIRATES_OKI =
            List.of(new RomEntry("s89_49d4.bin", 0x80000, 0, 0x63a739ec));

    private static final List<RomEntry> GENIX_PROGRAM_LOW =
            List.of(new RomEntry("11.u15.15c", 0x80000, 0, 0xd26abfb0));
    private static final List<RomEntry> GENIX_PROGRAM_HIGH =
            List.of(new RomEntry("12.u16.16c", 0x80000, 0, 0xa14a25b4));
    private static final List<RomEntry> GENIX_GFX = List.of(
            new RomEntry("17.u34.12g", 0x40000, 0 * PLANE_SIZE, 0x58da8aac),
            new RomEntry("19.u35.12h", 0x40000, 1 * PLANE_SIZE, 0x96bad9a8),
            new RomEntry("18.u48.13g", 0x40000, 2 * PLANE_SIZE, 0x0ddc58b6),
            new RomEntry("20.u49.13h", 0x40000, 3 * PLANE_SIZE, 0x2be308c5)
    );
    private static final List<RomEntry> GENIX_SPRITES = List.of(
            new RomEntry("16.u69.6g", 0x40000, 0 * PLANE_SIZE, 0xb8422af7),
            new RomEntry("15.u70.4g", 0x40000, 1 * PLANE_SIZE, 0xe46125c5),
            new RomEntry("14.u71.3g", 0x40000, 2 * PLANE_SIZE, 0x7a8ed21b),
            new RomEntry("13.u72.1g", 0x40000, 3 * PLANE_SIZE, 0xf78bd6ca)
    );
    private static final List<RomEntry> GENIX_OKI =
            List.of(new RomEntry("10.u31.1b", 0x80000, 0, 0x80d087bc));

    private final Game game;
    private final M68000 mainCpu;
    private final OKIM6295 oki;
    private final Eeprom93C46 eeprom;

    private final GfxSet tiles = new GfxSet();
    private final GfxSet sprites = new GfxSet();

    private final int[] rom = new int[0x80000];
    private final int[] workRam = new int[0x8000];
    private final int[] spriteRam = new int[0x800];
    private final int[] paletteRam = new int[0x2000];
    private final int[] tileRam = new int[0x4000];
    private final int[] palette = new int[0x2000];

    private final byte[][] soundBanks = new byte[2][0x40000];

    private int in0 = 0x0f;
    private int in1 = 0xffff;
    private int scrollX;

    private final int[] framebuffer = new int[SCREEN_WIDTH * SCREEN_HEIGHT];
    private final int[] canvas = new int[512 * 256];
    private final java.util.ArrayList<Short> audio = new java.util.ArrayList<>();
    private long okiAccumulator;
    private long audioAccumulator;
    private int lastOki;
    private int lastOkiBank = -1;

    private final int mainCyclesPerLine;

    public Pirates() {
        this(Game.PIRATES);
    }

    public Pirates(Game game) {
        this.game = game;
        mainCpu = new M68000(MAIN_CLOCK);
        oki = new OKIM6295(OKI_CLOCK, false);
        eeprom = new Eeprom93C46(16);
        Arrays.fill(framebuffer, 0xff000000);
        Arrays.fill(canvas, 0xff000000);

        mainCpu.setMemoryHandlers(this::mainRead, this::mainWrite);
        mainCpu.setCycleHandler(this::onMainCycles);

        mainCyclesPerLine = MAIN_CLOCK / (SCANLINES * (int) FRAMES_PER_SECOND);
    }

    @Override
    public String title() {
        return game == Game.PIRATES ? "Pirates" : "Genix Family";
    }

    @Override
    public boolean init(String romPath, StringBuilder error) {
        if (!loadProgram(romPath, error)) {
            return false;
        }
        if (!loadSound(romPath, error)) {
            return false;
        }
        if (!loadGraphics(romPath, error)) {
            return false;
        }

        // Pirates only: two-byte protection patch from pirates_hw.pas
        // (`rom[$62c0 shr 1] := $6006`).
        if (game == Game.PIRATES) {
            rom[0x62c0 / 2] = 0x6006;
        }

        eeprom.reset();
        lastOkiBank = -1;
        selectOkiBank(0);
        reset();
        return true;
    }

    @Override
    public void reset() {
        mainCpu.reset();
        oki.reset();
        in0 = 0x0f;
        in1 = 0xffff;
        scrollX = 0;
    }

    @Override
    public void runFrame() {
        for (int line = 0; line < SCANLINES; line++) {
            if (line == VBLANK_LINE) {
                mainCpu.setIrq(1, IrqLine.HOLD);
                renderFrame();
            }
            mainCpu.run(mainCyclesPerLine);
        }
    }

    @Override
    public void setInputs(MachineInputs inputs) {
        int nextIn1 = 0xffff;
        if (inputs.player1.up) nextIn1 &= ~0x0001;
        if (inputs.player1.down) nextIn1 &= ~0x0002;
        if (inputs.player1.left) nextIn1 &= ~0x0004;
        if (inputs.player1.right) nextIn1 &= ~0x0008;
        if (inputs.player1.button1) nextIn1 &= ~0x0010;
        if (inputs.player1.button2) nextIn1 &= ~0x0020;
        if (inputs.player1.button3) nextIn1 &= ~0x0040;
        if (inputs.player1.start) nextIn1 &= ~0x0080;
        if (inputs.player2.right) nextIn1 &= ~0x0100;
        if (inputs.player2.left) nextIn1 &= ~0x0200;
        if (inputs.player2.up) nextIn1 &= ~0x0400;
        if (inputs.player2.down) nextIn1 &= ~0x0800;
        // Pascal P2 uses but1/but2/but3 (not but0), so button2/3/4 here.
        if (inputs.player2.button2) nextIn1 &= ~0x1000;
        if (inputs.player2.button3) nextIn1 &= ~0x2000;
        if (inputs.player2.button4) nextIn1 &= ~0x4000;
        if (inputs.player2.start) nextIn1 &= ~0x8000;
        in1 = nextIn1 & 0xffff;

        int nextIn0 = 0x0f;
        if (inputs.coin1) nextIn0 &= ~0x01;
        if (inputs.coin2) nextIn0 &= ~0x02;
        in0 = nextIn0;
    }

    @Override
    public void setDipSwitch(int bank, int value) {
        // This board has no physical DIP bank; every setting lives in the 93C46
        // EEPROM and is configured from the game's own service menu.
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
        return 44100;
    }

    public int debugPc() {
        return mainCpu.pc();
    }

    private int mainRead(int addr) {
        addr &= 0xffffff;
        if (addr <= 0xfffff) {
            return rom[addr >>> 1] & 0xffff;
        }
        if (addr >= 0x100000 && addr <= 0x10ffff) {
            if (game == Game.GENIX) {
                int byteOff = addr & 0xffff;
                if (byteOff == 0x9e98) {
                    return 4;
                }
                if (byteOff >= 0x9e99 && byteOff <= 0x9e9b) {
                    return 0;
                }
            }
            return workRam[(addr & 0xffff) >>> 1] & 0xffff;
        }
        if (addr == 0x300000) {
            return in1 & 0xffff;
        }
        if (addr == 0x400000) {
            if (game == Game.GENIX) {
                return in0;
            }
            return (in0 | (eeprom.doRead() << 4)) & 0xffff;
        }
        if (addr >= 0x500000 && addr <= 0x500fff) {
            return spriteRam[(addr & 0xfff) >>> 1] & 0xffff;
        }
        if (addr >= 0x800000 && addr <= 0x803fff) {
            return paletteRam[(addr & 0x3fff) >>> 1] & 0xffff;
        }
        if (addr >= 0x900000 && addr <= 0x907fff) {
            return tileRam[(addr & 0x7fff) >>> 1] & 0xffff;
        }
        if (addr == 0xa00000) {
            return oki.read();
        }
        return 0xffff;
    }

    private void mainWrite(int addr, int value) {
        addr &= 0xffffff;
        value &= 0xffff;
        if (addr <= 0xfffff) {
            return;
        }
        if (addr >= 0x100000 && addr <= 0x10ffff) {
            workRam[(addr & 0xffff) >>> 1] = value;
            return;
        }
        if (addr >= 0x500000 && addr <= 0x500fff) {
            spriteRam[(addr & 0xfff) >>> 1] = value;
            return;
        }
        if (addr == 0x600000) {
            eeprom.diWrite((value >> 2) & 1);
            eeprom.csWrite(value & 1);
            eeprom.clkWrite((value >> 1) & 1);
            selectOkiBank((value >> 6) & 1);
            return;
        }
        if (addr == 0x700000) {
            scrollX = value & 0x1ff;
            return;
        }
        if (addr >= 0x800000 && addr <= 0x803fff) {
            writePalette((addr & 0x3fff) >>> 1, value);
            return;
        }
        if (addr >= 0x900000 && addr <= 0x907fff) {
            tileRam[(addr & 0x7fff) >>> 1] = value;
            return;
        }
        if (addr == 0xa00000) {
            oki.write(value & 0xff);
        }
    }

    private void writePalette(int index, int value) {
        if (index < 0 || index >= paletteRam.length) {
            return;
        }
        paletteRam[index] = value;
        int r = pal5bit(value >> 10);
        int g = pal5bit(value >> 5);
        int b = pal5bit(value);
        palette[index] = argb(r, g, b);
    }

    private int palColor(int index) {
        if (index < 0 || index >= palette.length) {
            return 0xff000000;
        }
        return palette[index];
    }

    private void onMainCycles(int cycles) {
        okiAccumulator += (long) cycles * oki.sampleFrequency();
        while (okiAccumulator >= MAIN_CLOCK) {
            okiAccumulator -= MAIN_CLOCK;
            lastOki = oki.update();
        }
        audioAccumulator += (long) cycles * sampleRate();
        while (audioAccumulator >= MAIN_CLOCK) {
            audioAccumulator -= MAIN_CLOCK;
            int sample = Math.max(-32768, Math.min(32767, lastOki));
            audio.add((short) sample);
        }
    }

    private void selectOkiBank(int bank) {
        bank &= 1;
        if (bank == lastOkiBank) {
            return;
        }
        lastOkiBank = bank;
        oki.setRom(Arrays.copyOf(soundBanks[bank], soundBanks[bank].length));
    }

    private boolean loadProgram(String romPath, StringBuilder error) {
        RomLoader loader = new RomLoader();
        if (!loader.open(romPath, error)) {
            return false;
        }
        try {
            byte[] even = new byte[0x80000];
            byte[] odd = new byte[0x80000];
            List<RomEntry> evenEntries = game == Game.PIRATES ? PIRATES_PROGRAM_LOW : GENIX_PROGRAM_LOW;
            List<RomEntry> oddEntries = game == Game.PIRATES ? PIRATES_PROGRAM_HIGH : GENIX_PROGRAM_HIGH;
            if (!loader.load(evenEntries, even, error)) {
                return false;
            }
            if (!loader.load(oddEntries, odd, error)) {
                return false;
            }

            // roms_load16w: p:0 (even) is the high byte of each word, p:1 (odd) the low byte.
            int[] raw = new int[0x80000];
            for (int i = 0; i < raw.length; i++) {
                raw[i] = ((even[i] & 0xff) << 8) | (odd[i] & 0xff);
            }

            for (int f = 0; f < 0x80000; f++) {
                int vl = programDataLow(raw[programAddrLow(f)] & 0xff);
                int vr = programDataHigh((raw[programAddrHigh(f)] >>> 8) & 0xff);
                rom[f] = ((vr << 8) | vl) & 0xffff;
            }
            warnings.addAll(loader.warnings());
            return true;
        } finally {
            loader.close();
        }
    }

    private boolean loadSound(String romPath, StringBuilder error) {
        RomLoader loader = new RomLoader();
        if (!loader.open(romPath, error)) {
            return false;
        }
        try {
            byte[] raw = new byte[0x80000];
            if (!loader.load(game == Game.PIRATES ? PIRATES_OKI : GENIX_OKI, raw, error)) {
                return false;
            }
            byte[] decoded = new byte[0x80000];
            for (int f = 0; f < 0x80000; f++) {
                decoded[okiAddr(f)] = (byte) okiData(raw[f] & 0xff);
            }
            System.arraycopy(decoded, 0, soundBanks[0], 0, 0x40000);
            System.arraycopy(decoded, 0x40000, soundBanks[1], 0, 0x40000);
            warnings.addAll(loader.warnings());
            return true;
        } finally {
            loader.close();
        }
    }

    private boolean loadGraphics(String romPath, StringBuilder error) {
        RomLoader loader = new RomLoader();
        if (!loader.open(romPath, error)) {
            return false;
        }
        try {
            {
                List<RomEntry> entries = game == Game.PIRATES ? PIRATES_GFX : GENIX_GFX;
                byte[] raw = new byte[PLANE_SIZE * 4];
                if (!loader.load(entries, raw, error)) {
                    return false;
                }
                byte[] decoded = new byte[PLANE_SIZE * 4];
                IntUnaryOperator[] planeFn = {
                        Pirates::tileDataPlane0, Pirates::tileDataPlane1,
                        Pirates::tileDataPlane2, Pirates::tileDataPlane3
                };
                decryptPlanes(decoded, raw, Pirates::tileAddr, planeFn);

                GfxLayout layout = new GfxLayout();
                layout.width = 8;
                layout.height = 8;
                layout.total = 0x10000;
                layout.planes = 4;
                layout.charIncrement = 8 * 8;
                // gfx_set_desc_data(4, 0, 8*8, $180000*8, $100000*8, $80000*8, 0)
                layout.planeOffsets = new int[] {0x180000 * 8, 0x100000 * 8, 0x80000 * 8, 0};
                layout.xOffsets = new int[] {7, 6, 5, 4, 3, 2, 1, 0};
                layout.yOffsets = new int[] {0, 8, 16, 24, 32, 40, 48, 56};
                tiles.decode(layout, decoded);
            }

            {
                List<RomEntry> entries = game == Game.PIRATES ? PIRATES_SPRITES : GENIX_SPRITES;
                byte[] raw = new byte[PLANE_SIZE * 4];
                if (!loader.load(entries, raw, error)) {
                    return false;
                }
                byte[] decoded = new byte[PLANE_SIZE * 4];
                IntUnaryOperator[] planeFn = {
                        Pirates::spriteDataPlane0, Pirates::spriteDataPlane1,
                        Pirates::spriteDataPlane2, Pirates::spriteDataPlane3
                };
                decryptPlanes(decoded, raw, Pirates::spriteAddr, planeFn);

                GfxLayout layout = new GfxLayout();
                layout.width = 16;
                layout.height = 16;
                layout.total = 0x4000;
                layout.planes = 4;
                layout.charIncrement = 16 * 16;
                layout.planeOffsets = new int[] {0x180000 * 8, 0x100000 * 8, 0x80000 * 8, 0};
                layout.xOffsets = new int[] {7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8};
                layout.yOffsets = new int[] {
                        0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240
                };
                sprites.decode(layout, decoded);
            }
            warnings.addAll(loader.warnings());
            return true;
        } finally {
            loader.close();
        }
    }

    private void drawLayer(int baseWord, int widthTiles, int heightTiles, int colorAdd,
            boolean transparent, int scroll, int destWidth) {
        int layerW = widthTiles * 8;
        for (int f = 0; f < widthTiles * heightTiles; f++) {
            int tx = f / heightTiles;
            int ty = f % heightTiles;
            int entry = baseWord + f * 2;
            if (entry + 1 >= tileRam.length) {
                continue;
            }
            int nchar = tileRam[entry] & 0xffff;
            int palBase = ((tileRam[entry + 1] & 0x1ff) + colorAdd) << 4;

            int baseX = Math.floorMod(tx * 8 - scroll, layerW);
            int baseY = ty * 8;
            for (int y = 0; y < 8; y++) {
                int fy = baseY + y;
                if (fy < 0 || fy >= 256) {
                    continue;
                }
                for (int x = 0; x < 8; x++) {
                    int p = tiles.pen(nchar, y * 8 + x);
                    if (transparent && p == 0) {
                        continue;
                    }
                    int fx = baseX + x;
                    if (layerW != 512) {
                        if (fx < 0 || fx >= destWidth) {
                            continue;
                        }
                    } else {
                        fx %= 512;
                    }
                    if (fx < 0 || fx >= 512) {
                        continue;
                    }
                    canvas[fy * 512 + fx] = palColor(palBase + p);
                }
            }
        }
    }

    private void drawSprites() {
        for (int f = 0; f <= 0x1fd; f++) {
            int base = f * 4;
            if (base + 6 >= spriteRam.length) {
                break;
            }
            int syRaw = spriteRam[base + 3] & 0xffff;
            if ((syRaw & 0x8000) != 0) {
                break;
            }

            int sx = (spriteRam[base + 5] & 0xffff) - 32;
            int atrib = spriteRam[base + 6] & 0xffff;
            int nchar = atrib >>> 2;
            boolean flipX = (atrib & 2) != 0;
            boolean flipY = (atrib & 1) != 0;
            int palBase = ((spriteRam[base + 4] & 0xff) << 4) + 0x1800;
            int sy = (0x00f2 - syRaw) & 0xffff;

            for (int y = 0; y < 16; y++) {
                int fy = sy + y;
                if (fy < 0 || fy >= 256) {
                    continue;
                }
                int sySrc = flipY ? 15 - y : y;
                for (int x = 0; x < 16; x++) {
                    int p = sprites.pen(nchar, sySrc * 16 + (flipX ? 15 - x : x));
                    if (p == 0) {
                        continue;
                    }
                    int fx = sx + x;
                    if (fx < 0 || fx >= 512) {
                        continue;
                    }
                    canvas[fy * 512 + fx] = palColor(palBase + p);
                }
            }
        }
    }

    private void renderFrame() {
        // Pascal compose: BG (opaque) then FG onto the 512-wide playfield, sprites
        // on top of that, then the 288-wide text layer last.
        drawLayer(0x1540, 64, 32, 0x100, false, scrollX, 512);
        drawLayer(0x9c0, 64, 32, 0x080, true, scrollX, 512);
        drawSprites();
        drawLayer(0xc0, 36, 32, 0x000, true, 0, 288);

        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            System.arraycopy(canvas, (y + 16) * 512, framebuffer, y * SCREEN_WIDTH, SCREEN_WIDTH);
        }
    }

    private static int pal5bit(int n) {
        n &= 0x1f;
        return n * 255 / 31;
    }

    private static int argb(int r, int g, int b) {
        return 0xff000000 | (r << 16) | (g << 8) | b;
    }

    private static int bitswap(int value, int... bits) {
        int out = 0;
        int shift = bits.length - 1;
        for (int b : bits) {
            out |= ((value >>> b) & 1) << shift;
            --shift;
        }
        return out;
    }

    private static int programAddrLow(int f) {
        return bitswap(f, 23, 22, 21, 20, 19, 18, 4, 8, 3, 14, 2, 15, 17, 0, 9, 13, 10, 5, 16, 7, 12, 6, 1, 11);
    }

    private static int programAddrHigh(int f) {
        return bitswap(f, 23, 22, 21, 20, 19, 18, 4, 10, 1, 11, 12, 5, 9, 17, 14, 0, 13, 6, 15, 8, 3, 16, 7, 2);
    }

    private static int programDataLow(int v) {
        return bitswap(v, 4, 2, 7, 1, 6, 5, 0, 3);
    }

    private static int programDataHigh(int v) {
        return bitswap(v, 1, 4, 7, 0, 3, 5, 6, 2);
    }

    private static int okiAddr(int f) {
        return bitswap(f, 23, 22, 21, 20, 19, 10, 16, 13, 8, 4, 7, 11, 14, 17, 12, 6, 2, 0, 5, 18, 15, 3, 1, 9);
    }

    private static int okiData(int v) {
        return bitswap(v, 2, 3, 4, 0, 7, 5, 1, 6);
    }

    private static int tileAddr(int f) {
        return bitswap(f, 23, 22, 21, 20, 19, 18, 10, 2, 5, 9, 7, 13, 16, 14, 11, 4, 1, 6, 12, 17, 3, 0, 15, 8);
    }

    private static int tileDataPlane0(int v) {
        return bitswap(v, 2, 3, 4, 0, 7, 5, 1, 6);
    }

    private static int tileDataPlane1(int v) {
        return bitswap(v, 4, 2, 7, 1, 6, 5, 0, 3);
    }

    private static int tileDataPlane2(int v) {
        return bitswap(v, 1, 4, 7, 0, 3, 5, 6, 2);
    }

    private static int tileDataPlane3(int v) {
        return bitswap(v, 2, 3, 4, 0, 7, 5, 1, 6);
    }

    private static int spriteAddr(int f) {
        return bitswap(f, 23, 22, 21, 20, 19, 18, 17, 5, 12, 14, 8, 3, 0, 7, 9, 16, 4, 2, 6, 11, 13, 1, 10, 15);
    }

    private static int spriteDataPlane0(int v) {
        return bitswap(v, 4, 2, 7, 1, 6, 5, 0, 3);
    }

    private static int spriteDataPlane1(int v) {
        return bitswap(v, 1, 4, 7, 0, 3, 5, 6, 2);
    }

    private static int spriteDataPlane2(int v) {
        return bitswap(v, 2, 3, 4, 0, 7, 5, 1, 6);
    }

    private static int spriteDataPlane3(int v) {
        return bitswap(v, 4, 2, 7, 1, 6, 5, 0, 3);
    }

    private static void decryptPlanes(byte[] dest, byte[] raw, IntUnaryOperator addrFn,
            IntUnaryOperator[] planeFn) {
        Arrays.fill(dest, (byte) 0);
        for (int f = 0; f < PLANE_SIZE; f++) {
            int addr = addrFn.applyAsInt(f);
            for (int plane = 0; plane < 4; plane++) {
                int src = plane * PLANE_SIZE + f;
                int value = src < raw.length ? (raw[src] & 0xff) : 0;
                dest[plane * PLANE_SIZE + addr] = (byte) planeFn[plane].applyAsInt(value);
            }
        }
    }
}
