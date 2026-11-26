#ifndef OVERWOLF_HOOK_TYPES_OVERLAYS_H
#define OVERWOLF_HOOK_TYPES_OVERLAYS_H
#include <overwolf_hook/types/client.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Image handoff mechanisms, independent of the application's renderer.
 * Pixel-buffer submission copies a completed image into SDK-owned shared slots.
 * Shared-texture binding imports a completed producer-owned D3D11 allocation.
 * Neither mechanism selects CPU/GPU rendering for the application. */
enum { OWH_CAP_PIXEL_BUFFER = 1u, OWH_CAP_D3D11_SHARED_TEXTURE = 2u };

/** Point-in-time native source capability snapshot. `available` is independent
 * of opt-in; `enabled` is the usable subset. Feature results diagnose only the
 * corresponding source operation. No overlay, HWND, GPU device, game, or
 * downstream consumer probe occurs. Results do not promise later display.
 */
typedef struct OWH_META("model=OverlayCapabilities;output;size=size") owh_overlay_capabilities {
    uint32_t size;
    uint32_t available;
    uint32_t enabled;
    uint32_t reserved;
    uint32_t process_id;
    uint32_t maximum_overlays;
    uint64_t process_started;
    uint64_t session_id;
    uint32_t maximum_width;
    uint32_t maximum_height;
    uint32_t minimum_slots;
    uint32_t maximum_slots;
    uint64_t maximum_pixel_buffer_bytes;
    uint64_t pixel_buffer_budget_bytes;
    uint64_t shared_texture_budget_bytes;
    uint32_t maximum_receipts_per_overlay;
    owh_result pixel_buffer_status;
    owh_result shared_texture_status;
} owh_overlay_capabilities;

typedef struct owh_overlay owh_overlay;
typedef struct owh_frame owh_frame;
typedef struct owh_submission owh_submission;
/** Optional changed area in image-local physical pixels. Supply a nonempty
 * region inside the source geometry, including old positions that must clear.
 * It describes changes since the last successfully submitted complete image;
 * the SDK carries changes across replaced frames. NULL means the whole image.
 * This is a publication hint, not a packed patch or GPU-upload guarantee. */
typedef struct OWH_META("model=PixelRegion;input;init=owh_pixel_region_init") owh_pixel_region {
    uint32_t x, y, width, height;
} owh_pixel_region;
#define OWH_PIXEL_REGION_INIT {0, 0, 0, 0}
enum { OWH_OVERLAY_PIXEL_BUFFER = 1u, OWH_OVERLAY_SHARED_TEXTURE = 2u };
enum { OWH_QUEUE_FIFO = 1u, OWH_QUEUE_LATEST_READY = 2u };
enum { OWH_PIXEL_BGRA8_UNORM = 1u };
enum {
    OWH_OVERLAY_AWAITING_TEXTURE = 2u,
    OWH_OVERLAY_READY = 3u,
    OWH_OVERLAY_LOST = 4u,
    OWH_OVERLAY_CLOSING = 5u
};
/** Immutable physical-pixel geometry. Recreate to resize or move. */
typedef struct OWH_META("model=OverlaySpec;input;init=owh_overlay_spec_init;size=size")
    owh_overlay_spec {
    uint32_t size, width, height;
    int32_t left, top;
} owh_overlay_spec;
#define OWH_OVERLAY_SPEC_INIT {sizeof(owh_overlay_spec), 0, 0, 0, 0}
/** Optional managed submission settings. Zero selects three slots/latest-ready.
 * LATEST_READY replaces the latest unread image; an in-flight read is retained.
 * FIFO preserves accepted order and submit waits for capacity (optional timeout/
 * cancellation). slot_count includes in-flight images, bounded by capabilities.
 * Direct acquire and explicit try-submit never wait for a free slot. */
typedef struct OWH_META("model=PixelBufferOverlayOptions;input;init=owh_pixel_buffer_overlay_options_init;size=size")
    owh_pixel_buffer_overlay_options {
    uint32_t size, slot_count, policy;
} owh_pixel_buffer_overlay_options;
#define OWH_PIXEL_BUFFER_OVERLAY_OPTIONS_INIT {sizeof(owh_pixel_buffer_overlay_options), 0, 0}
typedef struct OWH_META("model=OverlayInfo;output;size=size") owh_overlay_info_data {
    uint32_t size, state;
    uint64_t id;
    uint32_t kind, width, height, reserved;
    owh_result last_result;
} owh_overlay_info_data;
#define OWH_OVERLAY_INFO_INIT {sizeof(owh_overlay_info_data)}
typedef struct OWH_META("model=PixelBuffer;output;size=size") owh_pixel_buffer {
    uint32_t size, pixel_format;
    void *pixels OWH_META("pointer=borrowed;copy=clear");
    uint64_t pixel_bytes;
    uint32_t width, height, stride, reserved;
} owh_pixel_buffer;
#define OWH_PIXEL_BUFFER_INIT {sizeof(owh_pixel_buffer)}
enum {
    OWH_SUBMISSION_PENDING = 1u,
    OWH_SUBMISSION_PUBLISHED = 2u,
    OWH_SUBMISSION_REJECTED = 4u,
    OWH_SUBMISSION_REPLACED = 5u,
    OWH_SUBMISSION_LOST = 6u
};
typedef struct OWH_META("model=SubmissionInfo;output;size=size") owh_submission_info {
    uint32_t size, state;
    owh_result result;
} owh_submission_info;
#define OWH_SUBMISSION_INFO_INIT {sizeof(owh_submission_info)}
typedef struct OWH_META(
    "model=LegacySharedTexture;input;init=owh_legacy_shared_texture_init;size=size")
    owh_legacy_shared_texture {
    uint32_t size, reserved;
    uint64_t legacy_shared_token;
    uint32_t adapter_luid_low;
    int32_t adapter_luid_high;
} owh_legacy_shared_texture;
#define OWH_LEGACY_SHARED_TEXTURE_INIT {sizeof(owh_legacy_shared_texture)}
#ifdef __cplusplus
}
static_assert(sizeof(owh_overlay_capabilities) == 136);
static_assert(sizeof(owh_overlay_spec) == 20);
static_assert(sizeof(owh_pixel_region) == 16);
static_assert(sizeof(owh_pixel_buffer) == 40);
static_assert(sizeof(owh_submission_info) == 32);
static_assert(sizeof(owh_legacy_shared_texture) == 24);
#endif
#endif
