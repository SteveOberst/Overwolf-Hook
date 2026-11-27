package examples.overlays

import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.kotlin.overlays
import dev.sxlver.overwolfhook.model.ClientOptions
import dev.sxlver.overwolfhook.model.LegacySharedTexture
import dev.sxlver.overwolfhook.model.OverlaySpec
import dev.sxlver.overwolfhook.model.OwhConstants

/** Takes ownership of one completed, unchanged renderer allocation. */
fun showCompletedTexture(
    allocationOwner: AutoCloseable,
    tokenBits: Long,
    luidLow: Long,
    luidHigh: Int,
    width: Int,
    height: Int,
) {
    // The owner's close() releases its native texture/device references.
    allocationOwner.use {
        OverwolfHook.open().use { sdk ->
            // --8<-- [start:attach]
            sdk.attach(ClientOptions().setExperimentalFeatures(
                OwhConstants.OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE))
            // --8<-- [end:attach]
            try {
                // --8<-- [start:binding]
                val spec = OverlaySpec().setWidth(width.toLong())
                    .setHeight(height.toLong()).setLeft(64).setTop(64)
                val texture = LegacySharedTexture()
                    .setLegacySharedToken(tokenBits)
                    .setAdapterLuidLow(luidLow)
                    .setAdapterLuidHigh(luidHigh)
                // --8<-- [end:binding]
                // --8<-- [start:scope]
                sdk.overlays.createSharedTexture(spec, texture).use {
                    println("Texture accepted. Enter the game, then " +
                        "press Enter here to close.")
                    readlnOrNull()
                } // Close overlay before allocationOwner releases its texture.
                // --8<-- [end:scope]
            } finally {
                sdk.detach()
            }
        }
    }
}
