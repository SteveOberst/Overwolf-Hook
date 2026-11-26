import dev.sxlver.overwolfhook.OverwolfHook;
import dev.sxlver.overwolfhook.GameStateController;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Runs against the packaged Java artifact without attaching or injecting into a host. */
public final class JvmSmokeTest {
    public static void main(String[] args) throws Exception {
        GameStateController.open().sources();
        try (var first = OverwolfHook.open(); var second = OverwolfHook.open()) {
            if (first == second) throw new AssertionError("open() reused a mutable SDK context");
            // Passive discovery is valid before attach and may return an empty list.
            first.gameState().sources();
            second.gameState().sources();
            if (args.length > 0 && args[0].equals("state")) verifyState(first, Long.parseUnsignedLong(args[1]));
        }
        System.out.println("PASS packaged Java SDK contexts, native loader, and passive state ABI");
    }

    private static void verifyState(OverwolfHook sdk, long channel) throws Exception {
        var snapshot = sdk.gameState().read(channel);
        var field = snapshot.fields().stream().filter(item -> Arrays.equals(
            item.getKey().copyBytes(), "unknown_key".getBytes(StandardCharsets.US_ASCII))).findFirst().orElseThrow();
        if (!Arrays.equals(field.getBytes().copyBytes(), new byte[]{-1, 0, -128, 66}) ||
            field.getProducerTicks() != 123456789L || field.getReservedBytes() != 8)
            throw new AssertionError("State data mismatch");
    }
}
