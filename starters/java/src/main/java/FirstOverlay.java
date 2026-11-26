import dev.sxlver.overwolfhook.OverwolfHook;
import dev.sxlver.overwolfhook.PixelBufferOverlay;
import dev.sxlver.overwolfhook.model.OverlaySpec;

public final class FirstOverlay {
    private static void publishPanel(PixelBufferOverlay overlay, int width,
                                     int height) throws Exception {
        int rowBytes = width * 4;
        byte[] image = new byte[rowBytes * height];
        for (int y = 16; y < height - 16; y++) {
            for (int x = 16; x < width - 16; x++) {
                int pixel = y * rowBytes + x * 4;
                image[pixel] = 0x20;           // B
                image[pixel + 1] = (byte)0x80; // G
                image[pixel + 2] = (byte)0xff; // R
                image[pixel + 3] = (byte)0xff; // A
            }
        }
        overlay.submit(image);
    }

    public static void main(String[] args) throws Exception {
        try (OverwolfHook sdk = OverwolfHook.open()) {
            // Restart mode may close and relaunch Overwolf; see the starter
            // README.
            sdk.attach();
            try {
                int width = 256;
                int height = 128;
                OverlaySpec spec = new OverlaySpec()
                                       .setWidth(width)
                                       .setHeight(height)
                                       .setLeft(64)
                                       .setTop(64);
                try (PixelBufferOverlay overlay = sdk.overlays().createPixelBuffer(spec)) {
                    publishPanel(overlay, width, height);
                    System.out.println("Image submitted. Check the game " +
                                       "window, then press Enter to close.");
                    System.in.read();
                }
            } finally {
                sdk.detach();
            }
        }
    }
}
