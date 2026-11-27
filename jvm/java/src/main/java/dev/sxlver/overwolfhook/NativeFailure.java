package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.model.Result;
import java.io.IOException;

/** Native failure retaining the generated result value, including unknown codes. */
public final class NativeFailure extends IOException {
    private final String operation;
    private final Result result;
    public NativeFailure(String operation, Result result) {
        super(operation + ": status=" + Integer.toUnsignedString(result.getStatus()) +
            ", stage=" + Long.toUnsignedString(result.getFailureStage()) +
            ", domain=" + Long.toUnsignedString(result.getErrorDomain()) +
            ", nativeCode=0x" + Long.toUnsignedString(result.getNativeCode(), 16));
        this.operation = operation;
        this.result = result;
    }
    public String operation() { return operation; }
    public Result result() { return result; }
    public int statusCode() { return result.getStatus(); }
}
