package dev.sxlver.overwolfhook;

import dev.sxlver.overwolfhook.bindings.OverwolfNative;
import dev.sxlver.overwolfhook.internal.NativeHandle;
import dev.sxlver.overwolfhook.internal.NativeResults;
import dev.sxlver.overwolfhook.internal.NativeTimeout;
import dev.sxlver.overwolfhook.model.Event;
import dev.sxlver.overwolfhook.model.SubscriptionInfo;
import dev.sxlver.overwolfhook.model.WaitOptions;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.Optional;
import static java.lang.foreign.ValueLayout.ADDRESS;

/** One consuming live event stream. Calls on a subscription must be serialized. */
public final class EventSubscription implements AutoCloseable {
    private final NativeHandle handle;
    EventSubscription(NativeHandle handle) { this.handle = handle; }

    public SubscriptionInfo info() throws NativeFailure {
        MemorySegment subscription = handle.get();
        try (var arena = Arena.ofConfined()) {
            var out = SubscriptionInfo.allocate(arena);
            NativeResults.check("query subscription", OverwolfNative.owh_subscription_query(arena, subscription, out));
            return SubscriptionInfo.copyOf(out);
        }
    }

    public Optional<Event> poll() throws NativeFailure {
        MemorySegment subscription = handle.get();
        try (var arena = Arena.ofConfined()) {
            var out = Event.allocate(arena);
            var result = OverwolfNative.owh_subscription_poll(arena, subscription, out);
            int status = NativeResults.status(result);
            if (status == OwhStatus.CLOSED) throw new EventStreamClosedException();
            if (empty(status)) return Optional.empty();
            NativeResults.check("poll event subscription", result);
            return Optional.of(Event.copyOf(out));
        }
    }

    public Optional<Event> receive() throws NativeFailure { return receive(null, null); }

    /** Null timeout means infinite; cancellation may be signaled from another thread. */
    public Optional<Event> receive(Duration timeout) throws NativeFailure {
        return receive(timeout, null);
    }

    public Optional<Event> receive(Duration timeout, WaitCancellation cancellation) throws NativeFailure {
        long timeoutMillis = NativeTimeout.millis(timeout);
        MemorySegment token = cancellation == null ? null : cancellation.get();
        MemorySegment subscription = handle.get();
        try (var arena = Arena.ofConfined()) {
            MemorySegment options = MemorySegment.NULL;
            if (timeout != null || token != null) {
                var value = new WaitOptions().setTimeoutMs(timeoutMillis)
                    .setCancellation(token == null ? MemorySegment.NULL : token);
                options = value.materialize(arena);
            }
            var out = Event.allocate(arena);
            var result = OverwolfNative.owh_subscription_receive(arena, subscription, options, out);
            int status = NativeResults.status(result);
            if (status == OwhStatus.CLOSED) throw new EventStreamClosedException();
            if (empty(status)) return Optional.empty();
            NativeResults.check("receive event", result);
            return Optional.of(Event.copyOf(out));
        }
    }

    private static boolean empty(int status) {
        return status == OwhStatus.NO_DATA || status == OwhStatus.BUSY || status == OwhStatus.TIMEOUT;
    }

    @Override public void close() throws NativeFailure {
        try (var arena = Arena.ofConfined()) {
            var result = handle.close(h -> OverwolfNative.owh_subscription_close(arena, h));
            if (result != null) NativeResults.check("close subscription", result);
        } catch (NativeFailure failure) { throw failure; }
        catch (Exception failure) { throw new IllegalStateException("Unable to close subscription", failure); }
    }

    private static final class OwhStatus {
        static final long CLOSED = dev.sxlver.overwolfhook.model.OwhConstants.OWH_CLOSED;
        static final long NO_DATA = dev.sxlver.overwolfhook.model.OwhConstants.OWH_NO_DATA;
        static final long BUSY = dev.sxlver.overwolfhook.model.OwhConstants.OWH_BUSY;
        static final long TIMEOUT = dev.sxlver.overwolfhook.model.OwhConstants.OWH_TIMEOUT;
    }
}
