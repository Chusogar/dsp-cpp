package dsp.core;

import java.util.ArrayList;
import java.util.List;

/**
 * Common interface implemented by every arcade driver, so the front end does not
 * need to know which game it is running. Ported from dsp-cpp {@code core/machine.h}.
 */
public abstract class Machine {
    protected final List<String> warnings = new ArrayList<>();

    public abstract boolean init(String romPath, StringBuilder error);

    public abstract void reset();

    /** Runs a full frame and renders it into the internal framebuffer. */
    public abstract void runFrame();

    public abstract void setInputs(MachineInputs inputs);

    /** DIP switch bank, 0 based. Unknown banks are ignored. */
    public abstract void setDipSwitch(int bank, int value);

    /** ARGB8888 framebuffer, {@code screenWidth() * screenHeight()} pixels. */
    public abstract int[] framebuffer();

    public abstract int screenWidth();

    public abstract int screenHeight();

    public int displayWidth() {
        return screenWidth();
    }

    public int displayHeight() {
        return screenHeight();
    }

    public boolean fitWindow() {
        return false;
    }

    public MachineOverlay screenOverlay() {
        return null;
    }

    public abstract double framesPerSecond();

    /** Consumes the audio samples generated so far (mono, signed 16 bit). */
    public abstract void drainAudio(List<Short> out);

    public abstract int sampleRate();

    public abstract String title();

    public boolean usesKeyboard() {
        return false;
    }

    public boolean usesPointer() {
        return false;
    }

    public boolean usesRelativePointer() {
        return false;
    }

    public boolean loadMedia(String path, StringBuilder error) {
        if (error != null) {
            error.setLength(0);
            error.append("this machine has no removable media");
        }
        return false;
    }

    public void tapeTogglePlay() {}

    public boolean tapeLoaded() {
        return false;
    }

    public List<String> warnings() {
        return warnings;
    }
}
