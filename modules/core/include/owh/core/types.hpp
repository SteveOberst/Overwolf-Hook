#pragma once
#include "policy.hpp"
#include <cstdint>
#include <overwolf_hook/types/build_info.h>
#include <overwolf_hook/types/events.h>
#include <overwolf_hook/types/overlays.h>
namespace overwolf_hook::core {
/** Identity verified at service admission, separate from bootstrap framing. */
struct PeerIdentity {
    uint32_t client_process_id = 0, host_process_id = 0, windows_session_id = 0;
    uint64_t client_process_started = 0, host_process_started = 0, host_nonce = 0;
    owh_build_info build_info{};
};
struct ResourceRef {
    uint64_t session_id = 0, resource_id = 0;
    friend bool operator==(const ResourceRef &, const ResourceRef &) = default;
};
struct PixelBufferRef {
    ResourceRef ref;
    uint64_t mapping_handle, wake_handle, completion_handle;
    uint64_t producer_started, consumer_started, mapping_bytes;
    uint32_t producer_pid, consumer_pid, width, height, slot_count, queue_policy;
};
// Fixed payload copied at the reviewed Overwolf event dispatcher. Sequence and
// loss accounting are assigned by the EventHub/EventLog publisher.
struct CapturedEvent {
    uint64_t captured_qpc;
    uint32_t source_channel, producer_process_id, raw_size;
    decltype(owh_event::raw) raw;
};
using Geometry = owh_overlay_spec;
using PixelBufferOverlayOptions = owh_pixel_buffer_overlay_options;
inline PixelBufferOverlayOptions resolve_queue(uint32_t slots, uint32_t policy) noexcept {
    return {sizeof(PixelBufferOverlayOptions), slots ? slots : 3, policy ? policy : OWH_QUEUE_LATEST_READY};
}
inline bool valid_geometry(const Geometry &g, uint32_t max_width = maximum_width,
                           uint32_t max_height = maximum_height) noexcept {
    return g.width && g.height && g.width <= max_width && g.height <= max_height;
}
inline bool valid_queue(PixelBufferOverlayOptions q) noexcept {
    return q.slot_count >= minimum_slots && q.slot_count <= maximum_slots &&
           (q.policy == OWH_QUEUE_FIFO || q.policy == OWH_QUEUE_LATEST_READY);
}
using LegacySharedTexture = owh_legacy_shared_texture;
} // namespace overwolf_hook::core
