---
title: "Overlays and submissions: C ABI reference"
description: "Look up C overlay creation, pixel submissions, frame access, shared-texture binding, publication status and resource cleanup."
---

# Overlays and submissions: C ABI

Look up the operations declared in `<overwolf_hook/abi/overlays.h>`.
See the [C ABI index](../native-api.md) for linking and shared result/ownership
rules, or [data records](../data-records.md) for field meanings.

For usage, see [your first overlay](../../overlays/first-overlay.md) and [repeated pixel-buffer submissions](../../overlays/pixel-buffers.md).

## `owh_overlay_query_capabilities`

Reads current overlay source capabilities:

```c
--8<-- "docs/assets/generated/reference/owh_overlay_query_capabilities.h"
```

Requires an attachment; initialize `output.size`. Outer `OK` means the snapshot was returned; inspect `pixel_buffer_status`, `shared_texture_status`, `available` and `enabled`. Host loss returns an error instead of a snapshot.

## `owh_overlay_spec_init`

Initializes physical-pixel geometry:

```c
--8<-- "docs/assets/generated/reference/owh_overlay_spec_init.h"
```

Returns nothing. Supply writable storage for the corresponding input record; null is a no-op. Writes the native size/defaults. See [data records](../data-records.md#data-records).

## `owh_pixel_region_init`

Initializes an optional damage rectangle to zero:

```c
--8<-- "docs/assets/generated/reference/owh_pixel_region_init.h"
```

Returns nothing; null is a no-op. Set nonempty `width` and `height` and in-bounds
`x` and `y` before supplying the record. A zero rectangle is invalid input;
pass null to a submission instead to select the whole image.

## `owh_pixel_buffer_overlay_options_init`

Initializes optional queue settings; zero values keep native defaults:

```c
--8<-- "docs/assets/generated/reference/owh_pixel_buffer_overlay_options_init.h"
```

Returns nothing. Supply writable storage for the corresponding input record; null is a no-op. Writes the native size/defaults. See [data records](../data-records.md#data-records).

## `owh_legacy_shared_texture_init`

Initializes D3D11 binding input:

```c
--8<-- "docs/assets/generated/reference/owh_legacy_shared_texture_init.h"
```

Returns nothing. Supply writable storage for the corresponding input record; null is a no-op. Writes the native size/defaults. See [data records](../data-records.md#data-records).

## `owh_overlay_create_pixel_buffer`

Allocates shared pixel-buffer slots and registers a source; null queue selects three `LATEST_READY` slots:

```c
--8<-- "docs/assets/generated/reference/owh_overlay_create_pixel_buffer.h"
```

Initialize `spec` and any queue override. Supply a writable handle output; null queue selects defaults. Returns `OK` with an owned source, or argument, readiness, resource-budget, registration or transport failure.

## `owh_overlay_create_shared_texture`

Creates and binds an experimental source to a completed shared texture:

```c
--8<-- "docs/assets/generated/reference/owh_overlay_create_shared_texture.h"
```

Initialize `spec` and the texture descriptor. Returns `OK` with an owned bound source; `OPT_IN_REQUIRED` without attach-time opt-in. A failed bind closes the newly created source and leaves the output null. Success confirms acceptance, not display.

## `owh_overlay_create_unbound_shared_texture`

Creates a source without a texture; the source starts in `AWAITING_TEXTURE`:

```c
--8<-- "docs/assets/generated/reference/owh_overlay_create_unbound_shared_texture.h"
```

Initialize `spec`. On success, own the returned source and bind it with
`owh_overlay_bind_d3d11` before use. Prefer the combined factory when the texture
already exists. Close the unbound source if you abandon it.

## `owh_overlay_submit_pixels`

Submits a full image. The SDK copies the input before returning and handles
slot ownership and publication retries:

```c
--8<-- "docs/assets/generated/reference/owh_overlay_submit_pixels.h"
```

Supply a complete straight-alpha BGRA8 image matching the source geometry.
Zero stride means tightly packed rows. The byte count must cover all rows.
Null `damage` selects full-image publication. A non-null `owh_pixel_region`
must be nonempty, in bounds, and cover every change since the last successful
submission, including cleared pixels. The SDK copies the complete input and
combines damage across replaced images; the hint can reduce Overwolf's retained
image copy. It does not promise a smaller GPU upload. See
[damage rectangles](../../overlays/pixel-buffers.md#limit-overwolfs-copy-to-changed-pixels).
Default latest-image delivery replaces an unread image; an in-flight read stays
protected. FIFO waits for capacity using the optional wait options (null means
infinite). Returns `TIMEOUT` or `CANCELLED` if admission cannot finish in that
budget, or argument/source/transport failure. `OK` means accepted, not displayed.
Input is reusable on return. An optional receipt reports later publication.
Serialize submissions; finish direct borrows first. See [delivery policies](../../overlays/pixel-buffers.md#choose-how-pending-images-are-handled).

Keep open receipt handles below the capability limit. If receipt allocation
fails after latest-image admission, the prior unread image may already have
been replaced even though this call fails. See [publication tracking](../../overlays/pixel-buffers.md#track-publication-when-needed).

## `owh_overlay_try_submit_pixels`

Attempts free-slot admission without waiting for queue capacity:

```c
--8<-- "docs/assets/generated/reference/owh_overlay_try_submit_pixels.h"
```

Uses the same complete-image and optional-damage input contract. Does not replace a READY image;
`BUSY` leaves the image unsubmitted. Other failures remain errors. On `OK`, input
is reusable and any requested receipt is owned by the caller.
Shared metadata access can still wait briefly; see
[timeout effects](../contracts.md#timeouts-and-session-loss).

## `owh_frame_prepare_submit`

Prepares managed admission for language bridges and direct renderers:

```c
--8<-- "docs/assets/generated/reference/owh_frame_prepare_submit.h"
```

Validates source byte count/stride and optional damage before claiming a slot using latest/FIFO
policy. Initialize `view.size` and supply a frame output. On `OK`, fill every row
in the returned borrow, end access, and commit or discard. Wait options govern
admission; zero timeout still uses the configured policy but fails with `TIMEOUT`
if no admission is possible. No copy happens here. Ordinary C callers use
`owh_overlay_submit_pixels` instead.
Latest-image admission may replace a pending image before commit. Discarding
the returned frame does not restore the replaced image.
The returned frame stores the supplied damage; null selects the whole image.

## `owh_overlay_query`

Reads source state and last result:

```c
--8<-- "docs/assets/generated/reference/owh_overlay_query.h"
```

Requires a valid source; initialize `output.size`. Returns `OK` with source state and `last_result`, or a source/session/transport error. A `READY` source is not a display acknowledgment.

## `owh_overlay_close`

Begins close and consumes local ownership even when cleanup reports an error:

```c
--8<-- "docs/assets/generated/reference/owh_overlay_close.h"
```

Requires a valid source handle. Returns the cleanup result and consumes local ownership on every returned result, including remote failure. Do not retry that handle.

## `owh_overlay_bind_d3d11`

Imports/validates one completed shared texture and registers its native source after explicit opt-in:

```c
--8<-- "docs/assets/generated/reference/owh_overlay_bind_d3d11.h"
```

Initialize the texture input and supply its real token/LUID. Returns `OK` on source acceptance; `UNSUPPORTED` for a non-D3D11 source, `ALREADY_EXISTS` for a successful prior binding, or an opt-in/import/descriptor/registration error. See [raw binding lifetime](../contracts.md#experimental-d3d11-shared-texture).

## `owh_frame_acquire`

Attempts an exclusive writable pixel-slot borrow; `BUSY` means no slot is available:

```c
--8<-- "docs/assets/generated/reference/owh_frame_acquire.h"
```

Supply a handle output and initialize `view.size`. Returns `OK` with a writable borrow and view, `BUSY` if no slot is free, or source/session/argument failure. Pixel access ends before commit/discard.
The call does not wait for a free slot, but completion collection can wait on
shared metadata. See [timeout effects](../contracts.md#timeouts-and-session-loss).

## `owh_frame_set_damage`

Sets an optional changed area on an uncommitted frame:

```c
--8<-- "docs/assets/generated/reference/owh_frame_set_damage.h"
```

Requires a valid writable frame. Null `damage` resets full-image publication.
Returns `OK` after copying the region, or `INVALID_ARGUMENT` for empty or
out-of-bounds geometry. An invalid region leaves the frame and its previous
setting usable. Does not consume the frame. Fill the complete image regardless
of the hint, including transparent pixels. Direct acquire defaults to the whole
image. See the [damage contract](../../overlays/pixel-buffers.md#limit-overwolfs-copy-to-changed-pixels).

## `owh_frame_commit`

Consumes a valid frame on every outcome; null receipt skips tracking. `OK` means queued, not displayed:

```c
--8<-- "docs/assets/generated/reference/owh_frame_commit.h"
```

Requires a valid acquired frame; optional receipt output may be null. Every returned result consumes the frame. `OK` means queued; receipt/resource/session/wake failures are possible. Never retry or discard the consumed frame, even if handoff may already have happened.

## `owh_frame_discard`

Releases an unsubmitted frame; null is accepted:

```c
--8<-- "docs/assets/generated/reference/owh_frame_discard.h"
```

Returns nothing. Null is accepted. Consumes an unsubmitted frame and ends its writable borrow without publication.

## `owh_submission_poll`

Reads the publication outcome without waiting for publication to finish:

```c
--8<-- "docs/assets/generated/reference/owh_submission_poll.h"
```

Requires a receipt; initialize `output.size`. Returns `OK` with the current outcome, including `PENDING`. Read `output.result` separately; it is the publication result.
Completion collection can wait up to 50 ms for the metadata lock, plus scheduling
time. This is not a strict nonblocking operation.

## `owh_submission_wait`

Waits for outcome, timeout, or cancellation:

```c
--8<-- "docs/assets/generated/reference/owh_submission_wait.h"
```

Requires a receipt; initialize `output.size` and any wait options. Null options wait indefinitely. Returns `OK` for a final outcome, `TIMEOUT` or `CANCELLED` when only this wait ends, or an argument/native wait error. Read `output` only on `OK` and its nested publication result separately.
Metadata collection can exceed a short requested deadline. A timeout here does
not close the Agent session; see [timeout effects](../contracts.md#timeouts-and-session-loss).

## `owh_submission_close`

Releases the local receipt:

```c
--8<-- "docs/assets/generated/reference/owh_submission_close.h"
```

Returns nothing. Null is accepted. Join active queries/waits before consuming the receipt; close does not cancel publication.

`owh_overlay_capabilities` reports current pixel-buffer/shared-texture readiness, enabled
feature flags, identity, and the accepted allocation limits. `available` reports
readiness independently of opt-in; `enabled` also accounts for your experimental
feature flags. This read-only query does not create an overlay or GPU device.
Its result can change after the query and does not guarantee display.

Pixel records are complete straight-alpha BGRA8 images. The queue shares the
same slot allocation between Client and Agent; it does not serialize a second
full image. The D3D11 path has no consumer fence or safe later-write point.
