#pragma once

#include <windows.h>

#include <cstdint>
#include <span>

namespace overwolf_hook::overwolf {
struct ResolvedProfile {
    bool common = false;
    bool pixel_buffer_source = false;
    bool shared_texture_source = false;
    bool lifecycle = false;
    uint64_t source_global = 0;
    uint64_t agent_global_slot = 0;
    uint64_t run_agent = 0;

    uint64_t clear_agent = 0;

    uint64_t unhook_agent = 0;

    uint64_t agent_init = 0;

    uint64_t agent_clear = 0;

    uint64_t source_paint = 0;
    uint64_t accelerated_paint = 0;
    uint64_t set_position = 0;
    uint64_t register_window = 0;
    uint64_t refresh_windows = 0;
};

/** Resolve loaded executable code by signatures and structural checks, regardless
 * of version/hash. This is compatibility screening, not a correctness guarantee. */
ResolvedProfile resolve_loaded_profile(HMODULE agent) noexcept;
/** Resolve against a caller-owned mapped PE image. Used by owned synthetic PE
 * fixtures and read-only disk preflight; Attach rechecks the loaded module.
 */
ResolvedProfile resolve_profile_image(std::span<const uint8_t> image, uint64_t image_base) noexcept;
} // namespace overwolf_hook::overwolf
