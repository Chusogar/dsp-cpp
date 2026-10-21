package dsp.video;

/**
 * Port of pal_engine.pas {@code compute_resistor_weights()}. All networks share
 * the same scale factor when {@code scaler} is negative (autoscale).
 */
public final class Palette {
    private Palette() {}

    public static double[][] computeResistorWeights(double minValue, double maxValue, double scaler,
                                                    ResistorNet[] nets) {
        double[][] weights = new double[nets.length][];
        double[] maxOut = new double[nets.length];
        for (int net = 0; net < nets.length; net++) {
            ResistorNet current = nets[net];
            int count = current.resistances.length;
            weights[net] = new double[count];
            for (int n = 0; n < count; n++) {
                double r0 = (current.pulldown == 0) ? 1.0 / 1e12 : 1.0 / current.pulldown;
                double r1 = (current.pullup == 0) ? 1.0 / 1e12 : 1.0 / current.pullup;
                for (int j = 0; j < count; j++) {
                    double resistance = current.resistances[j];
                    if (resistance == 0.0) {
                        continue;
                    }
                    if (j == n) {
                        r1 += 1.0 / resistance;
                    } else {
                        r0 += 1.0 / resistance;
                    }
                }
                r0 = 1.0 / r0;
                r1 = 1.0 / r1;
                double vout = (maxValue - minValue) * r0 / (r1 + r0) + minValue;
                weights[net][n] = clamp(vout, minValue, maxValue);
                maxOut[net] += weights[net][n];
            }
        }

        double scale = scaler;
        if (scaler < 0.0) {
            double max = 0.0;
            for (double value : maxOut) {
                max = Math.max(max, value);
            }
            scale = (max != 0.0) ? maxValue / max : 0.0;
        }
        for (double[] netWeights : weights) {
            for (int i = 0; i < netWeights.length; i++) {
                netWeights[i] *= scale;
            }
        }
        return weights;
    }

    public static int combineWeights(double[] weights, int[] bits) {
        double result = 0.5;
        int n = Math.min(bits.length, weights.length);
        for (int i = 0; i < n; i++) {
            result += weights[i] * bits[i];
        }
        return (int) Math.min(result, 255.0);
    }

    private static double clamp(double value, double min, double max) {
        return Math.max(min, Math.min(max, value));
    }
}
