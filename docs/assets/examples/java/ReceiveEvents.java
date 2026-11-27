package examples.events;

import dev.sxlver.overwolfhook.EventStreamClosedException;
import dev.sxlver.overwolfhook.OverwolfHook;
import dev.sxlver.overwolfhook.model.Event;
import java.time.Duration;

public final class ReceiveEvents {
    // --8<-- [start:handler]
    private static void handleEvent(Event value) {
        var bytes = value.getRaw();
        System.out.printf("channel=%d rawBytes=%d lost=%s firstBytes=",
                          value.getSourceChannel(), bytes.size(),
                          Long.toUnsignedString(value.getLostBefore()));
        for (long i = 0; i < Math.min(bytes.size(), 16); i++)
            System.out.printf("%02x ", Byte.toUnsignedInt(bytes.get(i)));
        System.out.println(bytes.size() > 16 ? "..." : "");
    }
    // --8<-- [end:handler]

    public static void main(String[] args) throws Exception {
        try (var sdk = OverwolfHook.open()) {
            // Default restart attach may close and relaunch Overwolf.
            sdk.attach();
            try (var subscription = sdk.events().subscribe()) {
                System.out.println("Listening for 5 minutes. Enter the game "
                                   + "and generate events now.");
                long started = System.nanoTime();
                while (System.nanoTime() - started <
                       Duration.ofMinutes(5).toNanos()) {
                    var event = subscription.receive(Duration.ofSeconds(1));
                    if (event.isEmpty())
                        continue;
                    handleEvent(event.get());
                }
                System.out.println("Observation ended.");
            } catch (EventStreamClosedException closed) {
                System.out.println("Event stream closed.");
            } finally {
                sdk.detach();
            }
        }
    }
}
