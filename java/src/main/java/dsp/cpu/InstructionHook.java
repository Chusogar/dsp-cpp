package dsp.cpu;

@FunctionalInterface
public interface InstructionHook {
    void beforeOpcode(int pc);
}
