package dev.sxlver.overwolfhook.kotlin

import dev.sxlver.overwolfhook.*
import dev.sxlver.overwolfhook.model.LegacySharedTexture
import dev.sxlver.overwolfhook.model.PixelRegion
import java.lang.foreign.MemorySegment
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.withContext
import kotlin.time.Duration

val OverwolfHook.overwolf get() = overwolf()

val OverwolfHook.overlays get() = overlays()
val OverwolfHook.events get() = events()
val OverwolfHook.gameState get() = gameState()

fun PixelBufferOverlay.tryAcquireOrNull(): PixelFrame? = tryAcquire().orElse(null)
fun Submission.await(timeout: kotlin.time.Duration, cancellation: WaitCancellation? = null): dev.sxlver.overwolfhook.model.SubmissionInfo =
    await(timeout.optionalJavaTimeout(), cancellation)
fun SharedTextureOverlay.bindLegacy(tokenBits: Long, adapterLuidLow: Int, adapterLuidHigh: Int) =
    bind(LegacySharedTexture().setLegacySharedToken(tokenBits).setAdapterLuidLow(Integer.toUnsignedLong(adapterLuidLow)).setAdapterLuidHigh(adapterLuidHigh))


/** Submit a complete image with optional row padding. Input can be reused on return. */
fun PixelBufferOverlay.submit(image: ByteArray, sourceStride: Int = 0, damage: PixelRegion? = null) =
    submit(MemorySegment.ofArray(image), sourceStride, null, null, damage)

/** Cancellable admission for FIFO backpressure. Default latest submission needs
 * no coroutine scheduler; the ordinary submit method is sufficient. */
suspend fun PixelBufferOverlay.submitCancellable(
    image: MemorySegment,
    sourceStride: Int = 0,
    timeout: Duration = Duration.INFINITE,
    damage: PixelRegion? = null,
): Unit = coroutineScope {
    val nativeTimeout = timeout.optionalJavaTimeout()
    WaitCancellation.create().use { token ->
        val writer = async(Dispatchers.IO) { submit(image, sourceStride, nativeTimeout, token, damage) }
        try { writer.await() }
        finally {
            withContext(NonCancellable) {
                try { token.cancel() }
                finally { writer.join() }
            }
        }
    }
}
