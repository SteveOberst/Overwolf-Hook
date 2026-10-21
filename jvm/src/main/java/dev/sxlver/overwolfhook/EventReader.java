package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.*;
import java.lang.foreign.*;
import static dev.sxlver.overwolfhook.bindings.OverwolfNative.*;
import static java.lang.foreign.ValueLayout.*;

/** One independent cursor. Poll and close are serialized; each event owns its complete payload. */
public final class EventReader implements AutoCloseable {
    public sealed interface Result permits Event, Status { }
    /** No data and contention do not certify the absence of gameplay events. Closed means drained. */
    public enum Status implements Result { NO_DATA, BUSY, CLOSED }
    /** Raw name/payload buffer, original channel and capture QPC. No game schema is assumed. */
    public record Event(long sequence, long lostBefore, long capturedTicks, long sourceChannel, byte[] bytes) implements Result { }
    private final Arena arena = Arena.ofShared();
    private final MemorySegment frame = owh_probe_frame.allocate(arena);
    private final MemorySegment record = owh_record.allocate(arena);
    private final MemorySegment result = owh_result.allocate(arena);
    private MemorySegment reader = MemorySegment.NULL;
    private boolean closed;

    EventReader(String channel, boolean newOnly) throws NativeFailure {
        try {
            var output = arena.allocate(ADDRESS);
            NativeFailure.check("open events", owh_reader_open(arena, OWH_ABI_VERSION(), arena.allocateFrom(channel),
                newOnly ? OWH_NEW_ONLY() : OWH_RETAINED(), output));
            reader = output.get(ADDRESS, 0);
        } catch (Throwable failure) { arena.close(); throw failure; }
    }

    public synchronized Result poll() throws NativeFailure {
        if (closed) throw new IllegalStateException("Event reader is closed");
        SegmentAllocator allocator = (size, alignment) -> result;
        var status = owh_reader_next(allocator, reader, frame, (int)frame.byteSize(), record);
        int code = owh_result.status(status);
        if (code == OWH_NO_DATA()) return Status.NO_DATA;
        if (code == OWH_BUSY()) return Status.BUSY;
        if (code == OWH_CLOSED()) return Status.CLOSED;
        NativeFailure.check("read event", status);
        if (owh_record.size(record) != frame.byteSize() || owh_record.reserved(record) != 0 ||
            owh_probe_frame.raw_size(frame) != owh_probe_frame.raw(frame).byteSize())
            throw new IllegalStateException("Unrecognized native event layout");
        return new Event(owh_record.sequence(record), owh_record.lost_before(record),
            owh_probe_frame.captured_qpc(frame), Integer.toUnsignedLong(owh_probe_frame.source_channel(frame)),
            owh_probe_frame.raw(frame).toArray(JAVA_BYTE));
    }

    @Override public synchronized void close() {
        if (closed) return;
        closed = true;
        try { owh_reader_close(reader); } finally { arena.close(); }
    }
}
