package examples.overlays;

import dev.sxlver.overwolfhook.OverwolfHook;
import dev.sxlver.overwolfhook.model.OverlaySpec;
import java.util.Arrays;
import java.io.BufferedReader;
import java.io.InputStreamReader;

public final class PublishBgraUpdates {
    public static void main(String[] args) throws Exception {
        int width = 256, height = 128;
        var console = new BufferedReader(new InputStreamReader(System.in));
        try (var sdk = OverwolfHook.open()) {
            sdk.attach(); // Default restart: run before entering the game.
            try {
                var spec = new OverlaySpec().setWidth(width)
                    .setHeight(height).setLeft(64).setTop(64);
                try (var overlay = sdk.overlays().createPixelBuffer(spec)) {
                    byte[] image = new byte[width * height * 4];
                    for (int update = 0; update < 2; ++update) {
                        // --8<-- [start:draw]
                        Arrays.fill(image, (byte)0);
                        int squareLeft = 16 + update * 160;
                        for (int y = 32; y < 96; ++y) {
                            for (int x = squareLeft;
                                 x < squareLeft + 64; ++x) {
                                int pixel = (y * width + x) * 4;
                                image[pixel] = 0x20;           // B
                                image[pixel + 1] = (byte)0x80; // G
                                image[pixel + 2] = (byte)0xff; // R
                                image[pixel + 3] = (byte)0xff; // A
                            }
                        }
                        // --8<-- [end:draw]
                        // --8<-- [start:submit]
                        overlay.submit(image);
                        // image is reusable; the SDK owns the submitted copy.
                        // --8<-- [end:submit]
                        System.out.println(update == 0
                            ? "First image accepted. Enter the game, then "
                              + "press Enter here to move the square."
                            : "Second image accepted. Press Enter to close.");
                        console.readLine();
                    }
                } // Close the overlay before detach.
            } finally {
                sdk.detach();
            }
        }
    }
}
