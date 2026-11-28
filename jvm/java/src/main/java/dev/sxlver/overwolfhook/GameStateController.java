package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.model.SourceChannel;
import dev.sxlver.overwolfhook.model.OwhConstants;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.List;
import java.io.IOException;
import static java.lang.foreign.ValueLayout.ADDRESS;

/** Passive state and source-discovery operations; no agent attachment is required. */
public final class GameStateController {
    GameStateController() {}
    /** Creates a passive controller, loading the Client through
     * {@link OverwolfHook#load()} if needed. Does not attach to the Agent. */
    public static GameStateController open() throws IOException {
        OverwolfHook.load();
        return new GameStateController();
    }
    public List<SourceChannel> sources() throws NativeFailure {
        try (var arena = Arena.ofConfined()) {
            var out = arena.allocate(ADDRESS);
            NativeResults.check("list game-state sources", OverwolfNative.owh_game_state_sources(arena, out));
            MemorySegment channels = out.get(ADDRESS, 0);
            try {
                int count = OverwolfNative.owh_source_list_count(channels);
                if (count < 0) throw new IllegalStateException("Invalid source-channel count");
                var row = dev.sxlver.overwolfhook.bindings.owh_source_channel.allocate(arena);
                var values = new ArrayList<SourceChannel>(count);
                for (int i = 0; i < count; i++) {
                    NativeResults.check("read source channel", OverwolfNative.owh_source_list_at(arena, channels, i, row));
                    values.add(SourceChannel.copyOf(row));
                }
                return List.copyOf(values);
            } finally { OverwolfNative.owh_source_list_close(channels); }
        }
    }
    public GameStateStorage storage(long sourceChannel) {
        return storage(sourceChannel, 0);
    }
    public GameStateStorage storage(long sourceChannel, long producerProcessId) {
        checkSource(sourceChannel, producerProcessId);
        return new GameStateStorage(this, sourceChannel, producerProcessId);
    }
    public GameStateStorage storage(SourceChannel source) {
        return storage(source.getSourceChannel(), source.getProducerProcessId());
    }
    public StateSnapshot read(SourceChannel source) throws NativeFailure {
        return storage(source).read();
    }
    public StateSnapshot read(SourceChannel source, long timeoutMillis) throws NativeFailure {
        return storage(source).read(timeoutMillis);
    }
    public StateSnapshot read(long sourceChannel) throws NativeFailure { return read(sourceChannel, OwhConstants.OWH_STATE_DEFAULT_TIMEOUT_MS); }
    public StateSnapshot read(long sourceChannel, long timeoutMillis) throws NativeFailure {
        return read(sourceChannel, timeoutMillis, 0);
    }
    public StateSnapshot read(long sourceChannel, long timeoutMillis, long producerProcessId) throws NativeFailure {
        checkSource(sourceChannel, producerProcessId);
        if (timeoutMillis < 0 || timeoutMillis > 0xffff_ffffL)
            throw new IllegalArgumentException("timeout must be a uint32 value");
        try (var arena = Arena.ofConfined()) {
            var out = arena.allocate(ADDRESS);
            NativeResults.check("read game state", OverwolfNative.owh_game_state_read_from(arena,
                (int) sourceChannel, (int) producerProcessId, (int) timeoutMillis, out));
            MemorySegment snapshot = out.get(ADDRESS, 0);
            try {
                int count = OverwolfNative.owh_snapshot_count(snapshot);
                if (count < 0) throw new IllegalStateException("Invalid snapshot field count");
                var row = dev.sxlver.overwolfhook.bindings.owh_state_field.allocate(arena);
                var values = new ArrayList<dev.sxlver.overwolfhook.model.StateField>(count);
                for (int i = 0; i < count; i++) {
                    NativeResults.check("read snapshot field", OverwolfNative.owh_snapshot_at(arena, snapshot, i, row));
                    values.add(dev.sxlver.overwolfhook.model.StateField.copyOf(row));
                }
                return new StateSnapshot(values);
            } finally { OverwolfNative.owh_snapshot_close(snapshot); }
        }
    }
    private static void checkSource(long channel, long producer) {
        if (channel < 0 || channel > 0xffff_ffffL || producer < 0 || producer > 0xffff_ffffL)
            throw new IllegalArgumentException("source channel and producer PID must be uint32 values");
    }
}
