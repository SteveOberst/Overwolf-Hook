package dev.sxlver.overwolfhook.internal;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import java.time.Duration;

/** Shared event/submission wait contract: null means infinite, otherwise exact finite milliseconds. */
public final class NativeTimeout {
    private NativeTimeout() { }
    public static long millis(Duration timeout) {
        if (timeout == null) return Integer.toUnsignedLong(OverwolfNative.OWH_WAIT_INFINITE());
        final long value;
        try { value = timeout.toMillis(); }
        catch (ArithmeticException overflow) { throw new IllegalArgumentException("timeout outside uint32 range", overflow); }
        if (timeout.isNegative() || timeout.getNano() % 1_000_000 != 0 || value > 0xffff_fffeL)
            throw new IllegalArgumentException("timeout must be a whole finite uint32 millisecond value");
        return value;
    }
}
