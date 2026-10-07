package dsp.sound;

/**
 * OKI MSM6295 ADPCM sample player, ported from dsp-cpp {@code sound/okim6295.cpp}
 * (oki6295.pas).
 */
public final class OKIM6295 {
    public static final int VOICES = 4;

    private static final int[] INDEX_SHIFT = {-1, -1, -1, -1, 2, 4, 6, 8};

    /** Only nine of the sixteen volume steps are used; the rest mute the voice. */
    private static final int[] VOLUME_TABLE = {
            0x20, 0x16, 0x10, 0x0b, 0x08, 0x06, 0x04, 0x03,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    private static final float[] DIFF_TABLE = buildDiffTable();

    private static final class Voice {
        boolean playing;
        int baseOffset;
        int sample;
        int count;
        int signal = -2;
        int step;
        int volume;
    }

    private final int clock;
    private int divisor;
    private byte[] rom = new byte[0];
    private final Voice[] voices = new Voice[VOICES];
    private int command = -1;

    /**
     * {@code pin7High} selects the /132 divider instead of /165.
     */
    public OKIM6295(int clock, boolean pin7High) {
        this.clock = clock;
        this.divisor = pin7High ? 132 : 165;
        for (int i = 0; i < VOICES; i++) {
            voices[i] = new Voice();
        }
        reset();
    }

    public void setRom(byte[] rom) {
        this.rom = rom != null ? rom : new byte[0];
    }

    /** CPS1 sound maps $F006 to pin 7 (divisor 132 when high, 165 when low). */
    public void setPin7(boolean high) {
        divisor = high ? 132 : 165;
    }

    public void reset() {
        command = -1;
        for (Voice voice : voices) {
            voice.playing = false;
            voice.baseOffset = 0;
            voice.sample = 0;
            voice.count = 0;
            voice.volume = 0;
            resetAdpcm(voice);
        }
    }

    public int read() {
        int result = 0xf0;
        for (int index = 0; index < VOICES; index++) {
            if (voices[index].playing) {
                result |= 1 << index;
            }
        }
        return result;
    }

    public void write(int value) {
        value &= 0xff;
        if (command != -1) {
            int mask = value >> 4;
            for (int index = 0; index < VOICES; index++, mask >>= 1) {
                if ((mask & 1) == 0) {
                    continue;
                }
                Voice voice = voices[index];
                if (voice.playing) {
                    continue;
                }
                int base = command * 8;
                int start = ((romByte(base) << 16) | (romByte(base + 1) << 8) | romByte(base + 2))
                        & 0x3ffff;
                int stop = ((romByte(base + 3) << 16) | (romByte(base + 4) << 8) | romByte(base + 5))
                        & 0x3ffff;
                if (Integer.compareUnsigned(start, stop) >= 0) {
                    continue;
                }
                voice.playing = true;
                voice.baseOffset = start;
                voice.sample = 0;
                voice.count = 2 * (stop - start + 1);
                resetAdpcm(voice);
                voice.volume = VOLUME_TABLE[value & 0x0f];
            }
            command = -1;
            return;
        }
        if ((value & 0x80) != 0) {
            command = value & 0x7f;
            return;
        }
        int mask = value >> 3;
        for (int index = 0; index < VOICES; index++, mask >>= 1) {
            if ((mask & 1) != 0) {
                voices[index].playing = false;
            }
        }
    }

    /** Frequency of the internal sample generator. */
    public int sampleFrequency() {
        return clock / divisor;
    }

    /** Generates the next sample, in the 16 bit range used by the other chips. */
    public int update() {
        int out = 0;
        for (Voice voice : voices) {
            if (voice.playing) {
                out += generateAdpcm(voice);
            }
        }
        return Math.max(-32767, Math.min(32767, out));
    }

    private void resetAdpcm(Voice voice) {
        voice.signal = -2;
        voice.step = 0;
    }

    private int clockAdpcm(Voice voice, int nibble) {
        voice.signal += (int) DIFF_TABLE[voice.step * 16 + (nibble & 0x0f)];
        voice.signal = Math.max(-2048, Math.min(2047, voice.signal));
        voice.step += INDEX_SHIFT[nibble & 0x07];
        voice.step = Math.max(0, Math.min(48, voice.step));
        return voice.signal;
    }

    private int generateAdpcm(Voice voice) {
        int offset = voice.baseOffset + (voice.sample >>> 1);
        int b = offset < rom.length ? (rom[offset] & 0xff) : 0;
        int nibble = (voice.sample & 1) == 0 ? (b >> 4) : (b & 0x0f);
        voice.sample++;
        if (voice.sample >= voice.count) {
            voice.playing = false;
        }
        return clockAdpcm(voice, nibble) * (voice.volume >> 1);
    }

    private int romByte(int offset) {
        return offset < rom.length ? (rom[offset] & 0xff) : 0;
    }

    private static float[] buildDiffTable() {
        int[][] nibbleToBit = {
                {1, 0, 0, 0}, {1, 0, 0, 1}, {1, 0, 1, 0}, {1, 0, 1, 1},
                {1, 1, 0, 0}, {1, 1, 0, 1}, {1, 1, 1, 0}, {1, 1, 1, 1},
                {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
                {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
        };
        float[] values = new float[49 * 16];
        for (int step = 0; step <= 48; step++) {
            int stepValue = (int) Math.floor(16.0 * Math.pow(11.0 / 10.0, step));
            for (int nibble = 0; nibble < 16; nibble++) {
                int[] bits = nibbleToBit[nibble];
                values[step * 16 + nibble] = (float) (bits[0] * (stepValue * bits[1]
                        + stepValue / 2.0 * bits[2]
                        + stepValue / 4.0 * bits[3]
                        + stepValue / 8.0));
            }
        }
        return values;
    }
}
