package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeHandle;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.internal.NativeAdoption;
import dev.sxlver.overwolfhook.model.PixelBuffer;
import dev.sxlver.overwolfhook.model.PixelRegion;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import static java.lang.foreign.ValueLayout.ADDRESS;

/** Exclusive native pixel borrow; its arena becomes invalid on commit or discard. */
public final class PixelFrame implements AutoCloseable {
    private final NativeHandle handle;
    private final Arena borrowScope = Arena.ofShared();
    private volatile boolean borrowOpen = true;
    private final MemorySegment pixels;
    private final PixelBuffer layout;
    PixelFrame(NativeHandle handle, MemorySegment source, PixelBuffer layout) {
        this(handle, source, layout, OverwolfNative::owh_frame_commit,
            OverwolfNative::owh_frame_discard);
    }
    PixelFrame(NativeHandle handle, MemorySegment source, PixelBuffer layout,
               CommitAction commit, NativeHandle.ThrowingConsumer<MemorySegment> discard) {
        this.handle = handle; this.layout = layout;
        this.commit = commit;
        this.discard = discard;
        this.pixels = MemorySegment.ofAddress(source.address()).reinterpret(layout.getPixelBytes(), borrowScope, ignored -> {});
    }
    @FunctionalInterface interface CommitAction {
        MemorySegment invoke(Arena arena, MemorySegment frame, MemorySegment receiptOut);
    }
    private final CommitAction commit;
    private final NativeHandle.ThrowingConsumer<MemorySegment> discard;
    public MemorySegment pixels() { return pixels; }
    /** Generated native pixel-buffer metadata with its borrowed pointer cleared. */
    public PixelBuffer layout() { return layout; }
    public long byteCount() { return layout.getPixelBytes(); }
    public long width() { return layout.getWidth(); }
    public long height() { return layout.getHeight(); }
    public long stride() { return layout.getStride(); }
    /** Optional publication hint; pixels must still cover the complete image.
     * null resets full damage. Invalid bounds do not consume the frame. */
    public void setDamage(PixelRegion damage) throws NativeFailure {
        try (var arena = Arena.ofConfined()) {
            NativeResults.check("set pixel damage", OverwolfNative.owh_frame_set_damage(arena,
                handle.get(), damage == null ? MemorySegment.NULL : damage.materialize(arena)));
        }
    }
    public void commit() throws NativeFailure { consume(false); }
    public Submission commitTracked() throws NativeFailure { return consume(true); }
    private Submission consume(boolean tracked) throws NativeFailure {
        try (var arena = Arena.ofConfined()) {
            MemorySegment receiptOut = tracked ? arena.allocate(ADDRESS) : MemorySegment.NULL;
            closeBorrowScope();
            MemorySegment nativeResult = handle.callAndConsume(frame -> commit.invoke(arena, frame, receiptOut));
            NativeResults.check("commit pixel frame", nativeResult);
            if (!tracked) return null;
            MemorySegment receipt = receiptOut.get(ADDRESS, 0);
            if (receipt.equals(MemorySegment.NULL)) throw new IllegalStateException("Native commit returned no receipt");
            try { return new Submission(new NativeHandle(receipt)); }
            catch (RuntimeException | Error failure) {
                NativeAdoption.cleanup(receipt, OverwolfNative::owh_submission_close, failure);
                throw failure;
            }
        } catch (NativeFailure failure) { throw failure; }
        catch (RuntimeException | Error failure) { throw failure; }
        catch (Exception failure) { throw new IllegalStateException("Unable to commit pixel frame", failure); }
    }
    @Override public void close() {
        try {
            closeBorrowScope();
            handle.callAndClose(discard);
        }
        catch (RuntimeException | Error failure) { throw failure; }
        catch (Exception failure) { throw new IllegalStateException("Unable to discard pixel frame", failure); }
    }
    private synchronized void closeBorrowScope() {
        if (borrowOpen) {
            borrowScope.close();
            borrowOpen = false;
        }
    }
}
