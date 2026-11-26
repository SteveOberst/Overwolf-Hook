package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeHandle;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.internal.NativeTimeout;
import dev.sxlver.overwolfhook.model.SubmissionInfo;
import dev.sxlver.overwolfhook.model.WaitOptions;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;

public final class Submission implements AutoCloseable {
    private final NativeHandle handle;
    Submission(NativeHandle handle) { this.handle = handle; }
    public SubmissionInfo poll() throws NativeFailure {
        MemorySegment submission = handle.get();
        try (var arena = Arena.ofConfined()) {
            var out = SubmissionInfo.allocate(arena);
            NativeResults.check("poll submission", OverwolfNative.owh_submission_poll(arena, submission, out));
            return SubmissionInfo.copyOf(out);
        }
    }
    public SubmissionInfo await() throws NativeFailure { return await(null, null); }
    public SubmissionInfo await(Duration timeout) throws NativeFailure { return await(timeout, null); }
    /** Null timeout waits indefinitely; finite values must be exact milliseconds. */
    public SubmissionInfo await(Duration timeout, WaitCancellation cancellation) throws NativeFailure {
        long millis = NativeTimeout.millis(timeout);
        MemorySegment submission = handle.get();
        MemorySegment token = cancellation == null ? MemorySegment.NULL : cancellation.get();
        try (var arena = Arena.ofConfined()) {
            var options = new WaitOptions().setTimeoutMs(millis)
                .setCancellation(token).materialize(arena);
            var out = SubmissionInfo.allocate(arena);
            NativeResults.check("wait for submission", OverwolfNative.owh_submission_wait(arena, submission, options, out));
            return SubmissionInfo.copyOf(out);
        }
    }
    @Override public void close() {
        try (var arena = Arena.ofConfined()) {
            handle.close(h -> { OverwolfNative.owh_submission_close(h); return java.lang.foreign.MemorySegment.NULL; });
        }
        catch (Exception failure) { throw new IllegalStateException("Unable to close submission", failure); }
    }
}
