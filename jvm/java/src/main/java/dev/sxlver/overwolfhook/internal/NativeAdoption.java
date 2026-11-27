package dev.sxlver.overwolfhook.internal;

import java.lang.foreign.MemorySegment;

/** Best-effort cleanup when constructing a Java owner for a newly returned native handle fails. */
public final class NativeAdoption {
    private NativeAdoption() { }
    public static void cleanup(MemorySegment raw, NativeHandle.ThrowingConsumer<MemorySegment> release, Throwable original) {
        if (raw == null || raw.equals(MemorySegment.NULL)) return;
        try { release.accept(raw); }
        catch (Throwable cleanupFailure) { original.addSuppressed(cleanupFailure); }
    }
}
