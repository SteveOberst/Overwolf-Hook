---
title: "C++ API reference"
description: "Look up C++ controllers, move-only resources, errors and ownership rules for the header-only facade over the Overwolf Hook C ABI."
---

# C++ API

Use this reference to find C++ controllers, move-only resources, and error
conversions. The C++20 header-only facade adapts the C ABI. Include
`<overwolf_hook/overwolf_hook.hpp>` and link `OverwolfHook::Cpp`.
The C application binary interface (ABI) is the shared native entry point;
the facade adds C++ controllers and resource ownership around it.
The API groups features into controllers; local Overwolf control can also be used
independently of an SDK context. Native data
records remain the C types in `types/*.h`.

`OverwolfHook::build_info()` returns the loaded Client's SDK
version/build number without opening or attaching a context. The C ABI version
is available separately through `OverwolfHook::abi_version()`.

For a complete application, see [your first overlay](../overlays/first-overlay.md).
For repeated image updates, see [pixel-buffer submission](../overlays/pixel-buffers.md).

## Quick lookup

All value types remain the canonical C records. Use the class sections for
C++ ownership and optional/error conversions; use the [C ABI](native-api.md)
for the complete underlying function contract.

| Class | Operations |
| --- | --- |
| [OverwolfHook](#sdk-context) | Open, attach/detach, check/probe, identity, ping, controller access, close |
| [OverwolfController](#overwolf-installation-and-process-control) | Installation, process, start, stop |
| [OverlayController](#overlays) | Capabilities, source creation |
| [PixelBufferOverlay / PixelFrame](#pixelbufferoverlay-and-pixelframe) | Submit images, optional direct frames, info, close |
| [SharedTextureOverlay](#sharedtextureoverlay) | Bind a completed allocation, info, close |
| [Submission / WaitCancellation](#submission-and-waitcancellation) | Poll/wait, signal, close |
| [EventController / EventSubscription](#events) | Capabilities, subscribe, info, poll/receive, close |
| [GameStateController](#game-state) | Discover, storage descriptor, direct reads |
| [SourceList / StateSnapshot / StateFieldView](#state-views-and-storage) | Count/at/fields/native, retained snapshot ownership |
| [NativeError / cleanup](#errors-and-resource-helpers) | Inspect result, explicit/nonthrowing cleanup |

## SDK context

`OverwolfHook::open()` creates an independent context. `attach(options = nullptr)`
uses native defaults; `detach()` closes the current attachment.
`check_client(const owh_client_options *options = nullptr)` returns a
point-in-time diagnostic report and
`probe_agent(const owh_client_options *options = nullptr)` reads a resident
Agent without attaching or starting it. These are instance methods on the C++
facade, but use no attachment state; the corresponding C calls take no SDK context.
`agent_info()` and `ping(uint32_t timeout_ms = 1000)` require an attachment;
`ping()` uses a one-second timeout, or you can pass a custom deadline in
milliseconds. `close()` is nonthrowing;
explicitly close resources when you need to observe their `NativeError` result.
Destructors do not retain cleanup failures.

`agent_info()` returns cached attachment identity after a local lifetime check;
it does not send a request. `ping()` does send a request, and an in-flight reply
timeout closes the session and retires its resources. See
[timeout effects](contracts.md#timeouts-and-session-loss).

Default `attach()` can close and relaunch Overwolf. Detach does not stop it.
The [attach contract](contracts.md#attach-modes) owns selectors, startup failures
and feature-readiness rules. Advanced HOT mode uses a running process and
accepts a setup race that can crash Overwolf. Set it through initialized options:

```cpp
--8<-- "docs/assets/examples/cpp/hot_attach.cpp:attach"
```

`overlays()` and `events()` return lightweight controllers bound to this
context. `game_state()` is a passive convenience accessor; a standalone
`GameStateController{}` works without an SDK context. Resources already
created remain bound to their original native handle.

## Overwolf installation and process control

`OverwolfController{}` is usable without `OverwolfHook::open()`;
`sdk.overwolf()` returns the same stateless controller.

| Method | Returns / effect |
| --- | --- |
| `installation()` | `std::optional<owh_overwolf_installation>` with directory, launcher and installed version. |
| `running_process(uint32_t pid = 0, uint64_t started = 0)` | `std::optional<owh_overwolf_process>`; zero PID discovers a unique main process, nonzero creation time revalidates its identity. |
| `start(uint32_t timeout_ms = 10000)` | `owh_overwolf_process`; returns the running main process or starts Overwolf and waits for it. |
| `stop(const owh_overwolf_process &, uint32_t timeout_ms = 10000)` | Requests normal exit of that identity and waits; never force-kills. |

Absence becomes `std::nullopt` only for the two queries; other failures throw
`NativeError`. Start/stop require a finite positive timeout. The controller owns
no session and needs no close. See [managing Overwolf](../overwolf.md) for
examples, version fields and the difference between launch and attachment.

## Overlays

`OverlayController::capabilities()` returns `owh_overlay_capabilities`, a current readiness
snapshot independent of the immutable attachment limits. Creation rechecks
readiness; a prior failed query does not require reattaching.
`create_pixel_buffer(spec, queue = nullptr)` creates a pixel-buffer overlay;
`create_shared_texture(spec, texture)` creates and binds a completed experimental D3D11 texture.
Use `create_unbound_shared_texture(spec)` only when binding must happen later.
All three factories take canonical `owh_overlay_spec`; use the native init function to get
defaults.

### PixelBufferOverlay and PixelFrame

`PixelBufferOverlay::submit(image, stride = 0, timeout_ms = OWH_WAIT_INFINITE,
cancellation = nullptr, damage = nullptr)` copies a complete BGRA8 `std::span<const std::byte>` into
shared memory. Zero stride means tight rows. Default latest-image delivery
replaces an unread image; FIFO waits for capacity with optional timeout and
cancellation. Input is reusable on return. `submit_tracked` accepts the same
arguments and returns a `Submission`; `try_submit(image, stride = 0)` explicitly
uses only free capacity and returns false when busy. See [delivery policies](../overlays/pixel-buffers.md#choose-how-pending-images-are-handled).

The remaining frame methods are optional direct-write access. Finish all borrows
before managed submission. Serialize each overlay's calls.

`submit(image, region[, stride, timeout_ms, cancellation])` and `submit_tracked`
accept an `owh_pixel_region` by reference. `try_submit(image, region[, stride])`
also accepts it. The trailing pointer form accepts null for full-image
publication. These hints still require a complete image; see
[damage rectangles](../overlays/pixel-buffers.md#limit-overwolfs-copy-to-changed-pixels).
`PixelFrame::set_damage(region)` sets the same hint before commit; its pointer
overload defaults to null to reset it. Invalid bounds throw `NativeError` without
consuming the frame or changing its previous hint.

`PixelBufferOverlay::try_acquire()` does not wait for slot capacity and returns an optional
move-only `PixelFrame`. `pixels()` returns a plain `std::span<std::byte>` borrow
that the caller must stop using before commit or discard. `layout()` returns
the native pixel layout without the pointer. A frame commit consumes the frame
even on error; `commit_tracked()` returns a `Submission` for observing the outcome.
Successful commit means queued; `PUBLISHED` means the native publish call succeeded.
Overlay `info()` returns `owh_overlay_info_data`.

Acquire, try-submit, and receipt poll can still wait for shared metadata.
Tracked submission also has a receipt limit; a failed latest-image submission
may already have replaced a pending image. See
[publication tracking](../overlays/pixel-buffers.md#track-publication-when-needed)
and [timeout effects](contracts.md#timeouts-and-session-loss).

### SharedTextureOverlay

See the [binding and allocation lifetime](../overlays/shared-textures.md#close-the-source)
for producer ownership and close-error handling.

`SharedTextureOverlay::bind(owh_legacy_shared_texture)` binds a completed
legacy allocation after expert unbound creation. The normal factory binds for you.
Unbound creation leaves the source in `AWAITING_TEXTURE`; bind
imports the allocation and registers the native source. Only one binding is
accepted per source. Keep the allocation alive and unchanged through close. This path has no
consumer fence or later-write guarantee.

<a id="submission-and-waitcancellation"></a>
## Submissions and cancellation


`Submission::poll()` and `wait()` return `owh_submission_info`; its nested
`result` is the publication outcome. Both are caller-driven; the SDK creates
no worker thread per receipt. See [tracked publication](../overlays/pixel-buffers.md#track-publication-when-needed)
for the distinction between queueing, publication and display.

`WaitCancellation::create()` creates a one-shot token. `signal()` wakes its
borrowing waits; cancellation is a `NativeError` with `OWH_CANCELLED` in C++.
Join all waits before `close()`. `Submission::wait(timeout_ms, cancellation)`
also throws for a wait timeout; its `owh_submission_info.result` describes the
separate publication outcome when a record is returned.


## Events

`EventController::capabilities()` returns `owh_event_capabilities`; its `status`
reports current event-source readiness without installing hooks.
`subscribe(options = nullptr)` returns an independent `EventSubscription`.

### EventSubscription

`EventSubscription::poll()` and `receive(timeout_ms = OWH_WAIT_INFINITE,
cancellation = nullptr)` consume the next matching event from the live stream.
The `receive(const owh_wait_options *)` overload accepts the raw wait record;
a null pointer waits indefinitely without cancellation. The timeout overload accepts an optional shared
`WaitCancellation`; signal the token and join waiters before token close.
`close()` consumes local ownership if native cleanup reports a failure.
Serialize calls and close on each resource; only token signal may run alongside
a wait. `EventSubscription::info()` returns `owh_subscription_info`, including
state, hook status, warnings, last result and the QPC/UTC clock anchor.
`WaitCancellation` is declared in `wait.hpp`.

## Game state

These types discover existing provider channels and read owned snapshots without
an Agent attachment.

### GameStateController

`GameStateController::sources()` returns a `SourceList` snapshot.
`storage(channel)` returns a passive descriptor; `read(channel, timeout_ms = OWH_STATE_DEFAULT_TIMEOUT_MS)`
returns a `StateSnapshot` without attaching to the Agent. `StateSnapshot::at()`
and `fields()` return `StateFieldView` objects containing the canonical
`owh_state_field`; each field view retains the snapshot owner so its byte
pointers stay valid.

`storage(const owh_source_channel&)` and `read(const owh_source_channel&,
timeout_ms = OWH_STATE_DEFAULT_TIMEOUT_MS)` preserve a discovered producer
selection. `storage(channel, producer_process_id = 0)` selects explicitly;
zero uses channel-only selection. A channel with multiple current producers
requires an explicit producer PID.

State reads without an explicit timeout use `OWH_STATE_DEFAULT_TIMEOUT_MS`
(100 ms) in C++, Java and Kotlin. Pass `0` explicitly for a nonblocking lock
attempt. The deadline applies to acquiring the provider's state mutex; it does
not wait for a newer game update.

### State views and storage

The native state-read deadline must be 0 through 1000 ms. `GameStateStorage`
owns a channel number and optional producer PID; `source_channel()` and
`producer_process_id()` return those selectors, and
`read(timeout_ms)` performs a fresh copy. `SourceList::count()` / `at(index)`
enumerate discovery results. `StateSnapshot::count()` / `at(index)` / `fields()`
enumerate copied fields; `StateFieldView::native()` returns their canonical
record. `SourceList::close()` and `StateSnapshot::close()` release their own
snapshot reference. Retained field views keep state bytes alive independently.

## Errors and resource helpers

`NativeError::result()` exposes the complete `owh_result` for a failed call;
`overwolf_hook::check(result)` throws it for every status other than `OWH_OK`.
Quiet event receives and busy pixel acquisition are converted to empty
`std::optional` by those specific methods, not by `check`.

`PixelFrame::close()` discards an uncommitted frame. `PixelBufferOverlay::close()`
and `SharedTextureOverlay::close()` report cleanup errors;
`close_noexcept()` explicitly discards them. Both overlay types expose
`info()`. `Submission::close()` releases only the receipt. All these owners
also clean up through their destructors.

All owning C++ resources are move-only. Destructors are nonthrowing; explicit
close reports operation errors and consumes the local handle according to the
ABI contract. See [native API](native-api.md) and [shared contracts](contracts.md).
