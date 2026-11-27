package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeHandle;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.internal.NativeAdoption;
import dev.sxlver.overwolfhook.model.OverlayCapabilities;
import dev.sxlver.overwolfhook.model.LegacySharedTexture;
import dev.sxlver.overwolfhook.model.OverlaySpec;
import dev.sxlver.overwolfhook.model.PixelBufferOverlayOptions;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import static java.lang.foreign.ValueLayout.ADDRESS;

/** Overlay creation and capabilities for the SDK's current attachment. */
public final class OverlayController {
    private final OverwolfHook sdk;
    OverlayController(OverwolfHook sdk) { this.sdk = sdk; }
    public OverlayCapabilities capabilities() throws NativeFailure {
        MemorySegment nativeSdk = sdk.handle().get();
        try (var arena = Arena.ofConfined()) {
            var out = OverlayCapabilities.allocate(arena);
            NativeResults.check("query overlay capabilities", OverwolfNative.owh_overlay_query_capabilities(arena, nativeSdk, out));
            return OverlayCapabilities.copyOf(out);
        }
    }
    public PixelBufferOverlay createPixelBuffer(OverlaySpec spec) throws NativeFailure { return createPixelBuffer(spec, new PixelBufferOverlayOptions()); }
    public PixelBufferOverlay createPixelBuffer(OverlaySpec spec, PixelBufferOverlayOptions queue) throws NativeFailure {
        if (spec == null || queue == null) throw new NullPointerException("spec and queue are required");
        MemorySegment nativeSdk = sdk.handle().get();
        try (var arena = Arena.ofConfined()) {
            var out = arena.allocate(ADDRESS);
            NativeResults.check("create pixel-buffer overlay", OverwolfNative.owh_overlay_create_pixel_buffer(arena, nativeSdk,
                spec.materialize(arena), queue.materialize(arena), out));
            MemorySegment overlay = out.get(ADDRESS, 0);
            if (overlay.equals(MemorySegment.NULL)) throw new IllegalStateException("Native create returned no overlay");
            try { return new PixelBufferOverlay(new NativeHandle(overlay)); }
            catch (RuntimeException | Error failure) { closeUnadopted(arena, overlay, failure); throw failure; }
        }
    }
    /** Create and bind a completed texture in one operation. The allocation
     * remains owned by your renderer and must outlive the overlay. */
    public SharedTextureOverlay createSharedTexture(OverlaySpec spec, LegacySharedTexture texture)
            throws NativeFailure {
        if (spec == null || texture == null) throw new NullPointerException("spec and texture are required");
        try (var arena = Arena.ofConfined()) {
            var out = arena.allocate(ADDRESS);
            NativeResults.check("create shared-texture overlay", OverwolfNative.owh_overlay_create_shared_texture(
                arena, sdk.handle().get(), spec.materialize(arena), texture.materialize(arena), out));
            MemorySegment overlay = out.get(ADDRESS, 0);
            if (overlay.equals(MemorySegment.NULL)) throw new IllegalStateException("Native create returned no overlay");
            try { return new SharedTextureOverlay(new NativeHandle(overlay)); }
            catch (RuntimeException | Error failure) { closeUnadopted(arena, overlay, failure); throw failure; }
        }
    }
    /** Expert factory for callers that must create before allocating their texture. */
    public SharedTextureOverlay createUnboundSharedTexture(OverlaySpec spec) throws NativeFailure {
        if (spec == null) throw new NullPointerException("spec");
        MemorySegment nativeSdk = sdk.handle().get();
        try (var arena = Arena.ofConfined()) {
            var out = arena.allocate(ADDRESS);
            NativeResults.check("create shared-texture overlay", OverwolfNative.owh_overlay_create_unbound_shared_texture(arena,
                nativeSdk, spec.materialize(arena), out));
            MemorySegment overlay = out.get(ADDRESS, 0);
            if (overlay.equals(MemorySegment.NULL)) throw new IllegalStateException("Native create returned no overlay");
            try { return new SharedTextureOverlay(new NativeHandle(overlay)); }
            catch (RuntimeException | Error failure) { closeUnadopted(arena, overlay, failure); throw failure; }
        }
    }
    private static void closeUnadopted(Arena arena, MemorySegment raw, Throwable failure) {
        NativeAdoption.cleanup(raw, handle -> NativeResults.check("release unadopted overlay",
            OverwolfNative.owh_overlay_close(arena, handle)), failure);
    }
}
