package dsp.cpu;

@FunctionalInterface
public interface CycleHandler {
    void onCycles(int cycles);
}
