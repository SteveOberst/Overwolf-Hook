import dev.sxlver.overwolfhook.*;
import dev.sxlver.overwolfhook.bindings.*;
import java.io.IOException;
import java.lang.foreign.*;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.concurrent.Executors;
import static dev.sxlver.overwolfhook.bindings.OverwolfNative.*;
import static java.lang.foreign.ValueLayout.*;

/** Executable artifact test using our own channel and process, never a running Overwolf. */
public class JvmSmokeTest {
    static void require(boolean condition) { if (!condition) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        var hook = OverwolfHook.open(); // Exercises embedded extraction/loading, not a developer-supplied library path.
        require(hook == OverwolfHook.open());
        if (args[0].equals("capture") || args[0].equals("hold") || args[0].equals("record")) {
            try (var capture = args[0].equals("record") ? hook.recordEvents(Path.of(args[2]), Long.parseLong(args[1])) :
                hook.capture(Long.parseLong(args[1]))) {
                long started = System.nanoTime();
                int events = 0;
                while (events < 3) {
                    if (System.nanoTime() - started > 5_000_000_000L) throw new AssertionError("No owned-host events");
                    if (capture.poll() instanceof EventReader.Event event) {
                        require(event.sourceChannel() == 900001);
                        ++events;
                    } else Thread.sleep(2);
                }
                require(capture.diagnostics().contains("hook enabled; channel="));
                if (args[0].equals("hold")) {
                    ProcessHandle.current().children().forEach(child -> System.out.println("controller=" + child.pid()));
                    System.out.flush();
                    Thread.sleep(60_000);
                } else {
                    try (var worker = Executors.newSingleThreadExecutor()) {
                        worker.submit(() -> { capture.close(); return null; }).get();
                    }
                }
            }
            System.out.println("PASS JVM-owned capture without recording, original frames and cross-thread close");
            return;
        }
        if (args[0].equals("state")) {
            long channel = Long.parseUnsignedLong(args[1]);
            require(hook.channels().stream().anyMatch(row -> row.id() == channel && row.info()));
            var first = hook.readState(channel);
            var field = first.fields().stream().filter(item -> item.keyText().equals("unknown_key")).findFirst().orElseThrow();
            require(Arrays.equals(field.bytes(), new byte[]{-1, 0, -128, 66}));
            require(field.producerTicks() == 123456789L);
            require(field.reservedBytes() == 8);
            require(hook.readState(channel).fields().size() == first.fields().size());
            System.out.println("PASS generated discovery/state API, binary field ownership and unsigned metadata");
            return;
        }
        System.load(Path.of(args[1]).toAbsolutePath().toString()); // Only the test writer needs capture exports in this JVM.
        try (var arena = Arena.ofConfined()) {
            String channel = "jvm-owned-" + ProcessHandle.current().pid() + "-" + System.nanoTime();
            var output = arena.allocate(ADDRESS);
            require(owh_result.status(owh_writer_create(arena, OWH_ABI_VERSION(), arena.allocateFrom(channel), 2,
                (int)owh_probe_frame.sizeof(), output)) == OWH_OK());
            var writer = output.get(ADDRESS, 0);
            try (var reader = hook.events(channel)) {
                require(reader.poll() == EventReader.Status.NO_DATA);
                var frame = owh_probe_frame.allocate(arena);
                owh_probe_frame.source_channel(frame, 0xf000_0001);
                owh_probe_frame.captured_qpc(frame, 987654321L);
                owh_probe_frame.raw_size(frame, (int)owh_probe_frame.raw(frame).byteSize());
                owh_probe_frame.raw(frame).fill((byte)0xa5);
                for (int i = 0; i < 4; ++i)
                    require(owh_result.status(owh_writer_publish(arena, writer, frame, (int)frame.byteSize())) == OWH_OK());
                var event = (EventReader.Event)reader.poll();
                require(event.sequence() == 3 && event.lostBefore() == 2);
                require(event.sourceChannel() == 0xf000_0001L && event.capturedTicks() == 987654321L);
                require(event.bytes().length == 64767 && event.bytes()[64766] == (byte)0xa5);
                reader.poll();
                require(owh_result.status(owh_writer_close(arena, writer)) == OWH_OK());
                writer = MemorySegment.NULL;
                require(reader.poll() == EventReader.Status.CLOSED);
                try (var worker = Executors.newSingleThreadExecutor()) { worker.submit(reader::close).get(); }
                require(event.bytes()[0] == (byte)0xa5); // Remains owned after close and native arena disposal.
            } finally { if (!writer.equals(MemorySegment.NULL)) owh_writer_close(arena, writer); }
        }
        try { hook.events("nonexistent-owned-" + System.nanoTime()); throw new AssertionError(); }
        catch (NativeFailure missing) { require(missing.status() == OWH_NOT_FOUND()); }
        try { hook.capture(ProcessHandle.current().pid()); throw new AssertionError(); }
        catch (IOException expected) { require(!expected.getMessage().isBlank()); }
        System.out.println("PASS embedded JAR, generated event layouts, overflow, no-data/closed, cross-thread close, error preservation and negative capture target");
    }
}
