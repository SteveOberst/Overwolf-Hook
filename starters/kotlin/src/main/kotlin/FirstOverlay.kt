import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.kotlin.overlays
import dev.sxlver.overwolfhook.model.OverlaySpec

private fun publishPanel(
    overlay: dev.sxlver.overwolfhook.PixelBufferOverlay, width: Int, height: Int,
) {
    val rowBytes = width * 4
    val image = ByteArray(rowBytes * height)
    for (y in 16 until height - 16) {
        for (x in 16 until width - 16) {
            val pixel = y * rowBytes + x * 4
            image[pixel] = 0x20
            image[pixel + 1] = 0x80.toByte()
            image[pixel + 2] = 0xff.toByte()
            image[pixel + 3] = 0xff.toByte()
        }
    }
    overlay.submit(image)
}

fun main() {
    OverwolfHook.open().use { sdk ->
        // Restart mode may close and relaunch Overwolf; see the starter README.
        sdk.attach()
        try {
            val width = 256
            val height = 128
            val spec = OverlaySpec().setWidth(width.toLong())
                .setHeight(height.toLong())
                .setLeft(64).setTop(64)
            sdk.overlays.createPixelBuffer(spec).use { overlay ->
                publishPanel(overlay, width, height)
                println("Image submitted. Check the game window, " +
                    "then press Enter to close.")
                readlnOrNull()
            }
        } finally {
            sdk.detach()
        }
    }
}
