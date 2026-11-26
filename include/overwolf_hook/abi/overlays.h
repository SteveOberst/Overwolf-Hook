#ifndef OVERWOLF_HOOK_ABI_OVERLAYS_H
#define OVERWOLF_HOOK_ABI_OVERLAYS_H
#include <overwolf_hook/abi/export.h>
#include <overwolf_hook/abi/sdk.h>
#include <overwolf_hook/types/overlays.h>
#include <overwolf_hook/abi/wait.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Refresh point-in-time source capabilities after Attach. Creates no window,
 * D3D device, texture, or probe. Host loss returns an error without a snapshot.
 */
OWH_CLIENT owh_result __cdecl owh_overlay_query_capabilities(owh_sdk *, owh_overlay_capabilities *);
OWH_CLIENT void __cdecl owh_overlay_spec_init(owh_overlay_spec *);
OWH_CLIENT void __cdecl owh_pixel_region_init(owh_pixel_region *);
OWH_CLIENT void __cdecl owh_pixel_buffer_overlay_options_init(owh_pixel_buffer_overlay_options *);
OWH_CLIENT void __cdecl owh_legacy_shared_texture_init(owh_legacy_shared_texture *);
/** Creates a pixel-buffer source with fixed physical-pixel geometry. NULL queue options
 * select the native default: three slots and LATEST_READY. Output is cleared
 * before validation and receives ownership only on success.
 */
OWH_CLIENT owh_result __cdecl owh_overlay_create_pixel_buffer(owh_sdk *, const owh_overlay_spec *,
                                                        const owh_pixel_buffer_overlay_options *,
                                                        owh_overlay **);
/** Creates and binds an experimental D3D11 source to one completed allocation.
 * Attach must opt in first. Failure closes the new source and clears output.
 */
OWH_CLIENT owh_result __cdecl owh_overlay_create_shared_texture(owh_sdk *, const owh_overlay_spec *,
                                                                const owh_legacy_shared_texture *,
                                                                owh_overlay **);
/** Expert factory: create first, then bind explicitly with owh_overlay_bind_d3d11. */
OWH_CLIENT owh_result __cdecl owh_overlay_create_unbound_shared_texture(owh_sdk *, const owh_overlay_spec *,
                                                                        owh_overlay **);
OWH_CLIENT owh_result __cdecl owh_overlay_query(owh_overlay *, owh_overlay_info_data *);
/** Begins logical close and always consumes the local handle, including when
 * the remote cleanup result is an error. The Agent retains physical cleanup.
 * Open frames may be discarded; their later commit consumes without publish.
 */
OWH_CLIENT owh_result __cdecl owh_overlay_close(owh_overlay *);
/** Copy one complete straight-alpha BGRA8 image into SDK-owned shared memory.
 * Transparent pixels are copied too; this is not a sparse/patch payload.
 * NULL damage selects the whole image. A non-null region must be nonempty and
 * inside the source. It includes every change since the last successful submit,
 * including pixels that became transparent. Validation precedes slot admission.
 * The SDK conservatively combines damage across unread/replaced images.
 * Initial/native fallback publication can still copy the whole image. The hint
 * does not reduce the input copy or promise a smaller GPU upload.
 * Zero stride means width*4; bytes must cover every row. Input can be reused on
 * return. Latest (default) replaces an unread image, never an in-flight read.
 * FIFO waits for space; NULL wait means infinite, finite waits can time out or
 * cancel before admission. OK means accepted, not displayed. No direct frame
 * borrow may be outstanding. Serialize submissions on each overlay.
 * NULL receipt skips optional publication tracking.
 * Receipt allocation failure after latest admission may already have replaced
 * the previous unread image. Close receipts after observing their outcomes.
 * Metadata collection/commit can exceed a short admission timeout.
 */
OWH_CLIENT owh_result __cdecl owh_overlay_submit_pixels(owh_overlay *, const void *pixels,
    uint64_t bytes, uint32_t stride, const owh_pixel_region *damage,
    const owh_wait_options *, owh_submission **receipt);
/** Explicit immediate admission: BUSY leaves the image unsubmitted. Does not
 * replace READY images, regardless of the configured managed submission policy.
 * Does not wait for queue capacity; shared metadata collection can still wait. */
OWH_CLIENT owh_result __cdecl owh_overlay_try_submit_pixels(owh_overlay *, const void *pixels,
    uint64_t bytes, uint32_t stride, const owh_pixel_region *damage, owh_submission **receipt);
/** Low-level managed admission for language bridges/direct renderers. Applies
 * the same input validation and latest/FIFO policy as submit without copying.
 * Fill every row and commit/discard before any other submission on this overlay.
 * timeout=0 uses the configured policy but returns TIMEOUT if admission fails.
 * Latest admission may replace an unread image before commit; discarding this
 * borrow does not restore that image. Metadata waits can exceed the timeout.
 * damage is validated before admission and copied into the returned frame;
 * NULL selects full-image publication. Commit uses that configured region.
 */
OWH_CLIENT owh_result __cdecl owh_frame_prepare_submit(owh_overlay *, uint64_t source_bytes,
    uint32_t source_stride, const owh_pixel_region *damage,
    const owh_wait_options *, owh_frame **, owh_pixel_buffer *);
/** Exclusive writable slot attempt. BUSY means no slot is available.
 * The frame and view are returned together. Fill the complete straight-alpha
 * BGRA8 image using stride; serialize use until commit or discard.
 * Defaults to full-image damage; owh_frame_set_damage can supply an optional hint.
 * Does not wait for queue capacity; metadata collection can wait up to 50 ms
 * plus scheduling time, so this is not a strict nonblocking call.
 */
OWH_CLIENT owh_result __cdecl owh_frame_acquire(owh_overlay *, owh_frame **, owh_pixel_buffer *);
/** Sets an optional publication region on an uncommitted frame. NULL resets
 * full-image damage. Invalid bounds leave the frame and its setting usable.
 * Does not consume the frame or change the complete-image write requirement. */
OWH_CLIENT owh_result __cdecl owh_frame_set_damage(owh_frame *, const owh_pixel_region *);
/** Every valid call consumes the frame, including errors. NULL receipt skips
 * tracking. OK means queued, not displayed. On transport failure after handoff,
 * publication may already have occurred; never retry or discard this frame.
 */
OWH_CLIENT owh_result __cdecl owh_frame_commit(owh_frame *, owh_submission **receipt);
/** Discards an unsubmitted frame. NULL is accepted. */
OWH_CLIENT void __cdecl owh_frame_discard(owh_frame *);
/** Reads the last publication outcome without waiting for a terminal result.
 * Completion collection can wait up to 50 ms for shared metadata, plus
 * scheduling time. Check output.state even when the call returns OK. */
OWH_CLIENT owh_result __cdecl owh_submission_poll(owh_submission *, owh_submission_info *);
/** Waits for a terminal outcome; TIMEOUT/CANCELLED ends this wait only.
 * Metadata collection can exceed a short deadline. */
OWH_CLIENT owh_result __cdecl owh_submission_wait(owh_submission *, const owh_wait_options *,
                                                  owh_submission_info *);
/** Releases the local receipt after active queries/waits have joined. */
OWH_CLIENT void __cdecl owh_submission_close(owh_submission *);
/** Binds one initially completed BGRA8 legacy shared allocation. The Agent
 * validates the actual descriptor against source geometry. No consumer fence
 * or safe later-write point is provided. Keep the producer allocation alive
 * through overlay close. The legacy token is not a COM pointer or NT HANDLE.
 */
OWH_CLIENT owh_result __cdecl owh_overlay_bind_d3d11(owh_overlay *,
                                                     const owh_legacy_shared_texture *);

#ifdef __cplusplus
}
#endif
#endif
