package dsp.core;

/** One file in a MAME-style ROM set. {@code crc} of 0 skips the checksum. */
public final class RomEntry {
    public final String name;
    public final int length;
    public final int offset;
    public final int crc;

    public RomEntry(String name, int length, int offset, int crc) {
        this.name = name;
        this.length = length;
        this.offset = offset;
        this.crc = crc;
    }
}
