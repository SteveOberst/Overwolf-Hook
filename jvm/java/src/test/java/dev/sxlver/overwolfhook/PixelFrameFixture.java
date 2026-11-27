package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.internal.NativeHandle;
import dev.sxlver.overwolfhook.bindings.owh_pixel_buffer;
import dev.sxlver.overwolfhook.model.OwhConstants;
import dev.sxlver.overwolfhook.model.PixelBuffer;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

/** Test-only factory for a borrowed frame backed by synthetic foreign memory. */
public final class PixelFrameFixture {
    private PixelFrameFixture() { }
    public static void copySubmittedImage(MemorySegment image, int stride, PixelFrame frame) {
        PixelBufferOverlay.copyImage(image, stride, frame);
    }
    public static PixelFrame create(NativeHandle handle, MemorySegment storage, long bytes,
                                    long width, long height, long stride,
                                    NativeHandle.ThrowingConsumer<MemorySegment> discard) {
        return createWithCommit(handle, storage, bytes, width, height, stride,
            (arena, frame, receipt) -> dev.sxlver.overwolfhook.bindings.OverwolfNative.owh_frame_commit(arena, frame, receipt), discard);
    }
    public static PixelFrame createFailingCommit(NativeHandle handle, MemorySegment storage, long bytes,
                                                 long width, long height, long stride,
                                                 NativeHandle.ThrowingConsumer<MemorySegment> discard) {
        return createWithCommit(handle, storage, bytes, width, height, stride,
            (arena, frame, receipt) -> { throw new IllegalStateException("before native entry"); }, discard);
    }
    private static PixelFrame createWithCommit(NativeHandle handle, MemorySegment storage, long bytes,
                                                long width, long height, long stride,
                                                PixelFrame.CommitAction commit,
                                                NativeHandle.ThrowingConsumer<MemorySegment> discard) {
        try (var arena = Arena.ofConfined()) {
            var segment = PixelBuffer.allocate(arena);
            owh_pixel_buffer.pixel_format(segment, (int) OwhConstants.OWH_PIXEL_BGRA8_UNORM);
            owh_pixel_buffer.pixel_bytes(segment, bytes);
            owh_pixel_buffer.width(segment, Math.toIntExact(width));
            owh_pixel_buffer.height(segment, Math.toIntExact(height));
            owh_pixel_buffer.stride(segment, Math.toIntExact(stride));
            return new PixelFrame(handle, storage, PixelBuffer.copyOf(segment), commit, discard);
        }
    }
}
