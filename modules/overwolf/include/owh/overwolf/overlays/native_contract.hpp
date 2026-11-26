#pragma once

#include <cstdint>

// Private Windows x64 adapter ABI for one reviewed OWAgent build. These
// signatures and offsets are not part of the public C/JVM contract.
namespace overwolf_hook::overwolf::native_contract {
struct rect32 {
    int32_t left, top, right, bottom;
};
static_assert(sizeof(rect32) == 16);

using paint_fn = uint64_t (*)(void *hwnd, const void *bgra, int32_t width, int32_t height,
                              const rect32 *damage, bool flag_a, bool flag_b,
                              uint64_t *opaque_output);
using accelerated_paint_fn = void (*)(void *hwnd, void *legacy_shared_texture, const rect32 *damage,
                                      bool is_popup);
using register_window_fn = bool (*)(void *current_agent, void *owned_hwnd);
using refresh_windows_fn = void (*)(void *current_agent, bool option, bool force);
using set_position_fn = bool (*)(void *owned_hwnd, int32_t overlay_x, int32_t overlay_y);
using run_agent_fn = void (*)(uint32_t original_argument);
using agent_lifecycle_fn = void (*)();
// AgentBase::Init reports bool in AL; preserve the rest of RAX unchanged.
using agent_init_fn = uint64_t (*)(void *agent, uint32_t original_argument);
} // namespace overwolf_hook::overwolf::native_contract
