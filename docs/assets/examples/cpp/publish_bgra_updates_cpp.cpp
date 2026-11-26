#include <overwolf_hook/overwolf_hook.hpp>
#include <algorithm>
#include <iostream>
#include <vector>

int main() {
    try {
        constexpr uint32_t width = 256, height = 128;
        auto sdk = overwolf_hook::OverwolfHook::open();
        sdk.attach(); // Default restart: run before entering the game.
        owh_overlay_spec spec{};
        owh_overlay_spec_init(&spec);
        spec.width = width;
        spec.height = height;
        spec.left = 64;
        spec.top = 64;
        auto overlay = sdk.overlays().create_pixel_buffer(spec);

        std::vector<std::byte> image(width * height * 4);
        for (int update = 0; update < 2; ++update) {
            // --8<-- [start:draw]
            std::fill(image.begin(), image.end(), std::byte{0});
            const int square_left = 16 + update * 160;
            for (int y = 32; y < 96; ++y) {
                for (int x = square_left; x < square_left + 64; ++x) {
                    const auto pixel = (y * width + x) * 4;
                    image[pixel] = std::byte{0x20};     // B
                    image[pixel + 1] = std::byte{0x80}; // G
                    image[pixel + 2] = std::byte{0xff}; // R
                    image[pixel + 3] = std::byte{0xff}; // A
                }
            }
            // --8<-- [end:draw]
            // --8<-- [start:submit]
            overlay.submit(image); // Zero/default stride: width * 4.
            // image is reusable now; the SDK owns the submitted copy.
            // --8<-- [end:submit]
            std::cout << (update == 0
                ? "First image accepted. Enter the game, then press Enter "
                  "here to move the square.\n"
                : "Second image accepted. Press Enter to close.\n");
            std::cin.get();
        }
        overlay.close();
        sdk.detach();
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
