package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.*;
import java.lang.foreign.*;
import java.time.Duration;
import static dev.sxlver.overwolfhook.bindings.OverwolfNative.*;
import static java.lang.foreign.ValueLayout.*;

/** Independent cursor with kernel-signalled reception. Close cancels an outstanding receive before freeing it. */
public final class EventReader implements AutoCloseable {
    public sealed interface Result permits Event, Status { }
    /** No data and contention do not certify the absence of gameplay events. Closed means drained. */
    public enum Status implements Result { NO_DATA, BUSY, CLOSED, TIMEOUT, CANCELLED }
    /** Raw name/payload buffer, original channel and capture QPC. No game schema is assumed. */
    public record Event(long sequence, long lostBefore, long capturedTicks, long sourceChannel, byte[] bytes) implements Result { }
    private final Arena arena = Arena.ofShared();
    private final MemorySegment frame = owh_probe_frame.allocate(arena);
    private final MemorySegment record = owh_record.allocate(arena);
    private final MemorySegment result = owh_result.allocate(arena);
    private MemorySegment reader = MemorySegment.NULL;
    private final Object reading = new Object();
    private volatile boolean closed;

    EventReader(String channel, boolean newOnly) throws NativeFailure {
        try {
            var output = arena.allocate(ADDRESS);
            NativeFailure.check("open events", owh_reader_open(arena, OWH_ABI_VERSION(), arena.allocateFrom(channel),
                newOnly ? OWH_NEW_ONLY() : OWH_RETAINED(), output));
            reader = output.get(ADDRESS, 0);
        } catch (Throwable failure) { arena.close(); throw failure; }
    }

    /** Non-blocking data check, for callers that explicitly own their scheduling. */
    public Result poll() throws NativeFailure { return read(false, 0); }
    /** Block until a record, drained producer, or cancellation. No timed polling. */
    public Result receive() throws NativeFailure { return read(true, OWH_WAIT_INFINITE()); }
    /** Block up to the given whole-millisecond timeout; TIMEOUT does not advance the cursor. */
    public Result receive(Duration timeout) throws NativeFailure {
        long millis = timeout.toMillis();
        if (millis < 0 || millis >= 0xffff_ffffL || !Duration.ofMillis(millis).equals(timeout))
            throw new IllegalArgumentException("Timeout must be whole milliseconds from 0 to 4294967294");
        return read(true, (int)millis);
    }

    private Result read(boolean blocking, int timeout) throws NativeFailure {
        synchronized (reading) {
            if (closed) throw new IllegalStateException("Event reader is closed");
            SegmentAllocator allocator = (size, alignment) -> result;
            var status = blocking ? owh_reader_receive(allocator, reader, frame, (int)frame.byteSize(), timeout, record) :
                owh_reader_next(allocator, reader, frame, (int)frame.byteSize(), record);
            int code = owh_result.status(status);
            if (code == OWH_NO_DATA()) return Status.NO_DATA;
            if (code == OWH_BUSY()) return Status.BUSY;
            if (code == OWH_CLOSED()) return Status.CLOSED;
            if (code == OWH_TIMEOUT()) return Status.TIMEOUT;
            if (code == OWH_CANCELLED()) return Status.CANCELLED;
            NativeFailure.check("read event", status);
            if (owh_record.size(record) != frame.byteSize() || owh_record.reserved(record) != 0 ||
                owh_probe_frame.raw_size(frame) != owh_probe_frame.raw(frame).byteSize())
                throw new IllegalStateException("Unrecognized native event layout");
            return new Event(owh_record.sequence(record), owh_record.lost_before(record),
                owh_probe_frame.captured_qpc(frame), Integer.toUnsignedLong(owh_probe_frame.source_channel(frame)),
                owh_probe_frame.raw(frame).toArray(JAVA_BYTE));
        }
    }

    /** Terminal receive cancellation, callable from another thread. Does not close the writer. */
    public synchronized void cancel() throws NativeFailure {
        if (!closed) {
            try (var callArena = Arena.ofConfined()) {
                NativeFailure.check("cancel events", owh_reader_cancel(callArena, reader));
            }
        }
    }

    @Override public synchronized void close() throws NativeFailure {
        if (closed) return;
        cancel();
        closed = true;
        synchronized (reading) {
            try { owh_reader_close(reader); } finally { arena.close(); }
        }
    }
}
