package dev.sxlver.overwolfhook.internal;

import dev.sxlver.overwolfhook.NativeFailure;
import dev.sxlver.overwolfhook.bindings.owh_result;
import dev.sxlver.overwolfhook.model.Result;
import dev.sxlver.overwolfhook.model.OwhConstants;
import java.lang.foreign.MemorySegment;

/** Result handling built on the generated native Result value. */
public final class NativeResults {
    private NativeResults() { }
    public static void check(String operation, MemorySegment nativeResult) throws NativeFailure {
        if (status(nativeResult) != OwhConstants.OWH_OK)
            throw new NativeFailure(operation, Result.copyOf(nativeResult));
    }
    public static int status(MemorySegment nativeResult) { return owh_result.status(nativeResult); }
}
