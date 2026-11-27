# Connections and resource lifetime

Understand when your application needs an Agent connection, how attach affects
Overwolf, and which objects you must keep alive. These rules apply to C, C++,
Java, and Kotlin. Exact fields and numeric values are in [data records](data-records.md)
and [constants and flags](constants.md); callable contracts are in the
[API references](native-api.md).

## Choose whether to attach

Overlays and event subscriptions require an attached SDK context. Passive
game-state reads and local Overwolf installation/process management do not.
Opening the SDK loads the Client locally; it does not attach to Overwolf.

For an overlay or event application, the normal sequence is:

1. Open an SDK context with the dependency's matching Client/Agent binaries.
2. Attach before entering the game. Default attach may close and restart Overwolf.
3. Create overlay sources or event subscriptions and keep their owners alive.
4. Close those resources, then detach or close the SDK context when finished.

Detach does not stop Overwolf. Closing a resource ends that resource, not other
clients' resources. See [your first overlay](../overlays/first-overlay.md) or
[event subscription](../game-events/index.md) for a runnable application.

## Attach modes

Use the default **restart** mode before entering the game. It requests normal
exit if Overwolf is running, launches Overwolf, and attaches during startup.
It does not force termination. If Overwolf cannot exit or the required startup
boundary cannot be established, attach fails. A successful attach does not
guarantee that an individual feature is ready.

Advanced **HOT** mode attaches to an already running Overwolf process. It
never starts or stops Overwolf, but accepts a setup race that can crash it.
Choose it only if that risk is acceptable for your application.

The native `owh_client_options.attach_mode` field defaults to
`OWH_ATTACH_RESTART`; Java/Kotlin inputs expose `setAttachMode(...)`.
Use `OWH_ATTACH_HOT` or generated JVM `OwhConstants.OWH_ATTACH_HOT` for HOT;
do not copy numeric values into your application.

??? details "Process selection and startup failure details"

    Restart (`OWH_ATTACH_RESTART`, `0`) signals Overwolf's existing cooperative
    shutdown event and waits up to ten seconds for the selected main process to
    exit. An unavailable event is `UNSUPPORTED`; an exit deadline is `TIMEOUT`.
    The SDK never falls back to forced termination.

    With no running process and no explicit selector, the Client uses the
    installed ShellExecute launcher and verifies the new main-process identity.
    An explicit PID/creation-time selector names the old process. If that PID
    is not running, attach returns `NOT_FOUND` rather than launching a replacement.
    A reused PID with a different creation time is rejected as `INVALID_ARGUMENT`.

    Restart loads the matching Agent before Overwolf native initialization and
    requires its controlled startup boundary. If that boundary was missed or
    cannot be established, attach fails with `SYSTEM_ERROR`, stage `BOOTSTRAP`,
    and Win32 `ERROR_NOT_READY`. Passing this boundary is not proof of safety
    for every Overwolf update; feature signatures and readiness remain separate.

    HOT (`OWH_ATTACH_HOT`, `1`) may load the Agent but skips the early startup
    observer. Later lifecycle guards still apply once installed. See the
    [startup mechanism](../development/internals.md#native-startup-boundary)
    for implementation details.

Hooks are process-resident and are not physically removed while Overwolf is
running. SDK detach and close end the SDK session; they do not stop Overwolf.

## Interpret preflight and readiness

Preflight produces a passive report: use `owh_sdk_check` in C,
`sdk.check_client()` in C++, or `sdk.check()` in Java/Kotlin.
An `OK` operation means
the report was produced; inspect `checked & ~passed` for failures. A bit absent
from `checked` was not tested. The `OWH_CHECK_*` bits cover host identity,
access to Overwolf's `OWAgent.dll` and its on-disk hash, common/lifecycle targets
and pixel-buffer and shared-texture source targets. This is **not** the injected SDK Agent
(`owh_agent.dll`).
They inspect existing process information and the on-disk module without
installing feature hooks.

`OWH_CHECK_AGENT_HASH` passing means the digest could be read, not that it is a
known build. Only when that check passed does `reviewed_build` identify a build
retained in this library's review data (`OWH_BUILD_REVIEWED` or
`OWH_BUILD_UNREVIEWED`); it does not indicate Overwolf endorsement. Unknown
versions/hashes are not locked out. A failed required target check identifies a
blocker for that path at this moment; passing is no safety, injection-permission,
game-data, adapter/settings or display guarantee.

After attach, use overlay/event `capabilities()` for their current source
readiness. For overlays, `available` uses `OWH_CAP_PIXEL_BUFFER` (pixel-buffer submission) and
`OWH_CAP_D3D11_SHARED_TEXTURE` (shared-texture binding); `enabled` also accounts for experimental
opt-in. Read each path's result separately. A successful outer call can return
a failed `pixel_buffer_status`, `shared_texture_status` or event `status`. Creation, binding and
subscription still validate the prerequisites when invoked.

## Contexts and ownership

`owh_sdk_open` creates an independent context. It has no attached session until
`owh_sdk_attach` succeeds. Detach ends the current session, and a later attach
does not transfer old resources to the new session. State reads use existing
provider mappings and do not require an attached Agent session.

Overlay and event resources created under an attachment stay tied to that
session. Detaching or closing the SDK context ends the session, and a later
attach does not resume its overlays or event subscriptions. State snapshots and
source lists are independent passive copies. Close overlays and subscriptions
when finished.
Callers serialize each resource's calls and close; only a cancellation-token
signal may run alongside a wait. Join all waits before closing the token.

Resource ownership follows these rules:

| Resource | Lifetime and close behavior |
| --- | --- |
| SDK context | Independent attach state; close consumes the context. |
| Pixel or shared-texture overlay | Fixed source geometry. Returned native close consumes the local handle, even on error; remote cleanup may continue. For textures, see [producer-reference lifetime](../overlays/shared-textures.md#close-the-source). |
| Pixel frame | Exclusive writable borrow; commit and discard consume it on every valid invocation. Overlay close revokes publication; an outstanding borrow stays mapped until consumed. |
| Submission | Local observation of one publication; poll or wait does not own a pixel slot. Join active waits before closing the receipt. |
| Event subscription | Consuming live stream; close ends delivery with no replay or drain promise. |
| State snapshot | Owned copy; field pointers/views retain bytes only while the snapshot owner remains alive. |
| Source list | Owned point-in-time discovery result. |

Game-state discovery and reads, client preflight, and resident-Agent probing are
passive C operations. They take no SDK context because they do not use attach
state or an Agent session. SDK-bound controller accessors remain conveniences;
native C++ `GameStateController{}` and Java `GameStateController.open()` work
independently.

Closing a handle ends your application's access; it does not mean all remote
resources have already been destroyed. The Agent continues deferred cleanup,
including after a remote close failure. Do not retry a consumed handle.
Explicit C++/Java close reports cleanup errors. C++ destructors do not throw
and do not retain those errors.

??? details "Java native-call failures during close"

    Java wrappers consume local ownership after the raw native close call
    returns, then check its result. A Foreign Function and Memory (FFM)
    invocation failure before native entry leaves the handle available for
    cleanup. A returned native error still means the handle was consumed and
    must not be retried. Handle the exception from explicit `close()` when
    your application needs to report cleanup failure.

## Results and defaults

`owh_result` reports a status, failure stage, error domain, and native code.
Check status before reading outputs. `NO_DATA` is a nonblocking stream result;
`BUSY` means a short admission/slot operation could not proceed; `TIMEOUT`
ends a bounded wait. These codes have per-operation meanings.

Use each native `owh_*_init` function for input defaults. JVM-generated input
models invoke the same initializer before applying fields explicitly set by the
caller. A null optional options pointer selects native defaults where noted.
`uint32_t` values are exposed as unsigned Java `long`; native records remain
the canonical C representation.

## Timeouts and session loss

A timeout has different effects depending on the operation:

| Operation | What a timeout ends | What remains usable |
| --- | --- | --- |
| Event `receive` | This wait for a matching record | The subscription; receive again when needed |
| Submission `wait` / `await` | This wait for a publication outcome | The receipt and the submitted image; publication can still finish |
| Pixel-buffer `submit` | Admission when no slot can be obtained | The overlay; an image already handed off is not cancelled |
| Agent request, including `ping` | The request/reply exchange | An in-flight exchange timeout closes the session; its overlays and subscriptions must be recreated |
| Overwolf `start` / `stop` | The wait for a process to appear or exit | The process action can still take effect; query its identity again |
| State `read` | The provider-mutex wait; reported as `BUSY` | The passive controller or storage descriptor |

`ping` is an active Agent request, not a passive observation. If its reply
times out after the exchange starts, the Client closes the connection to avoid
mistaking a late reply for another request's result. The Agent retires that
session's resources. A timeout while waiting for the local request gate, before
starting the exchange, does not close it. Preserve the operation and stage when
reporting a timeout; the status alone does not distinguish these cases.

After session loss, close the old resource handles, detach, and attach again
before creating replacement resources. Remember that default attach can restart
Overwolf. See [attach modes](#attach-modes) before reconnecting during a game.

Image-copy and metadata synchronization also run on the calling thread.
Pixel-buffer poll/acquire/try-submit calls do not wait for publication or queue
capacity, but completion collection can wait up to 50 ms for a shared metadata
lock, plus scheduling time. Their names do not imply a strict wall-clock bound.
The same internal wait can exceed a short submit or receipt-wait deadline.
See [pixel-buffer delivery](../overlays/pixel-buffers.md#choose-how-pending-images-are-handled)
for queue policy and receipt limits.

## SDK build information

The SDK release version comes from the dependency's `version.txt`. Native
builds also report an unsigned 64-bit build number: local builds default to
`0`, while CI assigns the workflow run ID. Client and Agent compatibility uses
that exact pair. It is a producer-supplied compatibility label, not a file
fingerprint or a promise that matching files are identical. C ABI and IPC
framing versions describe their respective formats and remain separate values.
`owh_sdk_build_info()` reads the loaded Client's pair without attaching;
Agent information reports the resident Agent's pair even when it does not
match the Client.

## Feature-specific contracts

After establishing the shared connection and ownership rules, use the feature's
contract for its data and scheduling:

- <a id="pixel-images-and-queue-behavior"></a>[pixel-buffer submission](../overlays/pixel-buffers.md):
  full-image submission, latest/FIFO delivery, optional direct borrows and receipts.
- <a id="experimental-d3d11-shared-texture"></a>[Experimental shared-texture binding](../overlays/shared-textures.md):
  allocation requirements, opt-in, one binding, and producer-allocation lifetime.
- <a id="event-streams"></a>[Event delivery](../game-events/data-model.md#live-delivery):
  bounded storage, loss counters, independent cursors, and no replay.
- <a id="passive-state-snapshots"></a>[Passive state reads](../game-state/reading-state.md#interpret-read-outcomes):
  provider availability, bounded lock deadlines, byte values, and owned snapshots.

<a id="interrupted-shared-memory-access"></a>
For process failure during event-ring access, see
[interrupted shared-memory access](../game-events/data-model.md#interrupted-shared-memory-access).
