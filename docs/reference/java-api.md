# Java API

Use this reference to find Java controllers, resource methods, and failure
types in `dev.sxlver.overwolfhook`. The API targets Java 25 on Windows x64
and requires `--enable-native-access=ALL-UNNAMED`. It uses Java's Foreign
Function and Memory (FFM) API over the
same C ABI as native callers. The facade has per-instance SDK contexts; only
the DLL loader is shared by the class loader.

`OverwolfHook.buildInfo()` returns the loaded Client's generated `BuildInfo`
record without opening or attaching an SDK context. `AgentInfo` includes the
resident Agent's own build information.

For a complete application, see [your first overlay](../overlays/first-overlay.md).
For repeated image updates, see [pixel-buffer submission](../overlays/pixel-buffers.md).

## Quick lookup

Choose a resource to find its methods and language-specific results. Shared
field meanings, numeric flags and native outcomes have one owner in
[data records](data-records.md#data-records) and the [C ABI reference](native-api.md).

| Resource | Operations |
| --- | --- |
| [OverwolfHook](#overwolfhook-context) | Open/select DLLs, attach/detach, check/probe, build/Agent identity, ping, controller access, close |
| [OverwolfController](#overwolfcontroller) | Discover installation/process, start, stop |
| [OverlayController](#overlaycontroller) | Capabilities, create either source type |
| [PixelBufferOverlay / PixelFrame](#pixelbufferoverlay-and-pixelframe) | Submit images, configure delivery, optional direct frames, info, close |
| [Submission](#submission) | Poll, await, close |
| [SharedTextureOverlay](#sharedtextureoverlay) | Bind, info, close |
| [EventController / EventSubscription](#eventcontroller) | Capabilities, subscribe, query, poll/receive, close |
| [GameStateController / GameStateStorage / StateSnapshot](#gamestatecontroller) | Discover channels, select storage, read and enumerate copied fields |
| [WaitCancellation / errors](#cancellation-and-failures) | Signal/join waits, inspect failures |
| [Models / ByteView](#models-and-byte-views) | Input setters, owned outputs, raw byte access |

## OverwolfHook context

`OverwolfHook.open()` reuses the selected native pair, or loads the bundled
pair on first use, and opens an independent SDK context. `open(Path)` explicitly
selects a directory containing matching `owh_client.dll` and `owh_agent.dll`.
Choose it before the first native operation. A later explicit call with a
different canonical directory throws `IllegalStateException`, even for
byte-identical DLLs. Default `open()`, `buildInfo()` and standalone controller
factories reuse the loaded pair; they do not switch back to bundled DLLs.
Loading/extraction failures throw `IOException`.

`attach()` or `attach(ClientOptions)` establishes the Agent session;
`detach()` closes that attachment. `check([ClientOptions])` produces a passive
preflight report. `probeAgent([ClientOptions])` reads resident Agent identity
without starting it; these passive operations do not use SDK attach state.
`agentInfo()` and `ping(timeoutMillis)` use the attached session. `close()`
closes this SDK context. `GameStateController.open()` provides passive state/discovery access without
an SDK context, reusing the same loader selection.

The context exposes `overlays()`, `events()`, `gameState()`, and `overwolf()` controllers.
Overlay and event operations resolve the current attachment.
Previously created resources stay tied to their original native handles.

`agentInfo()` returns cached attachment identity after a local lifetime check;
it does not send an Agent request. `ping(timeoutMillis)` sends one bounded
request and accepts 0 through 4294967294 milliseconds. An in-flight reply
timeout closes the whole session, including overlays and subscriptions. See
[timeout effects](contracts.md#timeouts-and-session-loss).

### Attach behavior

Default `attach()` can close and relaunch Overwolf; call it before entering
the game. Detach does not stop Overwolf. See [attach modes](contracts.md#attach-modes)
for selectors, startup failures and the separate feature-readiness contract.

HOT explicitly uses a running process and accepts a setup race that can crash
Overwolf. Use the generated constant with initialized options:

```java
--8<-- "docs/assets/examples/java/HotAttach.java:attach"
```

See the complete [HOT attach example](../assets/examples/java/HotAttach.java).

## OverwolfController

`OverwolfController.open()` reuses the selected DLL pair (bundled on first use),
without opening an SDK context
or attaching an Agent. `installation()` and `runningProcess([pid, started])`
return optional generated `OverwolfInstallation` / `OverwolfProcess` values.
`start([timeoutMillis])` returns the running main process; `stop(process[, timeoutMillis])`
requests normal close without forcing termination. The controller has no close
operation. See [managing Overwolf](../overwolf.md) for absence/error and version semantics.

| Method | Return / behavior |
| --- | --- |
| `static open()` | `OverwolfController`; reuses/loads the native pair, may throw `IOException`. |
| `installation()` | `Optional<OverwolfInstallation>` with directory, launcher and installed version. |
| `runningProcess()` / `runningProcess(long pid, long started)` | `Optional<OverwolfProcess>` with PID, creation time, executable and running version. Zero PID selects unique-process discovery; nonzero creation time must match. |
| `start()` / `start(long timeoutMillis)` | `OverwolfProcess`; returns an existing process or launches and waits. Default 10 seconds. |
| `stop(OverwolfProcess)` / `stop(OverwolfProcess, long timeoutMillis)` | Requests normal exit and waits for the exact process. Default 10 seconds; never forces termination. |

Query absence returns `Optional.empty()`; other native failures throw
`NativeFailure`. Start/stop timeouts must be 1 through 4294967294 milliseconds.
Neither operation attaches the Agent. Returned records own their data and do
not keep the process running or become live views of it.

## OverlayController

`capabilities()` returns generated `OverlayCapabilities`. It carries the
current process and session identity, overlay limits and budgets, available
and enabled feature masks, and separate `pixelBufferStatus` / `sharedTextureStatus` results.
`createPixelBuffer(OverlaySpec[, PixelBufferOverlayOptions])` returns a `PixelBufferOverlay`;
`createSharedTexture(OverlaySpec, LegacySharedTexture)` returns a bound `SharedTextureOverlay`
with the supplied allocation; its native window is registered during bind.

### PixelBufferOverlay and PixelFrame

`PixelBufferOverlay.submit(byte[])`, `submit(ByteBuffer)`, and `submit(MemorySegment)`
copy one complete BGRA8 image. For row padding, use `submit(segment, sourceStride)`;
zero stride means tight rows. `submit(segment, stride, Duration, WaitCancellation)`
adds optional timeout/cancellation for FIFO backpressure (null timeout is infinite).
Default latest-image submission replaces an unread image without waiting for
consumer capacity. `submitTracked(segment)` or the four-argument overload returns
a receipt. Input is reusable on return. `trySubmit(segment[, stride])` explicitly
uses free capacity and returns false when busy. See [delivery policies](../overlays/pixel-buffers.md#choose-how-pending-images-are-handled).

The direct frame methods are optional expert access. Serialize each overlay's
calls and finish outstanding borrows before managed submission.

`submit(image, PixelRegion)` accepts a damage hint with any supported image
input. The advanced segment/stride/timeout/cancellation overload adds optional
`PixelRegion` as its fifth argument. `submitTracked(segment, region)` and
`trySubmit(segment[, stride], region)` accept the same hint. Null selects the
whole image. All forms still require a complete image; see
[damage rectangles](../overlays/pixel-buffers.md#limit-overwolfs-copy-to-changed-pixels).
`PixelFrame.setDamage(region)` changes the hint before commit; null resets it.
Invalid bounds throw `NativeFailure` without consuming the frame or changing
its previous hint. Generated `PixelRegion` setters enforce unsigned field ranges.

`PixelBufferOverlay.tryAcquire()` returns `Optional<PixelFrame>`; an empty result means
the writable slot queue was busy. The frame exposes `pixels()`, `byteCount()`,
`width()`, `height()`, and `stride()`. Its byte view is a scoped native borrow;
stop using it before `commit()` or `close()` (discard). Commit consumes the
frame on every native outcome. `layout()` returns the generated `PixelBuffer`
metadata with its borrowed pointer cleared; `pixels()` is the writable scoped
view. `commitTracked()` requests tracking and returns a `Submission`. Successful
commit means queued; `PUBLISHED` means the native publish call succeeded.
Only a new acquire grants access to another writable frame.

Acquire, try-submit, and receipt poll can still wait for shared metadata.
Tracked submission is bounded by the open-receipt limit; a failed latest-image
submission may already have replaced a pending image. See
[publication tracking](../overlays/pixel-buffers.md#track-publication-when-needed)
and [timeout effects](contracts.md#timeouts-and-session-loss).

### Submission

Receipts support `poll()`, `await()`, `await(Duration[, WaitCancellation])`,
and `close()`. `Duration` here is `java.time.Duration`. The `await` overloads
block the calling thread; `poll()` returns without waiting for publication.
No timeout (or a null `Duration`) means an indefinite wait;
explicit finite timeouts must be exact whole milliseconds. Join active waits before closing a receipt. A
`WaitCancellation` token can be shared by waits; signal it and join those waits
before token close.
The SDK creates no thread per receipt.

### SharedTextureOverlay

`createUnboundSharedTexture(OverlaySpec)` is the expert factory when creation
must precede binding. `SharedTextureOverlay.bind(LegacySharedTexture)` binds one already completed
legacy shared texture. Only one binding is accepted per source. Retain the
producer allocation alive and unchanged through overlay close.
There is no consumer fence or safe rewrite notification.

Both overlay types expose `info()` returning generated `OverlayInfo` and
`close()` ending local ownership. `PixelFrame.close()` discards an uncommitted
frame. Use `try`-with-resources for all these owners, including `Submission`;
its close releases only the observation receipt.

## EventController

`capabilities()` returns generated `EventCapabilities`, with the current
process and session identity plus the event source `status` result.
`subscribe([EventOptions])` returns an independent `EventSubscription`.

### EventSubscription

`EventSubscription.info()` returns generated `SubscriptionInfo`; `poll()` and
`receive(Duration[, WaitCancellation])` consume the next matching event and
return `Optional<Event>`. A null timeout waits indefinitely. Event payload bytes
are provider bytes, not a decoded schema. A stopped native stream raises
`EventStreamClosedException`. Serialize calls and close on each
resource; signal and join waits before closing a shared cancellation token.

`receive()` is the no-argument indefinite form. `Optional.empty()` represents
`NO_DATA`, `BUSY` or a receive `TIMEOUT`; cancellation instead throws
`NativeFailure` with `OWH_CANCELLED`. `EventSubscription.close()` consumes the
subscription even when native cleanup returns an error.

## GameStateController

`sources()` returns a snapshot list of generated `SourceChannel` values.
`storage(long channel)` returns a passive `GameStateStorage`;
`GameStateController.read(long channel[, long timeoutMillis])` and
`GameStateStorage.read([long timeoutMillis])` return an owned `StateSnapshot`.
Its `fields()` returns an immutable `List<StateField>`.
`storage(SourceChannel)` and `read(SourceChannel[, long timeoutMillis])`
preserve a discovered producer selection. `storage(long channel, long
producerProcessId)` selects explicitly; `read(long channel, long timeoutMillis,
long producerProcessId)` performs the corresponding read. Producer zero uses
channel-only selection; multiple current producers require an explicit PID.
This path reads existing provider storage and does not require `attach()`.
Category, key, and value remain raw bytes; no encoding is assumed.

State reads without an explicit timeout use `OWH_STATE_DEFAULT_TIMEOUT_MS`
(100 ms) in C++, Java and Kotlin. Pass `0` explicitly for a nonblocking lock
attempt. The deadline applies to acquiring the provider's state mutex; it does
not wait for a newer game update.

State deadlines are 0 through 1000 ms. `GameStateStorage.sourceChannel()`
returns the stored channel number; constructing storage does not open a mapping
or check that it exists. `producerProcessId()` returns its producer selector.
`GameStateStorage`, `StateSnapshot`, discovery lists
and generated fields need no `close()`; native temporary resources are released
before their owned Java copies are returned.

## Cancellation and failures

`WaitCancellation.create()` creates a one-shot token. Pass it to
`EventSubscription.receive(Duration, WaitCancellation)` or
`Submission.await(Duration, WaitCancellation)`, call `cancel()` from the
signaling thread, join every wait, then `close()` the token. Cancellation ends
the wait, not the subscription or publication. `Submission.await` throws
`NativeFailure` for both `OWH_TIMEOUT` and `OWH_CANCELLED`; successful return
contains the separate publication result in `SubmissionInfo`.

`NativeFailure` extends `IOException`. Its `operation()` identifies the call,
`statusCode()` returns the native status, and `result()` returns the generated
`Result` with failure stage, error domain and native code. Invalid Java arguments
can raise `IllegalArgumentException`; use after local resource close can raise
`IllegalStateException`. A stopped native event stream uses
`EventStreamClosedException`, a separate runtime exception. Closeable resources
implement `AutoCloseable`; follow the [shared ownership rules](contracts.md#contexts-and-ownership).

See [installation](../getting-started/installation.md), [event delivery](../game-events/data-model.md),
[overlay transfer](../overlays/index.md), and [native ownership rules](contracts.md).

## Models and byte views

The generated models live in `dev.sxlver.overwolfhook.model`. Inputs such as
`ClientOptions`, `OverlaySpec`, `PixelBufferOverlayOptions`, `EventOptions`, and
`WaitOptions` provide mutable setters. Their `materialize(Arena)` method calls
the native initializer, then writes only explicitly set fields. Outputs such
as `AgentInfo`, `Event`, `ClientSanityReport`, `OverlayInfo`, `StateField`, and
`SubmissionInfo` are immutable owned views. Their Bean getters expose the copied values; byte arrays are exposed as read-only `ByteView` and can be
copied explicitly with `copyBytes()`. uint32 fields are represented as Java
`long` values in the range 0 through 2^32-1; uint64 fields preserve raw `long`
bits.

`ByteView.size()`, `get(long index)` and `asMemorySegment()` provide read-only
access; `copyBytes()` makes a Java array copy. `utf8()` explicitly decodes UTF-8
and is appropriate for documented UTF-8 fields such as Overwolf paths; it does
not establish an encoding for raw provider data. For direct ABI interop,
generated outputs provide `allocate(Arena)` and `copyOf(MemorySegment)`; inputs
provide their constructor, field setters and `materialize(Arena)`. Their fields
and defaults are listed in [shared data records](data-records.md#data-records).

For model-generation implementation details, see [generated models](../development/internals.md#generated-models-and-change-locations).
