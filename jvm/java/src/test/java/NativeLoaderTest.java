import dev.sxlver.overwolfhook.GameStateController;
import dev.sxlver.overwolfhook.OverwolfController;
import dev.sxlver.overwolfhook.OverwolfHook;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/** Isolated JVM: explicit selection must precede any default loader entry point. */
public final class NativeLoaderTest {
    public static void main(String[] args) throws Exception {
        if (args.length > 1 && args[1].equals("bundled")) {
            OverwolfHook.load();
            OverwolfHook.load();
            OverwolfHook.buildInfo();
            try (var sdk = OverwolfHook.open()) { sdk.gameState(); }
            System.out.println("PASS explicit bundled loading without attach");
            return;
        }
        if (args.length > 1 && args[1].equals("failures")) {
            try {
                OverwolfHook.load(Path.of(args[0]).resolve("missing-directory"));
                throw new AssertionError("A missing directory was accepted");
            } catch (IOException expected) {
                // Directory lookup fails before native loading starts.
            }
            Path incomplete = Files.createDirectories(Path.of(args[0]).resolve("incomplete"));
            try {
                OverwolfHook.load(incomplete);
                throw new AssertionError("A missing Agent payload was accepted");
            } catch (IOException expected) {
                if (!expected.getMessage().contains("owh_agent.dll")) throw expected;
            }
            Path invalid = Files.createDirectories(Path.of(args[0]).resolve("invalid"));
            copyPair(invalid);
            Files.writeString(invalid.resolve("owh_client.dll"), "Not a DLL");
            try {
                OverwolfHook.load(invalid);
                throw new AssertionError("An invalid Client DLL was accepted");
            } catch (UnsatisfiedLinkError expected) {
                // Preserve the JVM loader error instead of hiding it as success.
            }
            System.out.println("PASS missing directory, missing payload, and invalid Client loading failures");
            return;
        }
        Path selected = Path.of(args[0]).resolve("selected");
        Path other = Path.of(args[0]).resolve("other");
        for (Path directory : new Path[]{selected, other}) {
            Files.createDirectories(directory);
            copyPair(directory);
        }
        OverwolfHook.load(selected);
        OverwolfHook.load(selected.resolve("."));
        OverwolfHook.load();
        OverwolfHook.buildInfo();
        try (var first = OverwolfHook.open(selected);
             var same = OverwolfHook.open(selected);
             var defaults = OverwolfHook.open()) {
            OverwolfHook.buildInfo();
            GameStateController.open();
            OverwolfController.open();
            first.gameState();
            first.overwolf();
            try {
                OverwolfHook.load(other);
                throw new AssertionError("An explicit DLL switch was accepted");
            } catch (IllegalStateException expected) {
                // Selection is shared by explicit loads and all default factories.
            }
            try {
                OverwolfHook.open(other).close();
                throw new AssertionError("An explicit DLL switch was accepted");
            } catch (IllegalStateException expected) {
                // Even byte-identical DLLs at a second path cannot switch the loader.
            }
        }
        System.out.println("PASS explicit native selection and default reuse; no attach");
    }

    private static void copyPair(Path directory) throws IOException {
        for (String name : new String[]{"owh_client.dll", "owh_agent.dll"}) {
            try (var input = NativeLoaderTest.class.getResourceAsStream(
                    "/overwolf/native/windows-x64/" + name)) {
                if (input == null) throw new AssertionError("Missing " + name);
                Files.copy(input, directory.resolve(name),
                        StandardCopyOption.REPLACE_EXISTING);
            }
        }
    }
}
