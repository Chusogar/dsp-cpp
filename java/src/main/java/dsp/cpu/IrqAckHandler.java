package dsp.cpu;

@FunctionalInterface
public interface IrqAckHandler {
    void onIrqAck();
}
