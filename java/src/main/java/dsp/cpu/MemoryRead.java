package dsp.cpu;

@FunctionalInterface
public interface MemoryRead {
    int read(int address);
}
