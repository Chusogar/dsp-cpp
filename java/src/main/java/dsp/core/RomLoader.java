package dsp.core;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Loads ROM files either from a plain directory or from a MAME-style zip file. */
public final class RomLoader {
    private Path path;
    private boolean zip;
    private ZipFile zipFile;
    private final Map<String, ZipEntry> zipIndex = new HashMap<>();
    private final List<String> warnings = new ArrayList<>();

    public boolean open(String romPath, StringBuilder error) {
        path = Path.of(romPath);
        zipIndex.clear();
        warnings.clear();
        closeQuietly();
        if (Files.isDirectory(path)) {
            zip = false;
            return true;
        }
        if (!Files.exists(path)) {
            if (error != null) {
                error.setLength(0);
                error.append("ROM path not found: ").append(path);
            }
            return false;
        }
        zip = true;
        try {
            zipFile = new ZipFile(path.toFile());
            Enumeration<? extends ZipEntry> entries = zipFile.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                if (entry.isDirectory()) {
                    continue;
                }
                zipIndex.put(entry.getName().toLowerCase(Locale.ROOT), entry);
            }
            return true;
        } catch (IOException e) {
            if (error != null) {
                error.setLength(0);
                error.append("cannot read ").append(path).append(": ").append(e.getMessage());
            }
            return false;
        }
    }

    public void close() {
        closeQuietly();
    }

    public List<String> warnings() {
        return warnings;
    }

    public List<String> filenames() {
        List<String> names = new ArrayList<>();
        if (!zip) {
            try (DirectoryStream<Path> stream = Files.newDirectoryStream(path)) {
                for (Path item : stream) {
                    if (Files.isRegularFile(item)) {
                        names.add(item.getFileName().toString().toLowerCase(Locale.ROOT));
                    }
                }
            } catch (IOException ignored) {
                // empty
            }
            return names;
        }
        for (String name : zipIndex.keySet()) {
            names.add(basename(name));
        }
        return names;
    }

    public boolean load(List<RomEntry> entries, byte[] dest, StringBuilder error) {
        for (RomEntry entry : entries) {
            if (entry.name == null) {
                continue;
            }
            byte[] data = null;
            String used = "";
            for (String candidate : entry.name.split("\\|")) {
                if (!candidate.isEmpty()) {
                    data = readFile(candidate);
                    if (data != null) {
                        used = candidate;
                        break;
                    }
                }
            }
            if (used.isEmpty() || data == null) {
                if (error != null) {
                    error.setLength(0);
                    error.append("missing ROM file: ").append(entry.name);
                }
                return false;
            }
            if (data.length != entry.length) {
                if (error != null) {
                    error.setLength(0);
                    error.append("wrong size for ").append(used)
                            .append(" (expected ").append(entry.length)
                            .append(", got ").append(data.length).append(")");
                }
                return false;
            }
            int crc = crc32Of(data);
            if (entry.crc != 0 && crc != entry.crc) {
                warnings.add(String.format("%s: CRC mismatch (expected %08x, got %08x)",
                        used, entry.crc, crc));
            }
            System.arraycopy(data, 0, dest, entry.offset, data.length);
        }
        return true;
    }

    public static int crc32Of(byte[] data) {
        CRC32 crc = new CRC32();
        crc.update(data);
        return (int) crc.getValue();
    }

    private byte[] readFile(String name) {
        if (!zip) {
            Path direct = path.resolve(name);
            if (Files.isRegularFile(direct)) {
                try {
                    return Files.readAllBytes(direct);
                } catch (IOException e) {
                    return null;
                }
            }
            try (DirectoryStream<Path> stream = Files.newDirectoryStream(path)) {
                String wanted = name.toLowerCase(Locale.ROOT);
                for (Path item : stream) {
                    if (Files.isRegularFile(item)
                            && item.getFileName().toString().toLowerCase(Locale.ROOT).equals(wanted)) {
                        return Files.readAllBytes(item);
                    }
                }
            } catch (IOException e) {
                return null;
            }
            return null;
        }

        String wanted = name.toLowerCase(Locale.ROOT);
        ZipEntry entry = zipIndex.get(wanted);
        if (entry == null) {
            for (Map.Entry<String, ZipEntry> kv : zipIndex.entrySet()) {
                if (basename(kv.getKey()).equals(wanted)) {
                    entry = kv.getValue();
                    break;
                }
            }
        }
        if (entry == null) {
            return null;
        }
        try (InputStream in = zipFile.getInputStream(entry)) {
            return readAll(in);
        } catch (IOException e) {
            return null;
        }
    }

    private static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        int n;
        while ((n = in.read(buffer)) >= 0) {
            out.write(buffer, 0, n);
        }
        return out.toByteArray();
    }

    private static String basename(String path) {
        int slash = Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
        return slash < 0 ? path : path.substring(slash + 1);
    }

    private void closeQuietly() {
        if (zipFile != null) {
            try {
                zipFile.close();
            } catch (IOException ignored) {
                // empty
            }
            zipFile = null;
        }
    }
}
