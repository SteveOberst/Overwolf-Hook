# Kotlin API

Use this reference to find Kotlin controller properties, coroutine helpers,
and resource-ownership rules. The Kotlin module adds extensions over the Java
API. It shares Java's native resource owners and generated models; it does not
create a second transport. The module targets Java 25 and Kotlin 2.4.0.

For a complete application, see [your first overlay](../overlays/first-overlay.md).
For repeated image updates, see [pixel-buffer submission](../overlays/pixel-buffers.md).

Generated Java Bean getters are visible as Kotlin properties, including for
generated values such as `Event`, `StateField`, `AgentInfo`,
`OverlayCapabilities`, and `EventCapabilities`. Input models keep their
generated Java setters. Raw provider bytes remain byte views; they are not
decoded as text automatically.

<a id="kotlin-extension-reference"></a>
## Quick lookup

These are all the Kotlin-specific additions. Deep links:
[submit](#submit), [cancellable submit](#submitcancellable),
[acquire](#tryacquireornull), [bind](#bindlegacy), [blocking await](#await),
[cancellable await](#awaitcancellable), [observe](#observe),
[receive](#receive), [state read](#read). Other methods retain the
[Java signatures and result types](java-api.md).

| Extension | Return / behavior |
| --- | --- |
| `OverwolfHook` controller properties | `overwolf`, `overlays`, `events`, `gameState`: accessors over the same SDK instance. |
| <a id="submit"></a>`PixelBufferOverlay.submit(...)` | Parameters: `image: ByteArray`, `sourceStride: Int = 0`. Copies a completed BGRA8 image using managed delivery; zero stride means tightly packed rows. |
| <a id="submitcancellable"></a>`PixelBufferOverlay.submitCancellable(...)` | Parameters: `image: MemorySegment`, `sourceStride: Int = 0`, `timeout: Duration = Duration.INFINITE`. Suspends during native admission/copy on an IO worker. Owns a cancellation token and joins the worker before returning; input must be accessible on that worker. |
| <a id="tryacquireornull"></a>`PixelBufferOverlay.tryAcquireOrNull()` | Returns `PixelFrame?`; nullable form of `tryAcquire()`. |
| <a id="bindlegacy"></a>`SharedTextureOverlay.bindLegacy(...)` | Parameters: `tokenBits: Long`, `adapterLuidLow: Int`, `adapterLuidHigh: Int`. Builds the canonical binding input and delegates to `bind`. LUID components preserve their 32-bit bit patterns. |
| <a id="await"></a>`Submission.await(...)` | Parameters: `timeout: Duration`, `cancellation: WaitCancellation? = null`. Blocking wait returning `SubmissionInfo`. Accepts `Duration.INFINITE`. |
| <a id="awaitcancellable"></a>`Submission.awaitCancellable(...)` | Parameter: `timeout: Duration = Duration.INFINITE`. Suspending wait returning `SubmissionInfo`. Owns a cancellation token, uses an IO worker for the native wait, and joins it before token release. |
| <a id="observe"></a>`EventController.observe(...)` | Parameter: `options: EventOptions = EventOptions()`. Cold `Flow<Event>`; one independently owned subscription per collection. |
| <a id="receive"></a>`EventSubscription.receive(...)` | Parameter: `timeout: Duration`. Blocking receive returning `Optional<Event>`; accepts finite whole milliseconds. |
| <a id="read"></a>`GameStateStorage.read(...)` | Parameter: `timeout: Duration`. Passive read returning `StateSnapshot`; native state deadlines are 0 through 1000 ms. |

Here `Duration` means `kotlin.time.Duration`. These extensions do not change
the native failure model: for example, cancellation/timeout of a submission
wait throws `NativeFailure`; a quiet event receive returns an empty optional.
Coroutine cancellation of `awaitCancellable` throws `CancellationException` and
waits for native cleanup before returning control.

## Context and options

`OverwolfHook.open()` creates an independent native SDK context. The Kotlin
extension properties `sdk.overlays`, `sdk.events`, `sdk.gameState`, and `sdk.overwolf` expose
the context's controllers. Use `sdk.attach()` or `sdk.attach(ClientOptions)` to
establish an Agent session; `sdk.detach()` ends it. Use generated model setters
from Kotlin.
`sdk.gameState` remains usable without attach; `GameStateController.open()`
also provides passive state/discovery access without an SDK context.

The complete [Java facade](java-api.md#quick-lookup) is also callable from Kotlin, including
`open(Path)`, `buildInfo()`, `check()`, `probeAgent()`, `agentInfo()`, `ping(timeoutMillis)`
and all controller/resource methods. Its [native loader selection](java-api.md#overwolfhook-context)
also applies here: default operations reuse an explicitly selected pair. Import the helpers you use from
`dev.sxlver.overwolfhook.kotlin`; they are extensions, not replacement owners.
An in-flight `ping` timeout closes the entire Agent session; it is different
from an event or receipt-wait timeout. See [timeout effects](contracts.md#timeouts-and-session-loss).

### Attach behavior

Default `attach()` can close and relaunch Overwolf; detach does not stop it.
Read [attach modes](contracts.md#attach-modes) for selectors, failure behavior
and readiness. HOT uses a running process and accepts a setup race that can
crash Overwolf. Set it through the generated model and constant:

```kotlin
--8<-- "docs/assets/examples/kotlin/HotAttach.kt:attach"
```

See the complete [HOT attach example](../assets/examples/kotlin/HotAttach.kt).

## Overwolf installation and process control

`OverwolfController.open()` works without an SDK context or Agent attachment.
If you already have a context, use `sdk.overwolf` with the
`dev.sxlver.overwolfhook.kotlin.overwolf` import.

| Operation | Result / effect |
| --- | --- |
| `installation()` | `Optional<OverwolfInstallation>`; directory, launcher and installed version. |
| `runningProcess()` / `runningProcess(pid, started)` | `Optional<OverwolfProcess>`; PID, creation time, executable and running version. |
| `start()` / `start(timeoutMillis)` | Returns the running main process or starts Overwolf and waits for it. |
| `stop(process)` / `stop(process, timeoutMillis)` | Requests exit of the shared Overwolf process and waits; see [process-wide effects](../overwolf.md#request-normal-exit). Does not force termination. |

Use `.orElse(null)` when nullable handling is convenient. Query absence is an
empty optional; native failures throw `NativeFailure`. Generated Bean getters
are properties such as `installation.directory`, `process.processId` and
`process.version`; string byte views provide `utf8()`.
Start/stop default to ten seconds and accept a finite positive `Long` count of
milliseconds, not `kotlin.time.Duration`. The controller needs no `close()`.
See [managing Overwolf](../overwolf.md) for installation, version, start and stop
examples and the effects of each operation.

## Overlays

`sdk.overlays.createPixelBuffer(spec[, options])` returns an owned `PixelBufferOverlay`.
Use `overlay.submit(image)` for a complete BGRA8 byte array, `ByteBuffer`, or
`MemorySegment`. The Kotlin byte-array extension also accepts `sourceStride`;
zero stride means tight rows. Latest-image delivery is the default. Set generated
`PixelBufferOverlayOptions.policy` to `OWH_QUEUE_FIFO` for bounded ordered delivery and
backpressure. `submitCancellable(segment, stride, timeout)` suspends during
admission/copy on an IO worker and joins it on coroutine cancellation; the segment
must be accessible on that worker. `submitTracked(segment)` returns a publication
receipt. `trySubmit(segment[, stride])` is explicit immediate/free-slot admission.
See [delivery policies](../overlays/pixel-buffers.md#choose-how-pending-images-are-handled).

Acquire, try-submit, and receipt poll can still wait briefly for shared
metadata. The tracked path has an open-receipt limit; a failed latest-image
submission can already have replaced an unread image. See
[publication tracking](../overlays/pixel-buffers.md#track-publication-when-needed).

`tryAcquireOrNull()` and `PixelFrame` remain optional direct-write access. Finish
pixel access before commit/discard and finish all borrows before managed submit.

Use the generated `PixelRegion` with `submit(image, region)` for optional damage.
The byte-array extension accepts named `damage` alongside `sourceStride`;
`submitCancellable(segment, sourceStride, timeout, damage)` also accepts it.
Tracked/immediate Java methods and `PixelFrame.setDamage(region)` are available
directly. Null resets full-image publication. Inputs remain complete images;
see [damage rectangles](../overlays/pixel-buffers.md#limit-overwolfs-copy-to-changed-pixels).
The default source has three shared slots. Optional slot count is bounded by
capabilities. There is no frame scheduler or per-overlay publishing worker.

`sdk.overlays.createSharedTexture(spec, texture)` creates and binds the completed
experimental allocation together. Only the expert
`createUnboundSharedTexture(spec)` needs a later `bind(texture)` or
`bindLegacy(tokenBits, adapterLuidLow, adapterLuidHigh)`. Keep the producer allocation
alive and unchanged through close; no consumer fence or safe rewrite notification
is provided.

## Events

`sdk.events.subscribe(options)` creates an independently owned subscription.
`EventController.observe(options)` returns a cold `Flow<Event>` which creates
and closes one subscription for each collection. It receives in bounded
100-millisecond intervals, so coroutine cancellation is checked between
receives. There is no replay or local multicast. Each generated `Event` owns
its copied payload. A slow stream can miss overwritten records; check
`event.lostBefore` for known global ingress/ring loss carried to the next
matching event. Native stream closure completes the flow normally.

See a complete [Flow collection example](../game-events/index.md#collect-events-with-kotlin-flow)
for setup and collection lifetime.

## Submissions and cancellation

Use `Submission.await(timeout)` for a blocking wait or `awaitCancellable()`
from a coroutine. Both return `SubmissionInfo`; its nested `result` describes
publication, separately from whether the wait succeeded. Close the receipt
after reading its outcome. A receipt wait does not confirm visible display.

Finite duration overloads accept whole-millisecond values up to 4294967294.
Receipt awaits also accept `Duration.INFINITE`, which is the default for
`awaitCancellable()`; Java represents this as a null timeout. Negative, fractional millisecond, or
out-of-range durations are rejected. Blocking overloads block the calling
thread. Coroutine receipt waits use a per-call cancellation token and join the
native wait before releasing it; cancellation ends the wait, not publication.

See [publication tracking](../overlays/pixel-buffers.md#track-publication-when-needed)
for complete helper examples and receipt limits. `WaitCancellation` is the Java
resource owner; use `cancel()` to signal it, join its waiters, then close it.

## Game state

`sdk.gameState.sources()` returns current `SourceChannel` values.
`sdk.gameState.storage(channel).read([timeout])` returns an owned snapshot of
the existing provider mapping. This is a passive read and does not require an
Agent session. Snapshot fields are generated `StateField` values with raw byte
views and provider ticks.

`GameStateController.read(channel[, timeoutMillis])` is also available directly.
Pass a generated `SourceChannel` to `read(source)` or `storage(source)` to
preserve the discovered producer selection. `storage(channel,
producerProcessId)` is available for explicit selection; zero accepts the
channel-only selection. `GameStateStorage.producerProcessId()` returns the
stored selector. Multiple current producers on a channel require an explicit PID.
`GameStateStorage.sourceChannel()` identifies its passive descriptor, and
`StateSnapshot.fields()` returns the copied field list. None of these passive
values needs closing. Use `use` for the Java `AutoCloseable` owners: SDK context,
overlays, frames, receipts, subscriptions and wait-cancellation tokens.

For exact Java signatures and ownership, see [Java API](java-api.md) and
[shared contracts](contracts.md).
