package dsp.frontend;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;

/** Uncompressed 24-bit BMP writer used by headless {@code --screenshot}. */
public final class BmpWriter {
    private BmpWriter() {}

    public static void write(Path path, int[] argb, int width, int height) throws IOException {
        int rowBytes = ((width * 3 + 3) / 4) * 4;
        int imageSize = rowBytes * height;
        int fileSize = 54 + imageSize;
        try (OutputStream out = Files.newOutputStream(path)) {
            write16(out, 0x4d42);
            write32(out, fileSize);
            write32(out, 0);
            write32(out, 54);
            write32(out, 40);
            write32(out, width);
            write32(out, height);
            write16(out, 1);
            write16(out, 24);
            write32(out, 0);
            write32(out, imageSize);
            write32(out, 2835);
            write32(out, 2835);
            write32(out, 0);
            write32(out, 0);
            byte[] row = new byte[rowBytes];
            for (int y = height - 1; y >= 0; y--) {
                int src = y * width;
                int dst = 0;
                for (int x = 0; x < width; x++) {
                    int pixel = argb[src + x];
                    row[dst++] = (byte) (pixel);
                    row[dst++] = (byte) (pixel >> 8);
                    row[dst++] = (byte) (pixel >> 16);
                }
                while (dst < rowBytes) {
                    row[dst++] = 0;
                }
                out.write(row);
            }
        }
    }

    private static void write16(OutputStream out, int value) throws IOException {
        out.write(value);
        out.write(value >> 8);
    }

    private static void write32(OutputStream out, int value) throws IOException {
        out.write(value);
        out.write(value >> 8);
        out.write(value >> 16);
        out.write(value >> 24);
    }
}
