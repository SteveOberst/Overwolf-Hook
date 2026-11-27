---
title: "Native and JVM data records"
description: "Look up fields, defaults, native layouts and generated Java/Kotlin views for the records shared across the SDK's language APIs."
---

# Data records

Look up the inputs and outputs shared by the C ABI, C++ facade, and generated
Java/Kotlin models. Field meanings and defaults are summarized first; exact C
declarations follow. For operations, see the [C ABI index](native-api.md).

<a id="data-records"></a>
## Fields and defaults

These are the public records shared by the C ABI, C++ and generated JVM
models. The tables use native `snake_case` field names. For example,
`process_started` becomes Java `getProcessStarted()` and Kotlin
`processStarted`; an input field such as `source_channel` uses
`setSourceChannel(...)`. Raw C/C++ callers use the input initializers and set
`output.size = sizeof(output)` for outputs whose declarations require a size.
The C++ facade and generated JVM helpers handle those initialization steps.
Leave `reserved` fields at zero. Input models provide setters; output models
provide getters and own their copied data on the JVM.

### Inputs

| C record / JVM model | Fields and defaults |
| --- | --- |
| [`owh_client_options`](#owh_client_options) / `ClientOptions` | `process_id`, `process_started`: optional main-process selector, zero for discovery/observed creation time. `attach_mode`: restart by default. `experimental_features`: none by default. `pixel_buffer_budget_bytes`, `shared_texture_budget_bytes`, `maximum_overlays`, `maximum_receipts_per_overlay`: zero selects SDK limits; accepted limits are reported by overlay capabilities. |
| [`owh_overlay_spec`](#owh_overlay_spec) / `OverlaySpec` | Positive `width`, `height` and signed `left`, `top` in physical pixels. Defaults are zero; set dimensions before creating. Geometry is fixed for the source lifetime. |
| [`owh_pixel_region`](#owh_pixel_region) / `PixelRegion` | Optional damage in image-local physical pixels: `x`, `y`, `width`, `height`. Extents must be nonzero and in bounds. Covers every change since the last successful submission, including cleared pixels. Omit the record for full-image publication; the zero initializer is not a valid hint. Input remains a complete image. |
| [`owh_pixel_buffer_overlay_options`](#owh_pixel_buffer_overlay_options) / `PixelBufferOverlayOptions` | `slot_count`, `policy`: zero selects three slots and `OWH_QUEUE_LATEST_READY`. `OWH_QUEUE_FIFO` is the alternative ordering policy. Current capability limits bound the slot count. |
| [`owh_legacy_shared_texture`](#owh_legacy_shared_texture) / `LegacySharedTexture` | `legacy_shared_token`, `adapter_luid_low`, `adapter_luid_high`: one completed legacy D3D11 allocation on the identified adapter. Defaults are zero; supply the actual token/LUID before bind. |
| [`owh_event_options`](#owh_event_options) / `EventOptions` | `source_channel`: `OWH_EVENTS_ALL_CHANNELS` (zero) by default; otherwise filters one source channel, not a game ID. `producer_process_id`: zero accepts all producers; otherwise filters the PID reported by Overwolf. Both filters apply together. |
| [`owh_wait_options`](#owh_wait_options) / `WaitOptions` | `timeout_ms`: `OWH_WAIT_INFINITE` by default. `cancellation`: null by default, or a borrowed one-shot token. Used by event receives, FIFO admission and publication waits; state/start/stop use their own timeout rules. |

### Outputs

| C record / JVM model | What it contains |
| --- | --- |
| [`owh_result`](#owh_result) / `Result` | `status`, `failure_stage`, `error_domain`, `native_code`; preserve these when reporting an operation failure. |
| [`owh_build_info`](#owh_build_info) / `BuildInfo` | `sdk_version`, `build_number`: the loaded component's compatibility label. |
| [`owh_agent_info`](#owh_agent_info) / `AgentInfo` | `abi_version`, `wire_version`, `server_state`, `process_id`, `process_started`, `instance_id`, `build_info`: resident Agent identity and startup state. |
| [`owh_client_sanity_report`](#owh_client_sanity_report) / `ClientSanityReport` | `checked`, `passed`, `reviewed_build`, process identity, `agent_sha256`, and `host_status`, `module_status`, `profile_status`, `hash_status`. See [preflight interpretation](contracts.md#interpret-preflight-and-readiness). |
| [`owh_overwolf_installation`](#owh_overwolf_installation) / `OverwolfInstallation` | UTF-8 `directory`, `launcher`, `version` from installation discovery. |
| [`owh_overwolf_process`](#owh_overwolf_process) / `OverwolfProcess` | `process_id`, `process_started`, UTF-8 `executable`, `version`; a non-owning point-in-time process identity. |
| [`owh_overlay_capabilities`](#owh_overlay_capabilities) / `OverlayCapabilities` | `available`, `enabled`, process/session identity, `maximum_overlays`, width/height and slot limits, `maximum_pixel_buffer_bytes`, pixel-buffer and shared-texture budgets, receipt limit, `pixel_buffer_status`, `shared_texture_status`. Source readiness, not a display test. |
| [`owh_overlay_info_data`](#owh_overlay_info_data) / `OverlayInfo` | `state`, source `id`, `kind`, `width`, `height`, `last_result`. Both overlay types expose this record. |
| [`owh_pixel_buffer`](#owh_pixel_buffer) / `PixelBuffer` | `pixel_format`, `pixels`, `pixel_bytes`, `width`, `height`, `stride`. Native `pixels` is borrowed; JVM `layout()` clears it and `PixelFrame.pixels()` supplies the scoped writable view instead. |
| [`owh_submission_info`](#owh_submission_info) / `SubmissionInfo` | `state` plus publication `result`; distinct from the result of polling or waiting. |
| [`owh_event_capabilities`](#owh_event_capabilities) / `EventCapabilities` | Process/session identity and event-source `status`. No overlay limits. |
| [`owh_subscription_info`](#owh_subscription_info) / `SubscriptionInfo` | `state`, `hook_enabled`, `warnings`, `qpc_frequency`, `started_qpc`, `started_utc_filetime`, `last_result`. The clock anchor relates captured QPC ticks to UTC FILETIME. |
| [`owh_event`](#owh_event) / `Event` | `sequence`, `lost_before`, `captured_qpc`, `source_channel`, `producer_process_id`, `raw_size`, `raw`. Only the first `raw_size` bytes are meaningful. The producer PID is metadata reported by Overwolf; the SDK does not access that process. |
| [`owh_source_channel`](#owh_source_channel) / `SourceChannel` | `source_channel`, `kinds`, `producer_process_id`. Kinds are `OWH_CHANNEL_INFO` and/or `OWH_CHANNEL_EVENTS` flags observed during discovery. The PID distinguishes producers on the same channel; zero means a classic source without an associated producer identity. |
| [`owh_state_field`](#owh_state_field) / `StateField` | `category`, `key`, `bytes`, `producer_ticks`, `size`, `reserved_bytes`. Unlike output-record sizing, this `size` is the value byte length. Names are bounded provider strings; values have explicit lengths. Ticks and reserved lengths retain provider-specific meanings. |

Native record declarations and all status/state constants are in the public
`include/overwolf_hook/types/` headers. JVM callers use generated
`OwhConstants`. Overlay states are `AWAITING_TEXTURE`, `READY`, `LOST`, and
`CLOSING`; subscription states are `STARTING`, `ACTIVE`, `STOPPING`, `LOST`,
and `FAILED`. These report SDK resource state, not whether a game is running
or an image is visible. Submission outcomes are explained under
[queue behavior](../overlays/pixel-buffers.md#choose-how-pending-images-are-handled).

## Native record layouts

These declarations come from the same public headers as the native ABI and JVM
models. Compiler-only generation annotations are omitted. See [data records](#data-records)
for field meanings/defaults and the corresponding API operation for output
initialization. `StateField.size` is a value length, not an output size tag.

--8<-- "docs/assets/generated/reference/records.txt"
