package examples.overlays

import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.kotlin.overlays
import dev.sxlver.overwolfhook.model.OverlaySpec

/** Publish one complete BGRA image and keep the overlay open until Enter. */
fun main() {
    OverwolfHook.open().use { sdk ->
        sdk.attach()
        try {
            val width = 256
            val height = 128
            val sourceStride = width * 4
            val spec = OverlaySpec().setWidth(width.toLong())
                .setHeight(height.toLong())
                .setLeft(64).setTop(64)
            sdk.overlays.createPixelBuffer(spec).use { overlay ->
                val image = ByteArray(sourceStride * height)
                for (y in 16 until height - 16) {
                    for (x in 16 until width - 16) {
                        val pixel = y * sourceStride + x * 4
                        image[pixel] = 0x20 // B
                        image[pixel + 1] = 0x80.toByte() // G
                        image[pixel + 2] = 0xff.toByte() // R
                        image[pixel + 3] = 0xff.toByte() // A
                    }
                }

                overlay.submit(image)

                println("Image submitted. Check the game window, " +
                    "then press Enter to close.")
                readlnOrNull()
            }
        } finally {
            sdk.detach()
        }
    }
}
