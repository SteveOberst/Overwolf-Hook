package dev.sxlver.overwolfhook.internal;

import java.lang.foreign.MemorySegment;

/** A local owning pointer slot. Callers serialize use and close themselves. */
public final class NativeHandle {
    private MemorySegment value;

    public NativeHandle(MemorySegment value) {
        if (value == null || value.equals(MemorySegment.NULL)) throw new IllegalArgumentException("null native handle");
        this.value = value;
    }

    public MemorySegment get() { return requireOpen(); }
    public boolean closed() { return value == null; }

    /** Idempotent close. Ownership is consumed only after the downcall returns. */
    public MemorySegment close(ThrowingFunction<MemorySegment, MemorySegment> action) throws Exception {
        return invokeAndConsume(action, false);
    }

    /** Preserve ownership if FFM rejects the call before native entry. */
    public <T> T callAndConsume(ThrowingFunction<MemorySegment, T> action) throws Exception {
        return invokeAndConsume(action, true);
    }

    public void callAndClose(ThrowingConsumer<MemorySegment> action) throws Exception {
        invokeAndConsume(handle -> { action.accept(handle); return null; }, false);
    }

    private <T> T invokeAndConsume(ThrowingFunction<MemorySegment, T> action, boolean requireOpen)
            throws Exception {
        MemorySegment handle = value;
        if (handle == null) {
            if (requireOpen) throw new IllegalStateException("Native resource is closed");
            return null;
        }
        T result = action.apply(handle);
        value = null;
        return result;
    }

    private MemorySegment requireOpen() {
        if (value == null) throw new IllegalStateException("Native resource is closed");
        return value;
    }

    @FunctionalInterface public interface ThrowingConsumer<T> { void accept(T value) throws Exception; }
    @FunctionalInterface public interface ThrowingFunction<T, R> { R apply(T value) throws Exception; }

}
