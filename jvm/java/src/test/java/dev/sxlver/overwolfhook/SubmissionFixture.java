package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.internal.NativeHandle;
import java.lang.foreign.MemorySegment;

/** A consumed receipt: validates overload routing without a host process or native receipt. */
public final class SubmissionFixture {
    private SubmissionFixture() { }
    public static Submission closed() throws Exception {
        var handle = new NativeHandle(MemorySegment.ofAddress(0x5000));
        handle.callAndClose(ignored -> { });
        return new Submission(handle);
    }
}
