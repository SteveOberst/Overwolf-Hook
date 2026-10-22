package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.*;
import java.io.IOException;
import java.lang.foreign.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.time.Duration;
import java.util.*;
import static dev.sxlver.overwolfhook.bindings.OverwolfNative.*;
import static java.lang.foreign.ValueLayout.*;

/** Explicit native access. No game semantics, background polling or hook is started by open().
 * A class loader uses one native build; loaded libraries remain resident for its lifetime.
 */
public final class OverwolfHook {
    private static OverwolfHook loaded;
    private final Path binaries;
    private OverwolfHook(Path binaries) { this.binaries = binaries; }

    /** Extract and load the binaries shipped in this JAR. Requires Windows x64 and native access. */
    public static synchronized OverwolfHook open() throws IOException {
        return loaded == null ? open(NativeBinaries.directory()) : loaded;
    }

    /** Use an explicitly supplied matching bin directory instead of the bundled build. */
    public static synchronized OverwolfHook open(Path directory) throws IOException {
        Path canonical = directory.toRealPath();
        if (loaded != null) {
            if (!loaded.binaries.equals(canonical)) throw new IllegalStateException("A different native build is already loaded by this class loader");
            return loaded;
        }
        System.load(canonical.resolve("owh_client.dll").toString());
        loaded = new OverwolfHook(canonical);
        return loaded;
    }

    /** Non-atomic mapping presence, not game ID, freshness, compatibility or permission to read. */
    public record Channel(long id, boolean info, boolean events) { }

    /** All original bytes are owned Java copies. Producer ticks are unsigned bits, not UTC/game time. */
    public record Field(byte[] category, byte[] key, long producerTicks, long reservedBytes, byte[] bytes) {
        /** UTF-8 convenience; original name bytes remain available if decoding is lossy. */
        public String categoryText() { return new String(category, StandardCharsets.UTF_8); }
        public String keyText() { return new String(key, StandardCharsets.UTF_8); }
    }

    /** A complete copied read. Empty fields are distinct from failure and do not certify a live match. */
    public record State(long sourceChannel, List<Field> fields) {
        public State { fields = List.copyOf(fields); }
    }

    public List<Channel> channels() throws NativeFailure {
        try (var arena = Arena.ofConfined()) {
            var output = arena.allocate(ADDRESS);
            NativeFailure.check("discover channels", owh_channels_discover(arena, OWH_ABI_VERSION(), output));
            var channels = output.get(ADDRESS, 0);
            try {
                int count = owh_channels_count(channels);
                if (count < 0) throw new IllegalStateException("Invalid channel count");
                var row = owh_source_channel.allocate(arena);
                var result = new ArrayList<Channel>(count);
                for (int i = 0; i < count; ++i) {
                    NativeFailure.check("read channel", owh_channels_at(arena, channels, i, row));
                    int kinds = owh_source_channel.kinds(row);
                    result.add(new Channel(Integer.toUnsignedLong(owh_source_channel.source_channel(row)),
                        (kinds & OWH_CHANNEL_INFO()) != 0, (kinds & OWH_CHANNEL_EVENTS()) != 0));
                }
                return List.copyOf(result);
            } finally { owh_channels_close(channels); }
        }
    }

    public State readState(long sourceChannel) throws NativeFailure { return readState(sourceChannel, Duration.ofMillis(100)); }

    /** Explicit bounded I/O. Failure never becomes a successful empty state. Provider handles close before return. */
    public State readState(long sourceChannel, Duration lockTimeout) throws NativeFailure {
        if (sourceChannel <= 0 || sourceChannel > 0xffff_ffffL) throw new IllegalArgumentException("Source channel must be uint32 and nonzero");
        long millis = lockTimeout.toMillis();
        if (millis < 0 || millis > 1000 || !Duration.ofMillis(millis).equals(lockTimeout))
            throw new IllegalArgumentException("Lock timeout must be whole milliseconds from 0 to 1000");
        try (var arena = Arena.ofConfined()) {
            var output = arena.allocate(ADDRESS);
            NativeFailure.check("read state", owh_state_read(arena, OWH_ABI_VERSION(), (int)sourceChannel, (int)millis, output));
            var state = output.get(ADDRESS, 0);
            try {
                int count = owh_state_count(state);
                if (count < 0) throw new IllegalStateException("Invalid field count");
                var field = owh_state_field.allocate(arena);
                var fields = new ArrayList<Field>(count);
                for (int i = 0; i < count; ++i) {
                    NativeFailure.check("read field", owh_state_at(arena, state, i, field));
                    long size = Integer.toUnsignedLong(owh_state_field.size(field));
                    fields.add(new Field(name(owh_state_field.category(field)), name(owh_state_field.key(field)),
                        owh_state_field.producer_ticks(field), Integer.toUnsignedLong(owh_state_field.reserved_bytes(field)),
                        size == 0 ? new byte[0] : owh_state_field.bytes(field).reinterpret(size).toArray(JAVA_BYTE)));
                }
                return new State(sourceChannel, fields);
            } finally { owh_state_close(state); }
        }
    }

    /** Attach an independent cursor to an existing bridge channel. This does not start a capture. */
    public EventReader events(String bridgeChannel) throws NativeFailure { return new EventReader(bridgeChannel, false); }
    public EventReader events(String bridgeChannel, boolean newOnly) throws NativeFailure { return new EventReader(bridgeChannel, newOnly); }

    /** Start a capture in the single running Overwolf host. No recording file is created. */
    public Capture capture() throws IOException { return Capture.start(binaries, Capture.runningHost(), null); }
    /** Explicit process discovery. Empty means no host; multiple hosts require caller selection. */
    public OptionalLong findHost() throws IOException { return Capture.findHost(); }
    /** Explicit host selection. The native controller rejects processes other than compatible Overwolf hosts. */
    public Capture capture(long processId) throws IOException { return Capture.start(binaries, processId, null); }
    /** Start the same capture with a full raw recording in a new caller-owned file. */
    public Capture recordEvents(Path recording) throws IOException { return Capture.start(binaries, Capture.runningHost(), recording); }
    /** Record from an explicitly selected host into a new caller-owned file. */
    public Capture recordEvents(Path recording, long processId) throws IOException { return Capture.start(binaries, processId, recording); }

    private static byte[] name(MemorySegment pointer) {
        var bounded = pointer.reinterpret(257);
        for (int length = 0; length <= 256; ++length)
            if (bounded.get(JAVA_BYTE, length) == 0) return bounded.asSlice(0, length).toArray(JAVA_BYTE);
        throw new IllegalStateException("Unterminated native field name");
    }
}
