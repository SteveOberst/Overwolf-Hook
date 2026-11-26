import dev.sxlver.overwolfhook.GameStateController;
import dev.sxlver.overwolfhook.OverwolfController;
import dev.sxlver.overwolfhook.OverwolfHook;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/** Isolated JVM: explicit selection must precede any default loader entry point. */
public final class NativeLoaderTest {
    public static void main(String[] args) throws Exception {
        Path selected = Path.of(args[0]).resolve("selected");
        Path other = Path.of(args[0]).resolve("other");
        for (Path directory : new Path[]{selected, other}) {
            Files.createDirectories(directory);
            for (String name : new String[]{"owh_client.dll", "owh_agent.dll"}) {
                try (var input = NativeLoaderTest.class.getResourceAsStream(
                        "/overwolf/native/windows-x64/" + name)) {
                    if (input == null) throw new AssertionError("Missing " + name);
                    Files.copy(input, directory.resolve(name),
                            StandardCopyOption.REPLACE_EXISTING);
                }
            }
        }
        try (var first = OverwolfHook.open(selected);
             var same = OverwolfHook.open(selected);
             var defaults = OverwolfHook.open()) {
            OverwolfHook.buildInfo();
            GameStateController.open();
            OverwolfController.open();
            first.gameState();
            first.overwolf();
            try {
                OverwolfHook.open(other).close();
                throw new AssertionError("An explicit DLL switch was accepted");
            } catch (IllegalStateException expected) {
                // Even byte-identical DLLs at a second path cannot switch the loader.
            }
        }
        System.out.println("PASS explicit native selection and default reuse; no attach");
    }
}
