import dev.sxlver.overwolfhook.EventReader;
import dev.sxlver.overwolfhook.OverwolfHook;

/** Explicit ten-second capture, no recording. Close disables interception and disposes our controller. */
public class ReceiveEvents {
    public static void main(String[] args) throws Exception {
        var hook = OverwolfHook.open();
        try (var capture = hook.capture()) {
            long started = System.nanoTime();
            while (System.nanoTime() - started < 10_000_000_000L) {
                switch (capture.poll()) {
                    case EventReader.Event event -> System.out.printf("channel=%d sequence=%d lost=%d bytes=%d%n",
                        event.sourceChannel(), event.sequence(), event.lostBefore(), event.bytes().length);
                    case EventReader.Status status -> {
                        if (status == EventReader.Status.CLOSED) return;
                        Thread.sleep(2); // NO_DATA and BUSY are polling outcomes, not proof of absent game events.
                    }
                }
            }
        }
    }
}
