#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <owh/core/pixel_region.hpp>

namespace overwolf_hook::overwolf {

/** Borrowed, immutable pixels valid only until the native publish call returns. */
struct PixelView {
    std::span<const std::byte> bytes;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    // Empty means full publication for existing direct/native callers.
    core::PixelRegion damage{};
};

} // namespace overwolf_hook::overwolf
