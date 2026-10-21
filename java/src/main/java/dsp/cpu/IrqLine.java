package dsp.cpu;

/** Interrupt line states, same meaning as CLEAR/ASSERT/HOLD/PULSE_LINE in cpu_misc.pas. */
public enum IrqLine {
    CLEAR,
    ASSERT,
    HOLD,
    PULSE
}
