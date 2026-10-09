package dsp.cpu;

@FunctionalInterface
public interface MemoryWrite {
    void write(int address, int value);
}
