import dev.sxlver.overwolfhook.OverwolfHook;
import dev.sxlver.overwolfhook.model.ClientOptions;
import dev.sxlver.overwolfhook.model.OwhConstants;
import java.nio.charset.StandardCharsets;

/** Installed-jar consumer; reads the repository's synthetic InfoDB fixture only. */
public final class ConsumerSmoke {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected the synthetic fixture channel");
        long channel = Long.parseUnsignedLong(args[0]);
        try (var sdk = OverwolfHook.open()) {
            var options = new ClientOptions().setProcessId(ProcessHandle.current().pid());
            var report = sdk.check(options);
            long hostIdentity = OwhConstants.OWH_CHECK_HOST_IDENTITY;
            if ((report.getChecked() & hostIdentity) == 0 || (report.getPassed() & hostIdentity) != 0)
                throw new AssertionError("Preflight did not report the expected wrong-process identity");
            boolean sourceFound = sdk.gameState().sources().stream().anyMatch(source ->
                source.getSourceChannel() == channel && (source.getKinds() & OwhConstants.OWH_CHANNEL_INFO) != 0);
            if (!sourceFound) throw new AssertionError("Synthetic source channel missing");
            var state = sdk.gameState().storage(channel).read(100);
            var field = state.fields().stream().filter(value -> java.util.Arrays.equals(
                value.getKey().copyBytes(), "unknown_key".getBytes(StandardCharsets.US_ASCII))).findFirst().orElseThrow();
            if (!java.util.Arrays.equals(field.getBytes().copyBytes(), new byte[]{-1, 0, -128, 66}) ||
                field.getProducerTicks() != 123456789L || field.getReservedBytes() != 8)
                throw new AssertionError("Synthetic state bytes or metadata mismatch");
        }
        System.out.println("PASS installed Java artifact, generated models, passive state, and preflight");
    }
}
