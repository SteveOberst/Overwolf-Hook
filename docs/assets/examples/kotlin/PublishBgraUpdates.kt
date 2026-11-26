package examples.overlays

import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.kotlin.overlays
import dev.sxlver.overwolfhook.model.OverlaySpec

object PublishBgraUpdates {
    @JvmStatic
    fun main(args: Array<String>) {
        val width = 256
        val height = 128
        OverwolfHook.open().use { sdk ->
            sdk.attach() // Default restart: run before entering the game.
            try {
                val spec = OverlaySpec().setWidth(width.toLong())
                    .setHeight(height.toLong()).setLeft(64).setTop(64)
                sdk.overlays.createPixelBuffer(spec).use { overlay ->
                    val image = ByteArray(width * height * 4)
                    repeat(2) { update ->
                        // --8<-- [start:draw]
                        image.fill(0)
                        val squareLeft = 16 + update * 160
                        for (y in 32 until 96) {
                            for (x in squareLeft until squareLeft + 64) {
                                val pixel = (y * width + x) * 4
                                image[pixel] = 0x20 // B
                                image[pixel + 1] = 0x80.toByte() // G
                                image[pixel + 2] = 0xff.toByte() // R
                                image[pixel + 3] = 0xff.toByte() // A
                            }
                        }
                        // --8<-- [end:draw]
                        // --8<-- [start:submit]
                        overlay.submit(image)
                        // image is reusable; the SDK owns the submitted copy.
                        // --8<-- [end:submit]
                        println(if (update == 0)
                            "First image accepted. Enter the game, then " +
                                "press Enter here to move the square."
                        else "Second image accepted. Press Enter to close.")
                        readlnOrNull()
                    }
                } // Close the overlay before detach.
            } finally {
                sdk.detach()
            }
        }
    }
}
