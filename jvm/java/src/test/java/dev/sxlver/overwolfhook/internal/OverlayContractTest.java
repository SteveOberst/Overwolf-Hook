package dev.sxlver.overwolfhook.internal;

import dev.sxlver.overwolfhook.PixelFrame;
import dev.sxlver.overwolfhook.PixelFrameFixture;
import dev.sxlver.overwolfhook.SubmissionFixture;
import java.time.Duration;
import dev.sxlver.overwolfhook.NativeFailure;
import dev.sxlver.overwolfhook.bindings.owh_result;
import dev.sxlver.overwolfhook.model.OwhConstants;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

/** Packaged-artifact handle, cancellation, and borrowed-frame lifetime checks; no host attach. */
public final class OverlayContractTest {
    public static void main(String[] args) throws Exception {
        try (var sdk = dev.sxlver.overwolfhook.OverwolfHook.open()) {
            generatedPixelRegionMatchesNativeFields();
        }
        heapImageSubmissionCopiesOnlyPixelRows();
        infiniteSubmissionWaitReachesTheReceipt();
        nativeWaitTimeoutContract();
        consumingCallReturnsItsResult();
        ordinaryCloseConsumesOnlyAfterRawCallReturns();
        frameArenaIsClosedBeforeNativeReleaseAndConsumeIsOneShot();
        failedPreEntryCommitRetainsFrameForDiscard();
        sequentialFrameCloseDiscardsOnce();
        System.out.println("PASS packaged JVM handle, cancellation, and frame ownership contracts");
    }

    private static void generatedPixelRegionMatchesNativeFields() {
        try (var arena = Arena.ofConfined()) {
            var region = new dev.sxlver.overwolfhook.model.PixelRegion()
                .setX(16).setY(32).setWidth(224).setHeight(64);
            var nativeRegion = region.materialize(arena);
            require(dev.sxlver.overwolfhook.bindings.owh_pixel_region.x(nativeRegion) == 16,
                "damage x changed at native boundary");
            require(dev.sxlver.overwolfhook.bindings.owh_pixel_region.y(nativeRegion) == 32,
                "damage y changed at native boundary");
            require(dev.sxlver.overwolfhook.bindings.owh_pixel_region.width(nativeRegion) == 224,
                "damage width changed at native boundary");
            require(dev.sxlver.overwolfhook.bindings.owh_pixel_region.height(nativeRegion) == 64,
                "damage height changed at native boundary");
        }
    }

    private static void heapImageSubmissionCopiesOnlyPixelRows() {
        byte[] source = {1,2,3,4,5,6,7,8,99,99,99,99,9,10,11,12,13,14,15,16};
        try (var arena = Arena.ofConfined()) {
            var destination = arena.allocate(16);
            try (var frame = PixelFrameFixture.create(new NativeHandle(MemorySegment.ofAddress(1)),
                    destination, 16, 2, 2, 8, ignored -> {})) {
                PixelFrameFixture.copySubmittedImage(MemorySegment.ofArray(source), 12, frame);
                for (int i = 0; i < 16; ++i)
                    require(destination.get(ValueLayout.JAVA_BYTE, i) == (byte)(i + 1), "source row padding copied as pixels");
                try {
                    PixelFrameFixture.copySubmittedImage(MemorySegment.ofArray(new byte[3]), 0, frame);
                    throw new AssertionError("short input accepted");
                } catch (IllegalArgumentException expected) { }
            }
        }
    }

    private static void infiniteSubmissionWaitReachesTheReceipt() throws Exception {
        var closed = SubmissionFixture.closed();
        try { closed.await(); throw new AssertionError("expected closed receipt"); }
        catch (IllegalStateException expected) {
            require("Native resource is closed".equals(expected.getMessage()), "infinite default did not reach receipt");
        }
        try { closed.await((Duration) null); throw new AssertionError("expected closed receipt"); }
        catch (IllegalStateException expected) {
            require("Native resource is closed".equals(expected.getMessage()), "explicit infinite did not reach receipt");
        }
    }

    private static void nativeWaitTimeoutContract() {
        require(NativeTimeout.millis(null) == 0xffff_ffffL, "null must select infinite wait");
        require(NativeTimeout.millis(Duration.ZERO) == 0, "zero must remain nonblocking");
        require(NativeTimeout.millis(Duration.ofMillis(0xffff_fffeL)) == 0xffff_fffeL, "maximum finite timeout changed");
        for (Duration invalid : new Duration[] { Duration.ofNanos(1), Duration.ofMillis(-1),
                Duration.ofMillis(0xffff_ffffL), Duration.ofSeconds(Long.MAX_VALUE) }) {
            try { NativeTimeout.millis(invalid); throw new AssertionError("invalid timeout accepted: " + invalid); }
            catch (IllegalArgumentException expected) { }
        }
    }

    private static void consumingCallReturnsItsResult() throws Exception {
        var pointer = MemorySegment.ofAddress(0x1300);
        var handle = new NativeHandle(pointer);
        require(handle.callAndConsume(value -> value).equals(pointer), "consuming call lost its result");
        require(handle.closed(), "successful consuming call retained ownership");
    }

    private static void ordinaryCloseConsumesOnlyAfterRawCallReturns() throws Exception {
        var preEntry = new NativeHandle(MemorySegment.ofAddress(0x1100));
        try { preEntry.close(ignored -> { throw new IllegalStateException("before native entry"); });
            throw new AssertionError("expected pre-entry failure"); }
        catch (IllegalStateException expected) {
            require("before native entry".equals(expected.getMessage()), "unexpected pre-entry error");
        }
        require(!preEntry.closed(), "pre-entry failure consumed the native handle");
        preEntry.close(ignored -> MemorySegment.NULL);
        require(preEntry.closed(), "later cleanup did not consume the native handle");

        var remoteError = new NativeHandle(MemorySegment.ofAddress(0x1200));
        var closeCalls = new java.util.concurrent.atomic.AtomicInteger();
        try (Arena resultArena = Arena.ofConfined()) {
            MemorySegment result = owh_result.allocate(resultArena);
            owh_result.status(result, (int) OwhConstants.OWH_SYSTEM_ERROR);
            MemorySegment raw = remoteError.close(ignored -> { closeCalls.incrementAndGet(); return result; });
            try { NativeResults.check("close fixture", raw); throw new AssertionError("expected native close failure"); }
            catch (NativeFailure expected) { }
            require(remoteError.closed(), "native error response did not consume the handle");
            require(remoteError.close(ignored -> { closeCalls.incrementAndGet(); return MemorySegment.NULL; }) == null,
                "repeated close should be an idempotent no-op");
            require(closeCalls.get() == 1, "consumed native close was retried");
        }
    }

    private static void frameArenaIsClosedBeforeNativeReleaseAndConsumeIsOneShot() throws Exception {
        try (var backing = Arena.ofShared()) {
            MemorySegment storage = backing.allocate(8);
            var handle = new NativeHandle(MemorySegment.ofAddress(0x2000));
            final PixelFrame[] owner = new PixelFrame[1];
            final boolean[] invalidBeforeRelease = {false};
            owner[0] = PixelFrameFixture.create(handle, storage, 8, 2, 1, 8, ignored -> {
                try { owner[0].pixels().get(ValueLayout.JAVA_BYTE, 0); }
                catch (IllegalStateException expected) { invalidBeforeRelease[0] = true; }
            });
            require(owner[0].layout().getPixelBytes() == 8 && owner[0].layout().getWidth() == 2 &&
                owner[0].layout().getHeight() == 1 && owner[0].layout().getStride() == 8,
                "generated pixel layout metadata differs from native values");
            require(owner[0].layout().getPixels().equals(MemorySegment.NULL),
                "generated pixel layout retained a borrowed native pointer");
            owner[0].pixels().set(ValueLayout.JAVA_BYTE, 0, (byte)0x5a);
            owner[0].close();
            require(invalidBeforeRelease[0], "native frame was released before escaped memory became invalid");
            try { owner[0].pixels().get(ValueLayout.JAVA_BYTE, 0); throw new AssertionError("frame view survived close"); }
            catch (IllegalStateException expected) { }
            try { owner[0].commit(); throw new AssertionError("consumed frame committed twice"); }
            catch (IllegalStateException expected) { }
        }
    }

    private static void failedPreEntryCommitRetainsFrameForDiscard() throws Exception {
        try (var backing = Arena.ofShared()) {
            var handle = new NativeHandle(MemorySegment.ofAddress(0x3000));
            final int[] discards = {0};
            var frame = PixelFrameFixture.createFailingCommit(handle, backing.allocate(8), 8, 2, 1, 8,
                ignored -> discards[0]++);
            try { frame.commit(); throw new AssertionError("expected pre-entry FFM failure"); }
            catch (IllegalStateException expected) {
                require("before native entry".equals(expected.getMessage()), "unexpected commit failure");
            }
            require(!handle.closed(), "failed FFM invocation consumed native frame");
            frame.close();
            require(discards[0] == 1 && handle.closed(), "frame was not discarded exactly once");
        }
    }

    private static void sequentialFrameCloseDiscardsOnce() throws Exception {
        try (var backing = Arena.ofShared()) {
            var handle = new NativeHandle(MemorySegment.ofAddress(0x4000));
            var discards = new java.util.concurrent.atomic.AtomicInteger();
            var frame = PixelFrameFixture.create(handle, backing.allocate(8), 8, 2, 1, 8, ignored -> {
                discards.incrementAndGet();
            });
            frame.close();
            frame.close();
            require(discards.get() == 1 && handle.closed(), "sequential close did not consume exactly once");
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
