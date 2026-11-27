---
title: "C ABI reference"
description: "Find the public C operations for SDK connections, Overwolf management, overlays, events, game state, waits and cancellation."
---

# C ABI reference

Look up the low-level C operations for SDK connections, Overwolf management,
overlays, events, waits, and game state. Link `OverwolfHook::Client` and include
the header for the domain you use. All domains share the records under
`<overwolf_hook/types/>`. The C++ facade has its own [reference](cpp-api.md).

Read the C ABI version with `owh_sdk_abi_version()`. Read the SDK release
version and build number with `owh_sdk_build_info()` without Agent attach.
Use the matching Client and Agent DLLs from one build.

All handles are opaque. Output handle pointers are cleared before work and
transfer only after successful creation. Initialize option records with their
`owh_*_init` function; passing a null optional input selects native defaults
where the function allows it. Check each `owh_result` before reading an output. The declarations in each domain reference are
extracted from the public headers during the site build; each operation has a
stable heading. Error lists describe expected outcomes, not every possible
underlying Windows failure. Preserve all result fields when reporting a failure.

## Find an operation

Choose the domain that owns the operation:

| Domain | Header |
| --- | --- |
| [SDK connection](c/sdk.md) | `abi/sdk.h` |
| [Overwolf management](c/overwolf.md) | `abi/overwolf.h` |
| [Overlays and submissions](c/overlays.md) | `abi/overlays.h` |
| [Events](c/events.md) | `abi/events.h` |
| [Game state](c/game-state.md) | `abi/game_state.h` |
| [Waits and cancellation](c/wait.md) | `abi/wait.h` |

## Common result and ownership rules

`owh_result` contains `status`, `failure_stage`, `error_domain`, `reserved`,
and `native_code`. See the [status, stage, and flag catalog](constants.md#values-and-meanings)
and [record layouts](data-records.md#native-record-layouts) for their values and fields.
`owh_result.status == OWH_OK` means the operation succeeded; `OWH_NO_DATA`,
`OWH_BUSY`, `OWH_TIMEOUT`, and `OWH_CANCELLED` have operation-specific meanings.

`owh_sdk_close` is void. Explicit resource close functions return a result and
consume the local handle even if remote cleanup fails: overlay and subscription.
Local frame, receipt, cancel token, source list, and state
snapshot close functions are void. Raw C callers must serialize ordinary calls
against close and stop using borrowed pointers before their owner is released.
Language wrappers provide local ownership checks. Callers serialize each
resource's calls and close; only token signaling may run beside a wait.


## Operation index

Find an exported function by name:

### Overwolf management

- <a id="owh_overwolf_installation_query"></a>[`owh_overwolf_installation_query`](c/overwolf.md#owh_overwolf_installation_query)
- <a id="owh_overwolf_process_query"></a>[`owh_overwolf_process_query`](c/overwolf.md#owh_overwolf_process_query)
- <a id="owh_overwolf_start"></a>[`owh_overwolf_start`](c/overwolf.md#owh_overwolf_start)
- <a id="owh_overwolf_stop"></a>[`owh_overwolf_stop`](c/overwolf.md#owh_overwolf_stop)

### SDK connection

- <a id="owh_sdk_open"></a>[`owh_sdk_open`](c/sdk.md#owh_sdk_open)
- <a id="owh_sdk_close"></a>[`owh_sdk_close`](c/sdk.md#owh_sdk_close)
- <a id="owh_sdk_attach"></a>[`owh_sdk_attach`](c/sdk.md#owh_sdk_attach)
- <a id="owh_sdk_detach"></a>[`owh_sdk_detach`](c/sdk.md#owh_sdk_detach)
- <a id="owh_sdk_check"></a>[`owh_sdk_check`](c/sdk.md#owh_sdk_check)
- <a id="owh_sdk_probe_agent"></a>[`owh_sdk_probe_agent`](c/sdk.md#owh_sdk_probe_agent)
- <a id="owh_sdk_agent_info"></a>[`owh_sdk_agent_info`](c/sdk.md#owh_sdk_agent_info)
- <a id="owh_sdk_ping"></a>[`owh_sdk_ping`](c/sdk.md#owh_sdk_ping)
- <a id="owh_sdk_abi_version"></a>[`owh_sdk_abi_version`](c/sdk.md#owh_sdk_abi_version)
- <a id="owh_sdk_build_info"></a>[`owh_sdk_build_info`](c/sdk.md#owh_sdk_build_info)
- <a id="owh_client_options_init"></a>[`owh_client_options_init`](c/sdk.md#owh_client_options_init)

### Waits and cancellation

- <a id="owh_wait_options_init"></a>[`owh_wait_options_init`](c/wait.md#owh_wait_options_init)
- <a id="owh_cancel_create"></a>[`owh_cancel_create`](c/wait.md#owh_cancel_create)
- <a id="owh_cancel_signal"></a>[`owh_cancel_signal`](c/wait.md#owh_cancel_signal)
- <a id="owh_cancel_close"></a>[`owh_cancel_close`](c/wait.md#owh_cancel_close)

### Overlays and submissions

- <a id="owh_overlay_query_capabilities"></a>[`owh_overlay_query_capabilities`](c/overlays.md#owh_overlay_query_capabilities)
- <a id="owh_overlay_spec_init"></a>[`owh_overlay_spec_init`](c/overlays.md#owh_overlay_spec_init)
- <a id="owh_pixel_buffer_overlay_options_init"></a>[`owh_pixel_buffer_overlay_options_init`](c/overlays.md#owh_pixel_buffer_overlay_options_init)
- <a id="owh_legacy_shared_texture_init"></a>[`owh_legacy_shared_texture_init`](c/overlays.md#owh_legacy_shared_texture_init)
- <a id="owh_overlay_create_pixel_buffer"></a>[`owh_overlay_create_pixel_buffer`](c/overlays.md#owh_overlay_create_pixel_buffer)
- <a id="owh_overlay_create_shared_texture"></a>[`owh_overlay_create_shared_texture`](c/overlays.md#owh_overlay_create_shared_texture)
- <a id="owh_overlay_create_unbound_shared_texture"></a>[`owh_overlay_create_unbound_shared_texture`](c/overlays.md#owh_overlay_create_unbound_shared_texture)
- <a id="owh_overlay_submit_pixels"></a>[`owh_overlay_submit_pixels`](c/overlays.md#owh_overlay_submit_pixels)
- <a id="owh_pixel_region_init"></a>[`owh_pixel_region_init`](c/overlays.md#owh_pixel_region_init)
- <a id="owh_frame_set_damage"></a>[`owh_frame_set_damage`](c/overlays.md#owh_frame_set_damage)
- <a id="owh_overlay_try_submit_pixels"></a>[`owh_overlay_try_submit_pixels`](c/overlays.md#owh_overlay_try_submit_pixels)
- <a id="owh_frame_prepare_submit"></a>[`owh_frame_prepare_submit`](c/overlays.md#owh_frame_prepare_submit)
- <a id="owh_overlay_query"></a>[`owh_overlay_query`](c/overlays.md#owh_overlay_query)
- <a id="owh_overlay_close"></a>[`owh_overlay_close`](c/overlays.md#owh_overlay_close)
- <a id="owh_overlay_bind_d3d11"></a>[`owh_overlay_bind_d3d11`](c/overlays.md#owh_overlay_bind_d3d11)
- <a id="owh_frame_acquire"></a>[`owh_frame_acquire`](c/overlays.md#owh_frame_acquire)
- <a id="owh_frame_commit"></a>[`owh_frame_commit`](c/overlays.md#owh_frame_commit)
- <a id="owh_frame_discard"></a>[`owh_frame_discard`](c/overlays.md#owh_frame_discard)
- <a id="owh_submission_poll"></a>[`owh_submission_poll`](c/overlays.md#owh_submission_poll)
- <a id="owh_submission_wait"></a>[`owh_submission_wait`](c/overlays.md#owh_submission_wait)
- <a id="owh_submission_close"></a>[`owh_submission_close`](c/overlays.md#owh_submission_close)

<a id="event-api"></a>
### Events

- <a id="owh_event_options_init"></a>[`owh_event_options_init`](c/events.md#owh_event_options_init)
- <a id="owh_event_query_capabilities"></a>[`owh_event_query_capabilities`](c/events.md#owh_event_query_capabilities)
- <a id="owh_event_subscribe"></a>[`owh_event_subscribe`](c/events.md#owh_event_subscribe)
- <a id="owh_subscription_query"></a>[`owh_subscription_query`](c/events.md#owh_subscription_query)
- <a id="owh_subscription_poll"></a>[`owh_subscription_poll`](c/events.md#owh_subscription_poll)
- <a id="owh_subscription_receive"></a>[`owh_subscription_receive`](c/events.md#owh_subscription_receive)
- <a id="owh_subscription_close"></a>[`owh_subscription_close`](c/events.md#owh_subscription_close)

### Game state

- <a id="owh_game_state_sources"></a>[`owh_game_state_sources`](c/game-state.md#owh_game_state_sources)
- <a id="owh_source_list_count"></a>[`owh_source_list_count`](c/game-state.md#owh_source_list_count)
- <a id="owh_source_list_at"></a>[`owh_source_list_at`](c/game-state.md#owh_source_list_at)
- <a id="owh_source_list_close"></a>[`owh_source_list_close`](c/game-state.md#owh_source_list_close)
- <a id="owh_game_state_read"></a>[`owh_game_state_read`](c/game-state.md#owh_game_state_read)
- <a id="owh_game_state_read_from"></a>[`owh_game_state_read_from`](c/game-state.md#owh_game_state_read_from)
- <a id="owh_snapshot_count"></a>[`owh_snapshot_count`](c/game-state.md#owh_snapshot_count)
- <a id="owh_snapshot_at"></a>[`owh_snapshot_at`](c/game-state.md#owh_snapshot_at)
- <a id="owh_snapshot_close"></a>[`owh_snapshot_close`](c/game-state.md#owh_snapshot_close)
