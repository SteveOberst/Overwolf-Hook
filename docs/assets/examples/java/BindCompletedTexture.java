package examples.overlays;

import dev.sxlver.overwolfhook.OverwolfHook;
import dev.sxlver.overwolfhook.model.ClientOptions;
import dev.sxlver.overwolfhook.model.LegacySharedTexture;
import dev.sxlver.overwolfhook.model.OverlaySpec;
import dev.sxlver.overwolfhook.model.OwhConstants;

public final class BindCompletedTexture {
    /** Takes ownership of one completed, unchanged renderer allocation.
     * The owner releases its native texture/device references in close(). */
    public static void show(AutoCloseable allocationOwner, long tokenBits,
                            long luidLow, int luidHigh, int width,
                            int height) throws Exception {
        // Keep the producer allocation through overlay close and SDK cleanup.
        try (allocationOwner; var sdk = OverwolfHook.open()) {
            // --8<-- [start:attach]
            sdk.attach(new ClientOptions().setExperimentalFeatures(
                OwhConstants.OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE));
            // --8<-- [end:attach]
            try {
                // --8<-- [start:binding]
                var spec = new OverlaySpec().setWidth(width)
                    .setHeight(height).setLeft(64).setTop(64);
                var texture = new LegacySharedTexture()
                    .setLegacySharedToken(tokenBits)
                    .setAdapterLuidLow(luidLow)
                    .setAdapterLuidHigh(luidHigh);
                // --8<-- [end:binding]
                // --8<-- [start:scope]
                try (var overlay =
                         sdk.overlays().createSharedTexture(spec, texture)) {
                    System.out.println("Texture accepted. Enter the game, "
                        + "then press Enter here to close.");
                    System.in.read();
                } // Close overlay before allocationOwner releases its texture.
                // --8<-- [end:scope]
            } finally {
                sdk.detach();
            }
        }
    }
}
