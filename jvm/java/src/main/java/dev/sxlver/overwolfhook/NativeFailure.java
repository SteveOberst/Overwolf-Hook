package dev.sxlver.overwolfhook;

import java.io.IOException;
import java.lang.foreign.MemorySegment;
import dev.sxlver.overwolfhook.bindings.owh_result;
import static dev.sxlver.overwolfhook.bindings.OverwolfNative.OWH_OK;

/** Failed native operation. Status and the original unsigned Win32 error are retained. */
public final class NativeFailure extends IOException {
    private final int status;
    private final long nativeError;

    NativeFailure(String operation, int status, long nativeError) {
        super(operation + ": status=" + status + ", Win32=" + nativeError);
        this.status = status;
        this.nativeError = nativeError;
    }

    public int status() { return status; }
    public long nativeError() { return nativeError; }

    static void check(String operation, MemorySegment result) throws NativeFailure {
        int status = owh_result.status(result);
        if (status != OWH_OK())
            throw new NativeFailure(operation, status, Integer.toUnsignedLong(owh_result.native_error(result)));
    }
}
