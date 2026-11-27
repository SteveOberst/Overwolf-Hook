package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeHandle;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.internal.NativeAdoption;
import dev.sxlver.overwolfhook.model.EventCapabilities;
import dev.sxlver.overwolfhook.model.EventOptions;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import static java.lang.foreign.ValueLayout.ADDRESS;

/** Event operations bound to the current SDK context. */
public final class EventController {
    private final OverwolfHook sdk;
    EventController(OverwolfHook sdk) { this.sdk = sdk; }
    public EventCapabilities capabilities() throws NativeFailure {
        MemorySegment nativeSdk = sdk.handle().get();
        try (var arena = Arena.ofConfined()) {
            var out = EventCapabilities.allocate(arena);
            NativeResults.check("query event capabilities", OverwolfNative.owh_event_query_capabilities(arena, nativeSdk, out));
            return EventCapabilities.copyOf(out);
        }
    }
    public EventSubscription subscribe() throws NativeFailure { return subscribe(new EventOptions()); }
    public EventSubscription subscribe(EventOptions options) throws NativeFailure {
        if (options == null) throw new NullPointerException("options");
        MemorySegment nativeSdk = sdk.handle().get();
        try (var arena = Arena.ofConfined()) {
            var out = arena.allocate(ADDRESS);
            NativeResults.check("subscribe to events", OverwolfNative.owh_event_subscribe(arena, nativeSdk, options.materialize(arena), out));
            MemorySegment handle = out.get(ADDRESS, 0);
            if (handle.equals(MemorySegment.NULL)) throw new IllegalStateException("Native subscribe returned no subscription");
            try { return new EventSubscription(new NativeHandle(handle)); }
            catch (RuntimeException | Error failure) {
                NativeAdoption.cleanup(handle, raw -> NativeResults.check("release unadopted subscription",
                    OverwolfNative.owh_subscription_close(arena, raw)), failure);
                throw failure;
            }
        }
    }
}
