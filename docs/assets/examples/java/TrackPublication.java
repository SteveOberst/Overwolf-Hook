package examples.overlays;

import dev.sxlver.overwolfhook.NativeFailure;
import dev.sxlver.overwolfhook.PixelBufferOverlay;
import java.lang.foreign.MemorySegment;
import dev.sxlver.overwolfhook.model.SubmissionInfo;
import java.time.Duration;

/** Submit a completed image and observe its native publication outcome. */
public final class TrackPublication {
    private TrackPublication() {}

    public static SubmissionInfo submitAndWait(PixelBufferOverlay overlay, MemorySegment image)
        throws NativeFailure {
        try (var receipt = overlay.submitTracked(image)) {
            // May throw TIMEOUT; closing the receipt does not cancel
            // publication.
            return receipt.await(Duration.ofSeconds(1));
        }
    }
}
