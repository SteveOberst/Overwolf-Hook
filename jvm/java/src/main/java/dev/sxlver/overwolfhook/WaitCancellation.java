package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeHandle;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.internal.NativeAdoption;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import static java.lang.foreign.ValueLayout.ADDRESS;

/** Shared wait token. Signal and join every borrowing wait before close. */
public final class WaitCancellation implements AutoCloseable {
    private final NativeHandle handle;
    private WaitCancellation(NativeHandle handle) { this.handle = handle; }
    public static WaitCancellation create() throws NativeFailure {
        try (var arena = Arena.ofConfined()) {
            var out = arena.allocate(ADDRESS);
            NativeResults.check("create wait cancellation", OverwolfNative.owh_cancel_create(arena, out));
            MemorySegment token = out.get(ADDRESS, 0);
            if (token.equals(MemorySegment.NULL)) throw new IllegalStateException("Native cancel-create returned no handle");
            try { return new WaitCancellation(new NativeHandle(token)); }
            catch (RuntimeException | Error failure) {
                NativeAdoption.cleanup(token, OverwolfNative::owh_cancel_close, failure);
                throw failure;
            }
        }
    }
    MemorySegment get() { return handle.get(); }
    public void cancel() { OverwolfNative.owh_cancel_signal(handle.get()); }
    @Override public void close() {
        try { handle.close(h -> {
            OverwolfNative.owh_cancel_close(h);
            return java.lang.foreign.MemorySegment.NULL;
        }); }
        catch (Exception failure) { throw new IllegalStateException("Unable to close wait cancellation", failure); }
    }
}
