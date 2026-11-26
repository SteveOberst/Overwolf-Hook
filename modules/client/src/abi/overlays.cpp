#include "handles.hpp"
#include <overwolf_hook/abi/overlays.h>
#include <cstring>
using namespace overwolf_hook;
using namespace overwolf_hook::client::abi;
namespace {
owh_result create(owh_sdk *sdk, const owh_overlay_spec *spec, const owh_pixel_buffer_overlay_options *queue,
                  uint32_t kind, owh_overlay **out) {
    if (out)
        *out = nullptr;
    if (!sdk || !spec || !out)
        return invalid();
    return boundary([&] {
        auto [result, connection] = sdk->value.session();
        if (result.status != OWH_OK)
            return result;
        auto [created, value] = client::Overlay::create(std::move(connection), *spec, queue, kind);
        return adopt(created, std::move(value), out);
    });
}
owh_result submit(owh_overlay *overlay, const void *pixels, uint64_t bytes, uint32_t stride,
                  const owh_pixel_region *damage, const owh_wait_options *wait,
                  owh_submission **out, bool immediate) {
    if (out) *out = nullptr;
    if (!overlay || !pixels || (wait && wait->size != sizeof(*wait))) return invalid();
    return boundary([&] {
        // Allocate optional ownership before replacing any pending image.
        auto handle = out ? std::make_unique<owh_submission>() : nullptr;
        auto [result, frame] = overlay->value->prepare(bytes, stride,
            wait ? wait->timeout_ms : OWH_WAIT_INFINITE,
            wait && wait->cancellation ? wait->cancellation->event.get() : nullptr, immediate, damage);
        if (result.status != OWH_OK) return result;
        const auto view = frame->view();
        const auto source_stride = stride ? stride : view.width * 4;
        for (uint32_t y = 0; y < view.height; ++y)
            std::memcpy(static_cast<std::byte *>(view.pixels) + size_t(y) * view.stride,
                        static_cast<const std::byte *>(pixels) + size_t(y) * source_stride,
                        size_t(view.width) * 4);
        auto [committed, receipt] = frame->commit(out != nullptr);
        if (committed.status == OWH_OK && out) {
            handle->value = std::move(receipt);
            *out = handle.release();
        }
        return committed;
    });
}
} // namespace
extern "C" {
OWH_CLIENT void __cdecl owh_pixel_region_init(owh_pixel_region *out) {
    if (out) *out = OWH_PIXEL_REGION_INIT;
}
OWH_CLIENT void __cdecl owh_overlay_spec_init(owh_overlay_spec *out) {
    if (out)
        *out = OWH_OVERLAY_SPEC_INIT;
}
OWH_CLIENT void __cdecl owh_pixel_buffer_overlay_options_init(owh_pixel_buffer_overlay_options *out) {
    if (out)
        *out = OWH_PIXEL_BUFFER_OVERLAY_OPTIONS_INIT;
}
OWH_CLIENT void __cdecl owh_legacy_shared_texture_init(owh_legacy_shared_texture *out) {
    if (out)
        *out = OWH_LEGACY_SHARED_TEXTURE_INIT;
}
OWH_CLIENT owh_result __cdecl owh_overlay_query_capabilities(owh_sdk *sdk,
                                                             owh_overlay_capabilities *out) {
    if (!sdk || !out || out->size != sizeof(*out))
        return invalid();
    auto [result, connection] = sdk->value.session();
    return result.status == OWH_OK ? connection->overlay_capabilities(*out) : result;
}
OWH_CLIENT owh_result __cdecl owh_overlay_create_pixel_buffer(owh_sdk *sdk, const owh_overlay_spec *spec,
                                                        const owh_pixel_buffer_overlay_options *queue,
                                                        owh_overlay **out) {
    return create(sdk, spec, queue, OWH_OVERLAY_PIXEL_BUFFER, out);
}
OWH_CLIENT owh_result __cdecl
owh_overlay_create_unbound_shared_texture(owh_sdk *sdk, const owh_overlay_spec *spec, owh_overlay **out) {
    return create(sdk, spec, nullptr, OWH_OVERLAY_SHARED_TEXTURE, out);
}
OWH_CLIENT owh_result __cdecl owh_overlay_create_shared_texture(
    owh_sdk *sdk, const owh_overlay_spec *spec, const owh_legacy_shared_texture *texture,
    owh_overlay **out) {
    if (out) *out = nullptr;
    if (!texture || !out || texture->size != sizeof(*texture) || texture->reserved ||
        !texture->legacy_shared_token) return invalid();
    owh_overlay *raw = nullptr;
    const auto created = create(sdk, spec, nullptr, OWH_OVERLAY_SHARED_TEXTURE, &raw);
    std::unique_ptr<owh_overlay> owner(raw);
    if (created.status != OWH_OK) return created;
    const auto bound = owner->value->bind(*texture);
    if (bound.status != OWH_OK) return bound;
    *out = owner.release();
    return ok;
}
OWH_CLIENT owh_result __cdecl owh_overlay_submit_pixels(owh_overlay *overlay, const void *pixels,
    uint64_t bytes, uint32_t stride, const owh_pixel_region *damage,
    const owh_wait_options *wait, owh_submission **receipt) {
    return submit(overlay, pixels, bytes, stride, damage, wait, receipt, false);
}
OWH_CLIENT owh_result __cdecl owh_overlay_try_submit_pixels(owh_overlay *overlay, const void *pixels,
    uint64_t bytes, uint32_t stride, const owh_pixel_region *damage, owh_submission **receipt) {
    return submit(overlay, pixels, bytes, stride, damage, nullptr, receipt, true);
}
OWH_CLIENT owh_result __cdecl owh_frame_prepare_submit(owh_overlay *overlay, uint64_t bytes,
    uint32_t stride, const owh_pixel_region *damage, const owh_wait_options *wait,
    owh_frame **out, owh_pixel_buffer *view) {
    if (out) *out = nullptr;
    if (!overlay || !out || !view || view->size != sizeof(*view) ||
        (wait && wait->size != sizeof(*wait))) return invalid();
    *view = OWH_PIXEL_BUFFER_INIT;
    return boundary([&] {
        auto handle = std::make_unique<owh_frame>();
        auto [result, value] = overlay->value->prepare(bytes, stride,
            wait ? wait->timeout_ms : OWH_WAIT_INFINITE,
            wait && wait->cancellation ? wait->cancellation->event.get() : nullptr, false, damage);
        if (result.status != OWH_OK) return result;
        handle->value = std::move(value);
        *view = handle->value->view();
        *out = handle.release();
        return ok;
    });
}
OWH_CLIENT owh_result __cdecl owh_overlay_query(owh_overlay *overlay, owh_overlay_info_data *out) {
    return overlay && out && out->size == sizeof(*out) ? overlay->value->info(*out) : invalid();
}
OWH_CLIENT owh_result __cdecl owh_overlay_close(owh_overlay *raw) {
    std::unique_ptr<owh_overlay> overlay(raw);
    return overlay ? overlay->value->close() : invalid();
}
OWH_CLIENT owh_result __cdecl owh_overlay_bind_d3d11(owh_overlay *overlay,
                                                     const owh_legacy_shared_texture *texture) {
    return overlay && texture ? overlay->value->bind(*texture) : invalid();
}
OWH_CLIENT owh_result __cdecl owh_frame_acquire(owh_overlay *overlay, owh_frame **out,
                                                owh_pixel_buffer *view) {
    if (out)
        *out = nullptr;
    if (!overlay || !out || !view || view->size != sizeof(*view))
        return invalid();
    *view = {};
    view->size = sizeof(*view);
    return boundary([&] {
        auto [result, value] = overlay->value->acquire();
        if (result.status != OWH_OK)
            return result;
        auto handle = std::make_unique<owh_frame>();
        handle->value = std::move(value);
        *view = handle->value->view();
        *out = handle.release();
        return ok;
    });
}
OWH_CLIENT owh_result __cdecl owh_frame_commit(owh_frame *raw, owh_submission **out) {
    if (out)
        *out = nullptr;
    std::unique_ptr<owh_frame> frame(raw);
    if (!frame)
        return invalid();
    return boundary([&] {
        std::unique_ptr<owh_submission> handle = out ? std::make_unique<owh_submission>() : nullptr;
        auto [result, value] = frame->value->commit(out != nullptr);
        if (result.status != OWH_OK)
            return result;
        if (out) {
            handle->value = std::move(value);
            *out = handle.release();
        }
        return ok;
    });
}
OWH_CLIENT owh_result __cdecl owh_frame_set_damage(owh_frame *frame, const owh_pixel_region *damage) {
    return frame ? frame->value->set_damage(damage) : invalid();
}
OWH_CLIENT void __cdecl owh_frame_discard(owh_frame *frame) { delete frame; }
OWH_CLIENT owh_result __cdecl owh_submission_poll(owh_submission *receipt,
                                                  owh_submission_info *out) {
    return receipt && out && out->size == sizeof(*out) ? receipt->value->poll(*out) : invalid();
}
OWH_CLIENT owh_result __cdecl owh_submission_wait(owh_submission *receipt,
                                                  const owh_wait_options *options,
                                                  owh_submission_info *out) {
    if (!receipt || !out || out->size != sizeof(*out) ||
        (options && options->size != sizeof(*options)))
        return invalid();
    return receipt->value->wait(
        options ? options->timeout_ms : OWH_WAIT_INFINITE,
        options && options->cancellation ? options->cancellation->event.get() : nullptr, *out);
}
OWH_CLIENT void __cdecl owh_submission_close(owh_submission *receipt) { delete receipt; }
}
