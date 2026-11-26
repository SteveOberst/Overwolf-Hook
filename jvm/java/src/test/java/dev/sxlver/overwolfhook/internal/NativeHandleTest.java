package dev.sxlver.overwolfhook.internal;

import static org.junit.jupiter.api.Assertions.*;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.junit.jupiter.api.Test;

final class NativeHandleTest {
    @Test void closeConsumesOnlyAfterTheDowncallReturns() throws Exception {
        try (var arena = Arena.ofConfined()) {
            var handle = new NativeHandle(arena.allocate(8));
            var original = handle.get();
            assertEquals(original, handle.callAndConsume(h -> h));
            assertTrue(handle.closed());
        }
    }

    @Test void preEntryFailureLeavesOwnershipAvailable() throws Exception {
        try (var arena = Arena.ofConfined()) {
            var handle = new NativeHandle(arena.allocate(8));
            assertThrows(IllegalStateException.class, () -> handle.callAndConsume(h -> {
                throw new IllegalStateException("rejected before entry");
            }));
            assertFalse(handle.closed());
            var result = handle.close(h -> MemorySegment.NULL);
            assertEquals(MemorySegment.NULL, result);
            assertTrue(handle.closed());
            assertNull(handle.close(h -> fail("close must be idempotent")));
        }
    }
}
