package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeHandle;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.model.LegacySharedTexture;
import dev.sxlver.overwolfhook.model.OverlayInfo;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;

/** Shared-texture binding for a completed producer-owned D3D11 allocation.
 * This handoff does not select a renderer or provide a later-write fence. */
public final class SharedTextureOverlay implements AutoCloseable {
    private final NativeHandle handle;
    SharedTextureOverlay(NativeHandle handle) { this.handle = handle; }
    public OverlayInfo info() throws NativeFailure {
        MemorySegment overlay = handle.get();
        try (var arena = Arena.ofConfined()) {
            var out = OverlayInfo.allocate(arena);
            NativeResults.check("query overlay", OverwolfNative.owh_overlay_query(arena, overlay, out));
            return OverlayInfo.copyOf(out);
        }
    }
    public void bind(LegacySharedTexture binding) throws NativeFailure {
        if (binding == null) throw new NullPointerException("binding");
        MemorySegment overlay = handle.get();
        try (var arena = Arena.ofConfined()) {
            NativeResults.check("bind shared texture", OverwolfNative.owh_overlay_bind_d3d11(arena, overlay, binding.materialize(arena)));
        }
    }
    @Override public void close() throws NativeFailure {
        try (var arena = Arena.ofConfined()) { var result = handle.close(h -> OverwolfNative.owh_overlay_close(arena, h)); if (result != null) NativeResults.check("close overlay", result); }
        catch (NativeFailure failure) { throw failure; }
        catch (Exception failure) { throw new IllegalStateException("Unable to close overlay", failure); }
    }
}
