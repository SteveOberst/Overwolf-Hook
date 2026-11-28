---
title: "SDK connections: C ABI reference"
description: "Look up C context creation, attach, detach, compatibility checks, Agent queries, liveness requests and SDK build information."
---

# SDK connection: C ABI

Look up the operations declared in `<overwolf_hook/abi/sdk.h>`.
See the [C ABI index](../native-api.md) for linking and shared result/ownership
rules, or [data records](../data-records.md) for field meanings.

For usage, see [connections and resource lifetime](../contracts.md) and the [first overlay walkthrough](../../overlays/first-overlay.md).

Windows loads the linked Client DLL when your application starts. No SDK
context is required to query `owh_sdk_build_info()` or `owh_sdk_abi_version()`.
See the [installation load check](../../getting-started/installation.md#check-that-the-library-loads).

## `owh_sdk_open`

Opens an independent SDK context:

```c
--8<-- "docs/assets/generated/reference/owh_sdk_open.h"
```

Supply a writable handle output; no `.size` is involved. Returns `OK` with ownership of one context, `INVALID_ARGUMENT` for a null output, or an allocation failure.
The context is local: this call does not create an Agent connection, inject
the Agent, or start feature hooks. Use `owh_sdk_attach` to connect it.

## `owh_sdk_close`

Closes the context; the Agent remains resident:

```c
--8<-- "docs/assets/generated/reference/owh_sdk_close.h"
```

Returns nothing. Consumes the context; null is accepted. Does not stop Overwolf or unload the Agent.

## `owh_sdk_attach`

Uses `attach_mode`: default `OWH_ATTACH_RESTART` requests normal close/verified relaunch and a controlled early Agent startup; explicit `OWH_ATTACH_HOT` targets a running process and accepts the initial crash race. Null options select the restart default:

```c
--8<-- "docs/assets/generated/reference/owh_sdk_attach.h"
```

Requires a valid context; options may be null. Returns `OK` after attachment, or an argument, process/startup, compatibility, resource or handshake failure. A missed restart boundary is `SYSTEM_ERROR` / `BOOTSTRAP` / Win32 `ERROR_NOT_READY`. See [attach modes](../contracts.md#attach-modes).

## `owh_sdk_detach`

Ends the current session; old resources do not migrate to a later attach:

```c
--8<-- "docs/assets/generated/reference/owh_sdk_detach.h"
```

Requires a valid context. Returns the session cleanup result; local detach completes even on a remote cleanup error. Old resources cannot move to a subsequent attachment.

## `owh_sdk_check`

Returns a passive preflight report without an SDK context; `OK` means a report was produced, even if some checks failed:

```c
--8<-- "docs/assets/generated/reference/owh_sdk_check.h"
```

Options may be null; initialize `output.size`. Returns `OK` when the report was produced, even if `checked & ~passed` is nonzero. Inspect the report rather than treating outer `OK` as readiness.

## `owh_sdk_probe_agent`

Reads resident Agent identity without an SDK context, attaching, or starting it:

```c
--8<-- "docs/assets/generated/reference/owh_sdk_probe_agent.h"
```

Options may be null; initialize `output.size`. Returns resident identity without attaching. Absence/readiness/compatibility and transport failures are distinct results; the returned build identity may describe a mismatched Agent.

## `owh_sdk_agent_info`

Returns the identity cached when the SDK attached:

```c
--8<-- "docs/assets/generated/reference/owh_sdk_agent_info.h"
```

Requires an attached context; initialize `output.size`. Returns `OK` with the
attachment identity, or `AGENT_NOT_READY`/session failure. It checks local
connection lifetime but sends no Agent request. This does not probe graphics
or data delivery; use `owh_sdk_probe_agent` for a passive resident-Agent query.

## `owh_sdk_ping`

Sends one bounded liveness request; no recurring heartbeat:

```c
--8<-- "docs/assets/generated/reference/owh_sdk_ping.h"
```

Requires an attachment. Returns `OK` for this liveness round trip, or a
wait/session/transport error. Supply a finite timeout in milliseconds; zero is
accepted, while `OWH_WAIT_INFINITE` is `INVALID_ARGUMENT`. This starts no
heartbeat. An in-flight reply timeout closes the entire session, including its
overlays and subscriptions. See [timeout effects](../contracts.md#timeouts-and-session-loss)
before using ping as a health check.

## `owh_sdk_abi_version`

Returns the Client ABI version:

```c
--8<-- "docs/assets/generated/reference/owh_sdk_abi_version.h"
```

Returns `uint32_t` directly, not `owh_result`. No context or output initialization is required.

## `owh_sdk_build_info`

Returns the loaded Client's SDK version and build number without attaching:

```c
--8<-- "docs/assets/generated/reference/owh_sdk_build_info.h"
```

Supply writable `owh_build_info`; it has no `.size` member. Returns `OK`, or `INVALID_ARGUMENT` for a null output. Does not attach.

## `owh_client_options_init`

Writes the native options size, ABI version, and defaults:

```c
--8<-- "docs/assets/generated/reference/owh_client_options_init.h"
```

Returns nothing. Supply writable storage for the corresponding input record; null is a no-op. Writes the native size/defaults. See [data records](../data-records.md#data-records).

`owh_client_options` selects attach mode, process identity, experimental
feature flags, and session resource budgets. See [attach modes](../contracts.md#attach-modes)
for the restart side effect, HOT risk, startup boundary, and error behavior.
`owh_client_sanity_report` and `owh_agent_info` are
defined in `types/client.h` and `types/agent.h`. Zero budget/limit fields select
SDK defaults. Nonzero overrides are validated before attaching: up to 256 overlays and 4096 tracked receipts per overlay,
and up to 1 GiB for each session allocation budget. Across sessions, the Agent
also limits overlays to 4096 resources and pixel-buffer and shared-texture allocations to 1 GiB each.
Deferred cleanup still counts against these limits.

Attach establishes immutable session limits; it does not establish overlay
or event readiness. See [attach modes](../contracts.md#attach-modes) for the
process side effects and startup contract, then query the relevant controller
for feature readiness.
