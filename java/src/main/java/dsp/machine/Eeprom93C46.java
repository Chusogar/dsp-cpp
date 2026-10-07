package dsp.machine;

/**
 * 93C46 serial EEPROM, ported from dsp-cpp {@code machine/eeprom93c46.cpp}
 * (eepromser.pas).
 *
 * The chip is wired as 128 x 8 (CPS1) or 64 x 16 (Pirates / Genix). Command
 * address width, cell width and the in-memory packing of each cell all follow
 * {@code eepromser_chip.create(E93C46, bits)}.
 */
public final class Eeprom93C46 {
    private enum State {
        IN_RESET,
        WAIT_FOR_START_BIT,
        WAIT_FOR_COMMAND,
        READING_DATA,
        WAIT_FOR_DATA,
        WAIT_FOR_COMPLETION
    }

    private enum Command {
        INVALID,
        READ,
        WRITE,
        ERASE,
        LOCK,
        UNLOCK,
        WRITE_ALL,
        ERASE_ALL
    }

    private static final int CS_RISING = 1 << 0;
    private static final int CS_FALLING = 1 << 1;
    private static final int CLK_RISING = 1 << 2;
    private static final int CLK_FALLING = 1 << 3;

    private int dataBits = 8;
    private int commandAddressBits = 7;
    private int addressBits = 7;

    private State state = State.IN_RESET;
    private Command command = Command.INVALID;
    private int csState;
    private int clkState;
    private int diState;
    private int bitsAccum;
    private int shiftRegister;
    private int commandAddressAccum;
    private int address;
    private boolean locked = true;
    private final byte[] data = new byte[0x80];

    public Eeprom93C46() {
        this(8);
    }

    public Eeprom93C46(int dataBits) {
        java.util.Arrays.fill(data, (byte) 0xff);
        this.dataBits = (dataBits == 16) ? 16 : 8;
        if (this.dataBits == 16) {
            commandAddressBits = 6;
            addressBits = calcAddressBits(64);
        } else {
            commandAddressBits = 7;
            addressBits = calcAddressBits(128);
        }
    }

    public void reset() {
        state = State.IN_RESET;
        locked = true;
        bitsAccum = 0;
        commandAddressAccum = 0;
        command = Command.INVALID;
        address = 0;
        shiftRegister = 0;
    }

    public int doRead() {
        if (state == State.READING_DATA && (shiftRegister & 0x80000000) == 0) {
            return 0;
        }
        return 1;
    }

    public void csWrite(int value) {
        value &= 1;
        if (value == csState) {
            return;
        }
        csState = value;
        handleEvent(csState != 0 ? CS_RISING : CS_FALLING);
    }

    public void clkWrite(int value) {
        value &= 1;
        if (value == clkState) {
            return;
        }
        clkState = value;
        handleEvent(clkState != 0 ? CLK_RISING : CLK_FALLING);
    }

    public void diWrite(int value) {
        diState = value & 1;
    }

    public int dataBits() {
        return dataBits;
    }

    public int commandAddressBits() {
        return commandAddressBits;
    }

    public int addressBits() {
        return addressBits;
    }

    public byte[] data() {
        return data;
    }

    private void writeCell(int address, int value) {
        if (dataBits == 16) {
            int index = (address & 0x3f) * 2;
            data[index] = (byte) (value >> 8);
            data[index + 1] = (byte) value;
            return;
        }
        data[address & 0x7f] = (byte) value;
    }

    private int readCell(int address) {
        if (dataBits == 16) {
            int index = (address & 0x3f) * 2;
            return ((data[index] & 0xff) << 8) | (data[index + 1] & 0xff);
        }
        return data[address & 0x7f] & 0xff;
    }

    private void parseCommandAndAddress() {
        command = Command.INVALID;
        address = commandAddressAccum & ((1 << commandAddressBits) - 1);
        switch (commandAddressAccum >>> commandAddressBits) {
            case 0:
                switch (address >> (commandAddressBits - 2)) {
                    case 0:
                        command = Command.LOCK;
                        break;
                    case 1:
                        command = Command.WRITE_ALL;
                        break;
                    case 2:
                        command = Command.ERASE_ALL;
                        break;
                    case 3:
                        command = Command.UNLOCK;
                        break;
                    default:
                        break;
                }
                address = 0;
                break;
            case 1:
                command = Command.WRITE;
                break;
            case 2:
                command = Command.READ;
                break;
            case 3:
                command = Command.ERASE;
                break;
            default:
                break;
        }
    }

    private void executeWriteCommand() {
        switch (command) {
            case WRITE:
                if (locked) {
                    state = State.IN_RESET;
                    return;
                }
                writeCell(address, shiftRegister);
                state = State.WAIT_FOR_COMPLETION;
                break;
            case WRITE_ALL:
                if (locked) {
                    state = State.IN_RESET;
                    return;
                }
                for (int cell = 0; cell < (1 << addressBits); cell++) {
                    writeCell(cell, readCell(cell) & shiftRegister);
                }
                state = State.WAIT_FOR_COMPLETION;
                break;
            default:
                break;
        }
    }

    private void executeCommand() {
        parseCommandAndAddress();
        bitsAccum = 0;
        switch (command) {
            case READ:
                shiftRegister = 0;
                state = State.READING_DATA;
                break;
            case WRITE:
            case WRITE_ALL:
                shiftRegister = 0;
                state = State.WAIT_FOR_DATA;
                break;
            case ERASE:
                if (locked) {
                    state = State.IN_RESET;
                    return;
                }
                writeCell(address, 0xffff);
                state = State.WAIT_FOR_COMPLETION;
                break;
            case LOCK:
                locked = true;
                state = State.IN_RESET;
                break;
            case UNLOCK:
                locked = false;
                state = State.IN_RESET;
                break;
            case ERASE_ALL:
                if (locked) {
                    state = State.IN_RESET;
                    return;
                }
                for (int cell = 0; cell < (1 << addressBits); cell++) {
                    writeCell(cell, 0xffff);
                }
                state = State.WAIT_FOR_COMPLETION;
                break;
            default:
                break;
        }
    }

    private void handleEvent(int event) {
        switch (state) {
            case IN_RESET:
                if (event == CS_RISING) {
                    state = State.WAIT_FOR_START_BIT;
                }
                break;
            case WAIT_FOR_START_BIT:
                if (event == CLK_RISING && diState != 0) {
                    commandAddressAccum = 0;
                    bitsAccum = 0;
                    state = State.WAIT_FOR_COMMAND;
                } else if (event == CS_FALLING) {
                    state = State.IN_RESET;
                }
                break;
            case WAIT_FOR_COMMAND:
                if (event == CLK_RISING) {
                    commandAddressAccum = (commandAddressAccum << 1) | diState;
                    bitsAccum++;
                    if (bitsAccum == 2 + commandAddressBits) {
                        executeCommand();
                    }
                } else if (event == CS_FALLING) {
                    state = State.IN_RESET;
                }
                break;
            case READING_DATA:
                if (event == CLK_RISING) {
                    int bitIndex = bitsAccum;
                    bitsAccum++;
                    if ((bitIndex % dataBits) == 0 && bitIndex == 0) {
                        int cell = (address + bitsAccum / dataBits) & ((1 << addressBits) - 1);
                        shiftRegister = readCell(cell) << (32 - dataBits);
                    } else {
                        shiftRegister = (shiftRegister << 1) | 1;
                    }
                } else if (event == CS_FALLING) {
                    state = State.IN_RESET;
                }
                break;
            case WAIT_FOR_DATA:
                if (event == CLK_RISING) {
                    shiftRegister = (shiftRegister << 1) | diState;
                    bitsAccum++;
                    if (bitsAccum == dataBits) {
                        executeWriteCommand();
                    }
                } else if (event == CS_FALLING) {
                    state = State.IN_RESET;
                }
                break;
            case WAIT_FOR_COMPLETION:
                if (event == CS_FALLING) {
                    state = State.IN_RESET;
                }
                break;
            default:
                break;
        }
    }

    private static int calcAddressBits(int cells) {
        int value = cells - 1;
        int bits = 0;
        while (value != 0) {
            value >>= 1;
            bits++;
        }
        return bits;
    }
}
