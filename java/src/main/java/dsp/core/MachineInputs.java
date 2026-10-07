package dsp.core;

/** Host inputs pushed into a machine every frame. */
public final class MachineInputs {
    public final InputState player1 = new InputState();
    public final InputState player2 = new InputState();
    public boolean coin1;
    public boolean coin2;
    public boolean service;
    public final boolean[] keys = new boolean[Key.values().length];

    public boolean hasPointer;
    public int pointerX;
    public int pointerY;
    public boolean pointerButton1;
    public boolean pointerButton2;
    public boolean pointerRelative;
    public boolean pointerResync;
    public int pointerDx;
    public int pointerDy;

    public boolean overlayPointer;
    public int overlayX;
    public int overlayY;
    public boolean overlayButton;

    public boolean key(Key value) {
        return keys[value.ordinal()];
    }
}
