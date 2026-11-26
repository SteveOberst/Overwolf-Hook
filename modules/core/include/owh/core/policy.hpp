#pragma once
#include <overwolf_hook/types/client.h>

namespace overwolf_hook::core {
// Immutable, accepted HELLO policy. These are limits, never cached readiness.
struct SessionPolicy {
    uint64_t pixel_buffer_budget_bytes = 0, shared_texture_budget_bytes = 0;
    uint32_t experimental_features = 0, maximum_overlays = 0;
    uint32_t maximum_receipts_per_overlay = 0, reserved = 0;
};
inline constexpr uint32_t maximum_overlays = 256, maximum_receipts = 4096;
inline constexpr uint32_t maximum_process_overlays = 4096;
inline constexpr uint32_t maximum_width = 4096, maximum_height = 4096;
inline constexpr uint32_t minimum_slots = 2, maximum_slots = 8;
inline constexpr uint64_t maximum_image_bytes = 64ull * 1024 * 1024;
inline constexpr uint64_t maximum_mapping_bytes = 256ull * 1024 * 1024;
inline constexpr uint64_t process_budget_bytes = 1024ull * 1024 * 1024;

inline bool valid_options(const owh_client_options &value) noexcept {
    return value.size == sizeof(value) && value.abi_version == OWH_ABI_VERSION &&
           (value.attach_mode == OWH_ATTACH_RESTART || value.attach_mode == OWH_ATTACH_HOT) &&
           !(value.experimental_features & ~OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE) &&
           value.maximum_overlays <= maximum_overlays &&
           value.maximum_receipts_per_overlay <= maximum_receipts &&
           value.pixel_buffer_budget_bytes <= process_budget_bytes &&
           value.shared_texture_budget_bytes <= process_budget_bytes;
}
inline SessionPolicy resolve_options(const owh_client_options &input) noexcept {
    return {input.pixel_buffer_budget_bytes ? input.pixel_buffer_budget_bytes : maximum_mapping_bytes,
            input.shared_texture_budget_bytes ? input.shared_texture_budget_bytes : maximum_mapping_bytes,
            input.experimental_features,
            input.maximum_overlays ? input.maximum_overlays : 16,
            input.maximum_receipts_per_overlay ? input.maximum_receipts_per_overlay : 256,
            0};
}
inline bool valid_policy(const SessionPolicy &value) noexcept {
    return !value.reserved && !(value.experimental_features & ~OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE) &&
           value.maximum_overlays && value.maximum_overlays <= maximum_overlays &&
           value.maximum_receipts_per_overlay &&
           value.maximum_receipts_per_overlay <= maximum_receipts &&
           value.pixel_buffer_budget_bytes &&
           value.pixel_buffer_budget_bytes <= process_budget_bytes &&
           value.shared_texture_budget_bytes && value.shared_texture_budget_bytes <= process_budget_bytes;
}
inline bool same_policy(const SessionPolicy &a, const SessionPolicy &b) noexcept {
    return a.reserved == b.reserved && a.experimental_features == b.experimental_features &&
           a.maximum_overlays == b.maximum_overlays &&
           a.maximum_receipts_per_overlay == b.maximum_receipts_per_overlay &&
           a.pixel_buffer_budget_bytes == b.pixel_buffer_budget_bytes &&
           a.shared_texture_budget_bytes == b.shared_texture_budget_bytes;
}
} // namespace overwolf_hook::core
