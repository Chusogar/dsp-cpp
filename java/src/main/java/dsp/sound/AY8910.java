package dsp.sound;

import java.util.function.IntConsumer;
import java.util.function.IntSupplier;

/**
 * AY-3-8910 PSG, ported from dsp-cpp {@code sound/ay8910.cpp} (MAME's classic AY core).
 */
public final class AY8910 {
    public static final int SAMPLE_RATE = 44100;

    private static final int STEP = 0x1000;
    private static final int MAX_OUTPUT = 0x7fff;

    private static final int AY_AFINE = 0;
    private static final int AY_ACOARSE = 1;
    private static final int AY_BFINE = 2;
    private static final int AY_BCOARSE = 3;
    private static final int AY_CFINE = 4;
    private static final int AY_CCOARSE = 5;
    private static final int AY_NOISEPER = 6;
    private static final int AY_ENABLE = 7;
    private static final int AY_AVOL = 8;
    private static final int AY_BVOL = 9;
    private static final int AY_CVOL = 10;
    private static final int AY_EFINE = 11;
    private static final int AY_ECOARSE = 12;
    private static final int AY_ESHAPE = 13;
    private static final int AY_PORTA = 14;
    private static final int AY_PORTB = 15;

    private static final int[] VOL_TABLE = buildVolumeTable();

    private final int[] regs = new int[16];
    private int periodA, periodB, periodC, periodN, periodE;
    private int countA, countB, countC, countN, countE;
    private int volA, volB, volC, volE;
    private int envelopeA, envelopeB, envelopeC;
    private int outputA, outputB, outputC, outputN = 0xff;
    private int hold, alternate, attack, holding, rng = 1, updateStep;
    private int countEnv;
    private int lastEnable = -1;
    private int latch;
    private int clock;
    private final float amplitude;

    private IntSupplier portARead;
    private IntSupplier portBRead;
    private IntConsumer portAWrite;
    private IntConsumer portBWrite;

    public AY8910(int clock) {
        this(clock, 2.0f);
    }

    public AY8910(int clock, float amplitude) {
        this.clock = clock;
        this.amplitude = amplitude;
        updateStep = (int) ((long) STEP * SAMPLE_RATE * 8 / this.clock);
        periodA = periodB = periodC = periodE = periodN = updateStep;
        reset();
    }

    public void setClock(int clock) {
        if (clock == 0 || clock == this.clock) {
            return;
        }
        this.clock = clock;
        updateStep = (int) ((long) STEP * SAMPLE_RATE * 8 / this.clock);
        periodA = periodB = periodC = periodE = periodN = updateStep;
    }

    public void setPortHandlers(IntSupplier portARead, IntSupplier portBRead,
                                IntConsumer portAWrite, IntConsumer portBWrite) {
        this.portARead = portARead;
        this.portBRead = portBRead;
        this.portAWrite = portAWrite;
        this.portBWrite = portBWrite;
    }

    public void reset() {
        latch = 0;
        outputA = outputB = outputC = 0;
        outputN = 0xff;
        rng = 1;
        lastEnable = -1;
        for (int reg = 0; reg <= 13; reg++) {
            writeReg(reg, 0);
        }
    }

    public void control(int value) {
        latch = value & 0x0f;
    }

    public void write(int value) {
        writeReg(latch, value & 0xff);
    }

    public int read() {
        return readReg(latch);
    }

    public int update() {
        if ((regs[AY_ENABLE] & 0x01) != 0) {
            if (countA <= STEP) {
                countA += STEP;
            }
            outputA = 1;
        } else if (regs[AY_AVOL] == 0) {
            if (countA <= STEP) {
                countA += STEP;
            }
        }
        if ((regs[AY_ENABLE] & 0x02) != 0) {
            if (countB <= STEP) {
                countB += STEP;
            }
            outputB = 1;
        } else if (regs[AY_BVOL] == 0) {
            if (countB <= STEP) {
                countB += STEP;
            }
        }
        if ((regs[AY_ENABLE] & 0x04) != 0) {
            if (countC <= STEP) {
                countC += STEP;
            }
            outputC = 1;
        } else if (regs[AY_CVOL] == 0) {
            if (countC <= STEP) {
                countC += STEP;
            }
        }
        if ((regs[AY_ENABLE] & 0x38) == 0x38) {
            if (countN <= STEP) {
                countN += STEP;
            }
        }

        int outNoise = outputN | regs[AY_ENABLE];
        int mixedA = 0, mixedB = 0, mixedC = 0;
        int left = STEP;

        do {
            int nextEvent = Math.min(countN, left);

            if ((outNoise & 0x08) != 0) {
                if (outputA != 0) {
                    mixedA += countA;
                }
                countA -= nextEvent;
                while (countA <= 0) {
                    countA += periodA;
                    if (countA > 0) {
                        outputA ^= 1;
                        if (outputA != 0) {
                            mixedA += periodA;
                        }
                        break;
                    }
                    countA += periodA;
                    mixedA += periodA;
                }
                if (outputA != 0) {
                    mixedA -= countA;
                }
            } else {
                countA -= nextEvent;
                while (countA <= 0) {
                    countA += periodA;
                    if (countA > 0) {
                        outputA ^= 1;
                        break;
                    }
                    countA += periodA;
                }
            }

            if ((outNoise & 0x10) != 0) {
                if (outputB != 0) {
                    mixedB += countB;
                }
                countB -= nextEvent;
                while (countB <= 0) {
                    countB += periodB;
                    if (countB > 0) {
                        outputB ^= 1;
                        if (outputB != 0) {
                            mixedB += periodB;
                        }
                        break;
                    }
                    countB += periodB;
                    mixedB += periodB;
                }
                if (outputB != 0) {
                    mixedB -= countB;
                }
            } else {
                countB -= nextEvent;
                while (countB <= 0) {
                    countB += periodB;
                    if (countB > 0) {
                        outputB ^= 1;
                        break;
                    }
                    countB += periodB;
                }
            }

            if ((outNoise & 0x20) != 0) {
                if (outputC != 0) {
                    mixedC += countC;
                }
                countC -= nextEvent;
                while (countC <= 0) {
                    countC += periodC;
                    if (countC > 0) {
                        outputC ^= 1;
                        if (outputC != 0) {
                            mixedC += periodC;
                        }
                        break;
                    }
                    countC += periodC;
                    mixedC += periodC;
                }
                if (outputC != 0) {
                    mixedC -= countC;
                }
            } else {
                countC -= nextEvent;
                while (countC <= 0) {
                    countC += periodC;
                    if (countC > 0) {
                        outputC ^= 1;
                        break;
                    }
                    countC += periodC;
                }
            }

            countN -= nextEvent;
            if (countN <= 0) {
                if (((rng + 1) & 2) != 0) {
                    outputN = ~outputN;
                    outNoise = outputN | regs[AY_ENABLE];
                }
                if ((rng & 1) != 0) {
                    rng ^= 0x28000;
                }
                rng >>= 1;
                countN += periodN;
            }
            left -= nextEvent;
        } while (left > 0);

        if (holding == 0) {
            countE -= STEP;
            if (countE <= 0) {
                do {
                    countEnv--;
                    countE += periodE;
                } while (countE <= 0);
                if (countEnv < 0) {
                    if (hold != 0) {
                        if (alternate != 0) {
                            attack ^= 0x1f;
                        }
                        holding = 1;
                        countEnv = 0;
                    } else {
                        if (alternate != 0 && (countEnv & 0x20) != 0) {
                            attack ^= 0x1f;
                        }
                        countEnv &= 0x1f;
                    }
                }
                volE = VOL_TABLE[countEnv ^ attack];
                if (envelopeA != 0) {
                    volA = volE;
                }
                if (envelopeB != 0) {
                    volB = volE;
                }
                if (envelopeC != 0) {
                    volC = volE;
                }
            }
        }

        double mixed = (mixedA * (double) volA / STEP
                + mixedB * (double) volB / STEP
                + mixedC * (double) volC / STEP) * amplitude;
        return (int) mixed;
    }

    private void writeReg(int reg, int value) {
        regs[reg] = value;
        int old;
        switch (reg) {
            case AY_AFINE:
            case AY_ACOARSE:
                regs[AY_ACOARSE] &= 0x0f;
                old = periodA;
                periodA = (regs[AY_AFINE] + 256 * regs[AY_ACOARSE]) * updateStep;
                if (periodA == 0) {
                    periodA = updateStep;
                }
                countA += periodA - old;
                if (countA <= 0) {
                    countA = 1;
                }
                break;
            case AY_BFINE:
            case AY_BCOARSE:
                regs[AY_BCOARSE] &= 0x0f;
                old = periodB;
                periodB = (regs[AY_BFINE] + 256 * regs[AY_BCOARSE]) * updateStep;
                if (periodB == 0) {
                    periodB = updateStep;
                }
                countB += periodB - old;
                if (countB <= 0) {
                    countB = 1;
                }
                break;
            case AY_CFINE:
            case AY_CCOARSE:
                regs[AY_CCOARSE] &= 0x0f;
                old = periodC;
                periodC = (regs[AY_CFINE] + 256 * regs[AY_CCOARSE]) * updateStep;
                if (periodC == 0) {
                    periodC = updateStep;
                }
                countC += periodC - old;
                if (countC <= 0) {
                    countC = 1;
                }
                break;
            case AY_NOISEPER:
                regs[AY_NOISEPER] &= 0x1f;
                old = periodN;
                periodN = regs[AY_NOISEPER] * updateStep;
                if (periodN == 0) {
                    periodN = updateStep;
                }
                countN += periodN - old;
                if (countN <= 0) {
                    countN = 1;
                }
                break;
            case AY_ENABLE:
                if (lastEnable == -1 || ((lastEnable & 0x40) != (regs[AY_ENABLE] & 0x40))) {
                    if (portAWrite != null) {
                        portAWrite.accept((regs[AY_ENABLE] & 0x40) != 0 ? regs[AY_PORTA] : 0xff);
                    }
                }
                if (lastEnable == -1 || ((lastEnable & 0x80) != (regs[AY_ENABLE] & 0x80))) {
                    if (portBWrite != null) {
                        portBWrite.accept((regs[AY_ENABLE] & 0x80) != 0 ? regs[AY_PORTB] : 0xff);
                    }
                }
                lastEnable = regs[AY_ENABLE];
                break;
            case AY_AVOL:
                regs[AY_AVOL] &= 0x1f;
                envelopeA = regs[AY_AVOL] & 0x10;
                old = regs[AY_AVOL] != 0 ? regs[AY_AVOL] * 2 + 1 : 0;
                volA = envelopeA != 0 ? volE : VOL_TABLE[old];
                break;
            case AY_BVOL:
                regs[AY_BVOL] &= 0x1f;
                envelopeB = regs[AY_BVOL] & 0x10;
                old = regs[AY_BVOL] != 0 ? regs[AY_BVOL] * 2 + 1 : 0;
                volB = envelopeB != 0 ? volE : VOL_TABLE[old];
                break;
            case AY_CVOL:
                regs[AY_CVOL] &= 0x1f;
                envelopeC = regs[AY_CVOL] & 0x10;
                old = regs[AY_CVOL] != 0 ? regs[AY_CVOL] * 2 + 1 : 0;
                volC = envelopeC != 0 ? volE : VOL_TABLE[old];
                break;
            case AY_EFINE:
            case AY_ECOARSE:
                old = periodE;
                periodE = (regs[AY_EFINE] + 256 * regs[AY_ECOARSE]) * updateStep;
                if (periodE == 0) {
                    periodE = updateStep / 2;
                }
                countE += periodE - old;
                if (countE <= 0) {
                    countE = 1;
                }
                break;
            case AY_ESHAPE:
                regs[AY_ESHAPE] &= 0x0f;
                attack = (regs[AY_ESHAPE] & 0x04) != 0 ? 0x1f : 0;
                if ((regs[AY_ESHAPE] & 0x08) == 0) {
                    hold = 1;
                    alternate = attack;
                } else {
                    hold = regs[AY_ESHAPE] & 1;
                    alternate = regs[AY_ESHAPE] & 2;
                }
                countE = periodE;
                countEnv = 0x1f;
                holding = 0;
                volE = VOL_TABLE[countEnv ^ attack];
                if (envelopeA != 0) {
                    volA = volE;
                }
                if (envelopeB != 0) {
                    volB = volE;
                }
                if (envelopeC != 0) {
                    volC = volE;
                }
                break;
            case AY_PORTA:
                if (portAWrite != null) {
                    portAWrite.accept(value);
                }
                break;
            case AY_PORTB:
                if (portBWrite != null) {
                    portBWrite.accept(value);
                }
                break;
            default:
                break;
        }
    }

    private int readReg(int reg) {
        if (reg == AY_PORTA && portARead != null) {
            regs[AY_PORTA] = portARead.getAsInt() & 0xff;
        }
        if (reg == AY_PORTB && portBRead != null) {
            regs[AY_PORTB] = portBRead.getAsInt() & 0xff;
        }
        return regs[reg];
    }

    private static int[] buildVolumeTable() {
        int[] table = new int[32];
        double out = MAX_OUTPUT / 4.0;
        for (int i = 31; i >= 1; i--) {
            table[i] = (int) (out + 0.5);
            out /= 1.188502227;
        }
        table[0] = 0;
        return table;
    }
}
