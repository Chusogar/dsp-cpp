package dsp.cpu;

@FunctionalInterface
public interface ReturnHandler {
    void onReturn(boolean reti);
}
