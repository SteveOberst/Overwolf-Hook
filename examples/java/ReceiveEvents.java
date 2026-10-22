import dev.sxlver.overwolfhook.EventReader;
import dev.sxlver.overwolfhook.OverwolfHook;
import java.time.Duration;

/** Explicit ten-second capture, no recording. Close disables interception and disposes our controller. */
public class ReceiveEvents {
    public static void main(String[] args) throws Exception {
        var hook = OverwolfHook.open();
        try (var capture = hook.capture()) {
            long started = System.nanoTime();
            while (System.nanoTime() - started < 10_000_000_000L) {
                long remaining = 10_000_000_000L - (System.nanoTime() - started);
                if (remaining <= 0) break;
                switch (capture.receive(Duration.ofMillis((remaining + 999_999L) / 1_000_000L))) {
                    case EventReader.Event event -> System.out.printf("channel=%d sequence=%d lost=%d bytes=%d%n",
                        event.sourceChannel(), event.sequence(), event.lostBefore(), event.bytes().length);
                    case EventReader.Status status -> {
                        if (status == EventReader.Status.CLOSED || status == EventReader.Status.CANCELLED ||
                            status == EventReader.Status.TIMEOUT) return;
                        throw new IllegalStateException("Unexpected blocking receive status: " + status);
                    }
                }
            }
        }
    }
}
