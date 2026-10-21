package dsp.video;

/**
 * MAME-style graphics layout: every offset is expressed in bits.
 * Ported from dsp-cpp {@code video/gfx.h}.
 */
public final class GfxLayout {
    public int width = 8;
    public int height = 8;
    public int total;
    public int planes = 1;
    public int charIncrement;
    public boolean lsbFirst;
    public boolean rotateCw;
    public boolean rotateCcw;
    public int[] planeOffsets = new int[0];
    public int[] xOffsets = new int[0];
    public int[] yOffsets = new int[0];
}
