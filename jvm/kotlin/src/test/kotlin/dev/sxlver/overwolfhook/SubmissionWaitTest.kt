package dev.sxlver.overwolfhook

import dev.sxlver.overwolfhook.internal.NativeHandle
import dev.sxlver.overwolfhook.kotlin.await
import dev.sxlver.overwolfhook.kotlin.awaitCancellable
import java.lang.foreign.MemorySegment
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertFailsWith
import kotlin.time.Duration

class SubmissionWaitTest {
    @Test fun infiniteSubmissionWaitsReachTheReceipt(): Unit = runBlocking {
        val handle = NativeHandle(MemorySegment.ofAddress(0x5000))
        handle.callAndClose { }
        val closed = Submission(handle)
        assertFailsWith<IllegalStateException> { closed.await(Duration.INFINITE) }
        // Passive library loading; the helper creates only its private wait token.
        OverwolfHook.open().use {
            assertFailsWith<IllegalStateException> { closed.awaitCancellable() }
        }
    }
}
