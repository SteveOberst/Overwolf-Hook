package dev.sxlver.overwolfhook;

import java.io.IOException;
import java.nio.file.*;
import java.security.*;
import java.util.*;

/** Stable content-addressed paths: capture DLLs can remain loaded in Overwolf after a session ends. */
final class NativeBinaries {
    static Path directory() throws IOException {
        if (!System.getProperty("os.name").startsWith("Windows") ||
            !Set.of("amd64", "x86_64").contains(System.getProperty("os.arch")))
            throw new UnsupportedOperationException("Overwolf Hook requires Windows x64");
        var contents = new LinkedHashMap<String, byte[]>();
        MessageDigest digest;
        try { digest = MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException impossible) { throw new AssertionError(impossible); }
        for (String name : List.of("owh_client.dll", "owh_capture.dll", "owh_probe.exe", "owh_info.exe")) {
            try (var stream = NativeBinaries.class.getResourceAsStream("/overwolf/native/windows-x64/" + name)) {
                if (stream == null) throw new IOException("Missing bundled native binary: " + name);
                byte[] bytes = stream.readAllBytes();
                contents.put(name, bytes);
                digest.update(bytes);
            }
        }
        var directory = Files.createDirectories(Path.of(System.getProperty("java.io.tmpdir"), "overwolf-hook", HexFormat.of().formatHex(digest.digest())));
        for (var entry : contents.entrySet()) {
            var destination = directory.resolve(entry.getKey());
            if (!Files.exists(destination)) {
                var temporary = Files.createTempFile(directory, "extract-", ".tmp");
                try {
                    Files.write(temporary, entry.getValue());
                    try { Files.move(temporary, destination, StandardCopyOption.ATOMIC_MOVE); }
                    catch (IOException race) {
                        // Windows may report AccessDenied rather than FileAlreadyExists
                        // when another JVM has already loaded the newly installed DLL.
                        if (!Files.exists(destination) || !Arrays.equals(Files.readAllBytes(destination), entry.getValue())) throw race;
                    }
                } finally { Files.deleteIfExists(temporary); }
            }
            if (!Arrays.equals(Files.readAllBytes(destination), entry.getValue()))
                throw new IOException("Native cache content mismatch: " + destination);
        }
        return directory;
    }
}
