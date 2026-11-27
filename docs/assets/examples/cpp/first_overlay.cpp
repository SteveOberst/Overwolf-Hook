#include <vector>
#include <iostream>
#include <overwolf_hook/overwolf_hook.hpp>

int main() {
    try {
        auto sdk = overwolf_hook::OverwolfHook::open();
        sdk.attach(); // Default restart: run before entering the game.
        owh_overlay_spec spec{};
        owh_overlay_spec_init(&spec);
        spec.width = 256;
        spec.height = 128;
        spec.left = 64;
        spec.top = 64;
        auto overlay = sdk.overlays().create_pixel_buffer(spec);
        std::vector<std::byte> pixels(spec.width * spec.height * 4);
        for (uint32_t y = 16; y < 112; ++y) {
            for (uint32_t x = 16; x < 240; ++x) {
                auto offset =
                    static_cast<size_t>(y) * spec.width * 4 + x * 4;
                pixels[offset] = std::byte{0x20};     // B
                pixels[offset + 1] = std::byte{0x80}; // G
                pixels[offset + 2] = std::byte{0xff}; // R
                pixels[offset + 3] = std::byte{0xff}; // A
            }
        }
        overlay.submit(pixels);
        std::cout << "Image submitted. Check the game window, "
                     "then press Enter to close.\n";
        std::cin.get();
        overlay.close();
        sdk.detach();
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
