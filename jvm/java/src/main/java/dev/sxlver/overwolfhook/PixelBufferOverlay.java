package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeAdoption;
import dev.sxlver.overwolfhook.internal.NativeHandle;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.internal.NativeTimeout;
import dev.sxlver.overwolfhook.model.OwhConstants;
import dev.sxlver.overwolfhook.model.OverlayInfo;
import dev.sxlver.overwolfhook.model.PixelBuffer;
import dev.sxlver.overwolfhook.model.PixelRegion;
import dev.sxlver.overwolfhook.model.WaitOptions;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.Objects;
import java.util.Optional;
import static java.lang.foreign.ValueLayout.ADDRESS;

/** Pixel-buffer submission with SDK-managed shared slots, independent of the renderer. */
public final class PixelBufferOverlay implements AutoCloseable {
    private final NativeHandle handle;
    PixelBufferOverlay(NativeHandle handle) { this.handle = handle; }
    public OverlayInfo info() throws NativeFailure {
        MemorySegment overlay = handle.get();
        try (var arena = Arena.ofConfined()) {
            var out = OverlayInfo.allocate(arena);
            NativeResults.check("query overlay", OverwolfNative.owh_overlay_query(arena, overlay, out));
            return OverlayInfo.copyOf(out);
        }
    }
    /** Copy a complete BGRA8 image; the default keeps the latest unread image.
     * Reuse the input on return. Serialize submissions and direct frame borrows. */
    public void submit(byte[] image) throws NativeFailure { submit(MemorySegment.ofArray(image)); }
    public void submit(byte[] image, PixelRegion damage) throws NativeFailure {
        submit(MemorySegment.ofArray(image), damage);
    }
    public void submit(ByteBuffer image) throws NativeFailure { submit(MemorySegment.ofBuffer(image)); }
    public void submit(ByteBuffer image, PixelRegion damage) throws NativeFailure {
        submit(MemorySegment.ofBuffer(image), damage);
    }
    public void submit(MemorySegment image) throws NativeFailure { submit(image, 0); }
    /** Optional changed area, not a packed patch. null publishes the whole image.
     * Includes pixels cleared since the last successful submit. The SDK merges
     * damage across replaced frames; input is still copied completely. */
    public void submit(MemorySegment image, PixelRegion damage) throws NativeFailure {
        submit(image, 0, null, null, damage);
    }
    public void submit(MemorySegment image, int sourceStride) throws NativeFailure {
        submit(image, sourceStride, null, null);
    }
    /** FIFO applies backpressure; null timeout waits indefinitely. Cancellation
     * and timeout act before admission, not after the copy has started. */
    public void submit(MemorySegment image, int sourceStride, Duration timeout,
                       WaitCancellation cancellation) throws NativeFailure {
        submit(image, sourceStride, timeout, cancellation, null);
    }
    public void submit(MemorySegment image, int sourceStride, Duration timeout,
                       WaitCancellation cancellation, PixelRegion damage) throws NativeFailure {
        try (var frame = prepare(image, sourceStride, timeout, cancellation, damage)) {
            copyImage(image, sourceStride, frame);
            frame.commit();
        }
    }
    public Submission submitTracked(MemorySegment image) throws NativeFailure {
        return submitTracked(image, 0, null, null);
    }
    public Submission submitTracked(MemorySegment image, PixelRegion damage) throws NativeFailure {
        return submitTracked(image, 0, null, null, damage);
    }
    public Submission submitTracked(MemorySegment image, int sourceStride, Duration timeout,
                                     WaitCancellation cancellation) throws NativeFailure {
        return submitTracked(image, sourceStride, timeout, cancellation, null);
    }
    public Submission submitTracked(MemorySegment image, int sourceStride, Duration timeout,
                                     WaitCancellation cancellation, PixelRegion damage) throws NativeFailure {
        try (var frame = prepare(image, sourceStride, timeout, cancellation, damage)) {
            copyImage(image, sourceStride, frame);
            return frame.commitTracked();
        }
    }
    /** Explicit immediate/free-slot admission. false leaves the image unsubmitted. */
    public boolean trySubmit(MemorySegment image) throws NativeFailure { return trySubmit(image, 0); }
    public boolean trySubmit(MemorySegment image, PixelRegion damage) throws NativeFailure {
        return trySubmit(image, 0, damage);
    }
    public boolean trySubmit(MemorySegment image, int sourceStride) throws NativeFailure {
        return trySubmit(image, sourceStride, null);
    }
    public boolean trySubmit(MemorySegment image, int sourceStride, PixelRegion damage) throws NativeFailure {
        Objects.requireNonNull(image, "image");
        if (sourceStride < 0) throw new IllegalArgumentException("negative stride");
        var acquired = tryAcquire();
        if (acquired.isEmpty()) return false;
        try (var frame = acquired.get()) {
            if (damage != null) frame.setDamage(damage);
            copyImage(image, sourceStride, frame);
            try { frame.commit(); }
            catch (NativeFailure failure) {
                if (failure.statusCode() == OwhConstants.OWH_BUSY) return false;
                throw failure;
            }
            return true;
        }
    }

    private PixelFrame prepare(MemorySegment image, int sourceStride, Duration timeout,
                               WaitCancellation cancellation, PixelRegion damage) throws NativeFailure {
        Objects.requireNonNull(image, "image");
        if (sourceStride < 0) throw new IllegalArgumentException("negative stride");
        try (var arena = Arena.ofConfined()) {
            var wait = new WaitOptions().setTimeoutMs(NativeTimeout.millis(timeout));
            if (cancellation != null) wait.setCancellation(cancellation.get());
            var frameOut = arena.allocate(ADDRESS);
            var buffer = PixelBuffer.allocate(arena);
            NativeResults.check("submit pixel image", OverwolfNative.owh_frame_prepare_submit(arena,
                handle.get(), image.byteSize(), sourceStride,
                damage == null ? MemorySegment.NULL : damage.materialize(arena),
                wait.materialize(arena), frameOut, buffer));
            return adoptFrame(frameOut.get(ADDRESS, 0), buffer);
        }
    }

    // The native API owns admission and synchronization. This row copy is the
    // FFM bridge: heap arrays cannot be passed as native downcall pointers.
    static void copyImage(MemorySegment image, int sourceStride, PixelFrame frame) {
        long rowBytes = frame.width() * 4;
        long stride = sourceStride == 0 ? rowBytes : sourceStride;
        if (stride < rowBytes || image.byteSize() < (frame.height() - 1) * stride + rowBytes)
            throw new IllegalArgumentException("image does not cover the overlay geometry");
        if (stride == frame.stride())
            MemorySegment.copy(image, 0, frame.pixels(), 0, frame.height() * rowBytes);
        else
            for (long y = 0; y < frame.height(); ++y)
                MemorySegment.copy(image, y * stride, frame.pixels(), y * frame.stride(), rowBytes);
    }
    /** Expert direct-write access. Prefer submit when your renderer owns its buffer. */
    public Optional<PixelFrame> tryAcquire() throws NativeFailure {
        try (var arena = Arena.ofConfined()) {
            var frameOut = arena.allocate(ADDRESS);
            var buffer = PixelBuffer.allocate(arena);
            var result = OverwolfNative.owh_frame_acquire(arena, handle.get(), frameOut, buffer);
            int status = NativeResults.status(result);
            if (status == OwhConstants.OWH_BUSY || status == OwhConstants.OWH_NO_DATA) return Optional.empty();
            NativeResults.check("acquire pixel frame", result);
            return Optional.of(adoptFrame(frameOut.get(ADDRESS, 0), buffer));
        }
    }
    private static PixelFrame adoptFrame(MemorySegment frame, MemorySegment buffer) {
        try {
            MemorySegment pixels = dev.sxlver.overwolfhook.bindings.owh_pixel_buffer.pixels(buffer);
            long bytes = dev.sxlver.overwolfhook.bindings.owh_pixel_buffer.pixel_bytes(buffer);
            if (frame.equals(MemorySegment.NULL) || pixels.equals(MemorySegment.NULL) || bytes < 0)
                throw new IllegalStateException("Native frame returned an invalid pixel view");
            dev.sxlver.overwolfhook.bindings.owh_pixel_buffer.pixels(buffer, MemorySegment.NULL);
            return new PixelFrame(new NativeHandle(frame), pixels, PixelBuffer.copyOf(buffer));
        } catch (RuntimeException | Error failure) {
            if (!frame.equals(MemorySegment.NULL))
                NativeAdoption.cleanup(frame, OverwolfNative::owh_frame_discard, failure);
            throw failure;
        }
    }
    @Override public void close() throws NativeFailure {
        try (var arena = Arena.ofConfined()) { var result = handle.close(h -> OverwolfNative.owh_overlay_close(arena, h)); if (result != null) NativeResults.check("close overlay", result); }
        catch (NativeFailure failure) { throw failure; }
        catch (Exception failure) { throw new IllegalStateException("Unable to close overlay", failure); }
    }
}
