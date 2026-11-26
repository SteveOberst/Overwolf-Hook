#pragma once
#include <algorithm>
#include <overwolf_hook/types/overlays.h>

namespace overwolf_hook::core {
using PixelRegion = owh_pixel_region;
inline PixelRegion full_region(uint32_t width, uint32_t height) noexcept {
    return {0, 0, width, height};
}
inline bool valid_region(PixelRegion r, uint32_t width, uint32_t height) noexcept {
    return r.width && r.height && r.x <= width && r.y <= height &&
           r.width <= width - r.x && r.height <= height - r.y;
}
/** Union of valid regions; the all-zero internal value represents no changes. */
inline PixelRegion union_region(PixelRegion a, PixelRegion b) noexcept {
    if (!a.width || !a.height) return b;
    const auto x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(a.x + a.width, b.x + b.width) - x,
                  std::max(a.y + a.height, b.y + b.height) - y};
}
} // namespace overwolf_hook::core
