package dsp.video;

/** One colour-PROM resistor network (pulldown / pullup in ohms). */
public final class ResistorNet {
    public final int[] resistances;
    public final int pulldown;
    public final int pullup;

    public ResistorNet(int[] resistances, int pulldown, int pullup) {
        this.resistances = resistances;
        this.pulldown = pulldown;
        this.pullup = pullup;
    }
}
