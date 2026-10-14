package dsp.video;

/** Decoded set of characters/sprites: one byte per pixel holding the colour index. */
public final class GfxSet {
    private int width;
    private int height;
    private int total;
    private byte[] pixels = new byte[0];

    public void decode(GfxLayout layout, byte[] rom) {
        create(layout.width, layout.height, layout.total);
        decodeElements(layout, rom, 0);
    }

    public void create(int width, int height, int total) {
        this.width = width;
        this.height = height;
        this.total = total;
        this.pixels = new byte[total * width * height];
    }

    public void decodeElements(GfxLayout layout, byte[] rom, int firstElement) {
        int index = firstElement * width * height;
        for (int n = 0; n < layout.total; n++) {
            int element = firstElement + n;
            int base = n * layout.charIncrement;
            for (int y = 0; y < layout.height; y++) {
                for (int x = 0; x < layout.width; x++) {
                    int value = 0;
                    for (int plane = 0; plane < layout.planes; plane++) {
                        int bit = getBit(rom,
                                layout.planeOffsets[plane]
                                        + layout.yOffsets[y]
                                        + layout.xOffsets[x]
                                        + base);
                        int shift = layout.lsbFirst ? plane : (layout.planes - 1 - plane);
                        value |= bit << shift;
                    }
                    pixels[index++] = (byte) value;
                }
            }
            if (layout.rotateCw) {
                int offset = element * width * height;
                byte[] source = new byte[width * height];
                System.arraycopy(pixels, offset, source, 0, source.length);
                for (int row = 0; row < height; row++) {
                    for (int column = 0; column < width; column++) {
                        pixels[offset + row * width + column] =
                                source[(height - 1 - column) * width + row];
                    }
                }
            }
            if (layout.rotateCcw) {
                int offset = element * width * height;
                byte[] source = new byte[width * height];
                System.arraycopy(pixels, offset, source, 0, source.length);
                for (int row = 0; row < height; row++) {
                    for (int column = 0; column < width; column++) {
                        pixels[offset + row * width + column] =
                                source[column * width + (width - 1 - row)];
                    }
                }
            }
        }
    }

    public int width() {
        return width;
    }

    public int height() {
        return height;
    }

    public int total() {
        return total;
    }

    public byte[] pixels() {
        return pixels;
    }

    public int elementOffset(int index) {
        return (index % total) * width * height;
    }

    private static int getBit(byte[] rom, int bitIndex) {
        int byteIndex = bitIndex >>> 3;
        if (byteIndex < 0 || byteIndex >= rom.length) {
            return 0;
        }
        return (rom[byteIndex] >> (7 - (bitIndex & 7))) & 1;
    }
}
