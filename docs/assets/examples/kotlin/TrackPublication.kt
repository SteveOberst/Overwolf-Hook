package examples.overlays

import dev.sxlver.overwolfhook.PixelBufferOverlay
import java.lang.foreign.MemorySegment
import dev.sxlver.overwolfhook.kotlin.awaitCancellable
import dev.sxlver.overwolfhook.model.SubmissionInfo
import kotlin.time.Duration.Companion.seconds

/** Submit a completed image and observe its native publication outcome. */
suspend fun submitAndWait(overlay: PixelBufferOverlay, image: MemorySegment): SubmissionInfo =
    overlay.submitTracked(image).use { receipt ->
        // Cancellation signals and joins the native wait before close.
        // Neither cancellation nor TIMEOUT cancels publication of this image.
        receipt.awaitCancellable(1.seconds)
    }
