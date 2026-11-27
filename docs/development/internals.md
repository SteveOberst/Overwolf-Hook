# SDK internals

Use this map when changing the native integration, shared-memory transport, or
generated language models. It describes private implementation boundaries, not
additional APIs your application must call. Start with [how it works](../architecture.md)
for the user-facing process model and [Contributing](https://github.com/SteveOberst/Overwolf-Hook/blob/main/CONTRIBUTING.md)
for build and test commands.

## Implementation map


The source tree separates the following responsibilities:

| Layer | Location | Responsibility |
| --- | --- | --- |
| Canonical records and constants | `include/overwolf_hook/types/*.h` | C-compatible values shared by native code and generated JVM views |
| C ABI | `include/overwolf_hook/abi/{sdk,overwolf,overlays,events,wait,game_state}.h` | Opaque handles, init functions, and exported operations |
| C++ facade | `include/overwolf_hook/{overwolf_hook,sdk,overwolf,overlays,events,game_state,wait}.hpp` | Context-bound controllers, move-only RAII resources, and shared wait cancellation |
| Java and generated values | `jvm/java/src/main/java/dev/sxlver/overwolfhook/`, `jvm/tools/generate_bindings.py`, `tools/bindings/` | FFM calls, local resource owners, controllers, and AST-generated immutable/output and mutable/input models |
| Kotlin facade | `jvm/kotlin/src/main/kotlin/dev/sxlver/overwolfhook/kotlin/` | Kotlin properties, durations, and cold event flows |
| Client | `modules/client/src/{abi,session,overwolf,events,overlays}/` | C ABI adaptation, SDK/session context, and service proxies |
| Agent | `modules/agent/src/{agent_root,agent_session}.cpp`, `overlays/`, `events/` | Owns pixel consumers and overlay resource records, the bounded event stream, and one accepted immutable session policy per client |
| Core contracts | `modules/core/include/owh/core/{types.hpp,policy.hpp,service_contracts.hpp,services.hpp,*.def}` | Canonical native inputs and typed Session/Overlay/Event service contracts |
| IPC | `modules/ipc/{include/owh/ipc,src}/` | Boost.Interprocess shared regions, call transport, pixel slots, and one bounded live event ring |
| Overwolf passive integration | `modules/overwolf/src/{signatures/resolver.cpp,state/}` | Overlay target resolution, provider reads, and state decoding |
| Overwolf active integration | `modules/overwolf/src/{events/source.cpp,signatures/event_resolver.cpp,overlays/}` | Early `runAgent` startup observer, process-resident lifecycle hooks, event-dispatch hook, single overlay actor, native handles, source adapter, and D3D11 import |
| Platform | `modules/platform/` | Windows handles, identity, hashing, deadlines, wait results, and actor wake signals |

PE consumers share bounded operations in `modules/platform/include/owh/platform/pe_image.hpp`.
Raw-file RVAs are translated through section raw ranges; mapped-image RVAs use image bounds.
The signature resolvers still own feature-specific patterns and require unique required targets;
the helper does not encode Overwolf versions or hashes.

The public APIs adapt to the same C ABI. Internally, `modules/core/include/owh/core/services.hpp` declares typed operation contracts from the `.def` catalogs; Client proxies and Agent session services implement those contracts around IPC. Domain/native headers depend on `types.hpp`; request/response envelopes live in `service_contracts.hpp` and are constructed by the service adapters. These are private native interfaces, not public parallel controller APIs. C++/Java controllers are thin language surfaces; they do not maintain separate transports or field-by-field copies of native value models. C++ and Java own local opaque-handle slots without general call/close synchronization. Callers serialize each resource's calls and close; only cancellation-token signaling may run alongside a borrowed wait. Kotlin delegates resources to Java.

Event capture and delivery have separate owners. `modules/overwolf/src/events/source.cpp` owns the hook and profile-specific setup. `modules/agent/src/events/event_hub.cpp` publishes captured records once into the bounded shared ring and wakes subscriptions; each Client subscription reads with its own cursor. Each `AgentSession` owns the `SessionContext` used by its services.

Admission supplies a verified peer identity, separate from bootstrap arguments. Only successful HELLO establishes the immutable limits/opt-in policy; resources share that accepted policy rather than copying ABI options or target-selection fields.

After a session finishes serving its last call, its services and peer-import helper are destroyed. The independent cleanup worker retains only the session ID; domain-owned resources retain the policy, imported handles and native pins they still need.

A native call still in progress can keep its session worker alive until it returns. Deferred native cleanup after that point does not retain the communication graph.

## Native startup boundary

The user-facing [attach contract](../reference/contracts.md#attach-modes) owns
process selection, restart effects, deadlines and failure results. The following
describes how the current native adapter establishes its startup boundary.

Restart mode loads the matching Agent in the verified process and arms an early
startup bridge before `OWAgent.dll` exists. The bridge observes
`OWServer.dll` resolving `GetProcAddress("runAgent")`, then installs lifecycle
guards before the returned function pointer reaches its caller. The guards
observe `runAgent`, `clearAgent`, `unhookAgent`, and the signature-resolved
`AgentBase::Init`. Admission requires one successful synchronous Init result
inside a normally completed run plus validation of the current root. A root
and non-null refresher alone do not prove initialization succeeded. Failed,
missing or throwing Init keeps admission closed; this does not certify later
background work. Clear and unhook revoke readiness even if old root fields
remain populated; only a new successful observed initialization can reopen it.
Deferred pixel completions also progress after a terminal publish error; querying
readiness does not perform that maintenance. If that startup point was missed, controlled attach fails with
`SYSTEM_ERROR` at `BOOTSTRAP` and Win32 `ERROR_NOT_READY`; it does not switch to
HOT. Success establishes the controlled startup boundary only. Feature
signatures and readiness are checked separately when a feature is queried or
created. This startup boundary has not been proven safe for every Overwolf
update.

HOT omits the early observer; it accepts the initial setup race documented in
the attach contract. Both modes keep installed hooks process-resident. The SDK
does not remove them while Overwolf runs or scan thread stacks to choose a mode.

## Resource ownership inside the Agent

Each SDK object owns one context. Overlay and event controllers use that context
to resolve an attachment when creating resources or querying capabilities.
An existing resource pins the exact session it was created under. Reattach does not migrate old resources. Game-state discovery and reads use a separate passive path without an SDK context or attachment.

The overlay domain owns pixel consumers, pending read leases, budgets and
publication results. Native window/registration/texture operations run on one
Overwolf actor. Domain transitions cross that actor boundary; the native host
does not own the pixel queue. A pixel-buffer read lease remains held across native `BUSY`
retries and is completed after a final publish result. Logical close is
acknowledged before deferred physical cleanup ends; the domain retains the
stream and native pins during that cleanup.

Managed pixel-buffer submission claims a writable slot from the existing pixel stream,
copies the input once, and commits it. Default latest-image admission replaces a
READY slot before using free capacity; READING slots are never overwritten.
FIFO admission waits on completion/peer/cancellation instead of adding a second
queue or publisher thread. Slot reuse returns the prior completion before its
sequence is replaced, preserving optional receipts. Explicit acquire and
try-submit do not wait for free capacity. Completion collection still uses a
50 ms metadata-lock budget even on poll/acquire/try-submit paths; these are not
strict nonblocking operations. Tracking capacity is checked at commit, after
managed admission, so a receipt-limit failure can already have replaced an
unread latest image. See the [public outcome rules](../overlays/pixel-buffers.md#track-publication-when-needed).
The JVM's ordinary submit uses native managed admission, scoped `PixelFrame`
copying, then the same commit; this accepts heap
buffers without a native staging allocation. The C ABI's prepare operation is a
bridge primitive, not a separate transport or policy implementation.
Java's immediate `trySubmit` instead uses direct acquire/copy/commit. It does
not replace READY slots; unlike managed admission, direct acquire does not
reclaim a completion that arrived after the initial drain until the next call.

The connection serializes control calls through one request gate. A send or
reply failure closes the connection, including a timeout after the exchange
starts. A gate-acquisition timeout sends nothing and leaves it open. The
server retires the closed session's resources; the transport does not attempt
to drain a late reply and reuse the connection. See [timeout effects](../reference/contracts.md#timeouts-and-session-loss).

The normal D3D11 factory combines creation and binding and closes on failure.
Internally, creation reserves an unbound source. Binding validates/imports the
allocation, creates/registers its window and calls the shared-texture publisher.
The Agent retains its import during deferred cleanup; that retention does not
provide a consumer fence or safe later-write point.

Overlay and event capability queries are domain-specific point-in-time reports. HELLO accepts and returns the resolved immutable session policy; it does not probe overlay or event readiness. An overlay capability query does not wait for the overlay actor or change its adapter state. Create and bind operations still validate readiness at their owning service boundary.

Each event subscription consumes matching records from the live ring. The ring is capped at 4 MiB and 64 slots; capture ingress is capped at 1 MiB and 16 entries. Capture stops with no active subscriptions. A slow stream can miss overwritten records; global `lost_before` is carried to the next matching event. Kotlin Flow owns one subscription per collection and checks cancellation between bounded receives.

Native resource close consumes local ownership even when the remote operation reports an error. The Agent takes responsibility for deferred physical cleanup and retains failed cleanup in its process-owned resource table. Its production root remains resident; destroying an owned test root drains native resources before releasing their stream and texture pins, and can wait for an already-running native call. C++ destructors do not throw; explicit close calls report errors. A later session does not take ownership of a previous session's resources. These lifetime guarantees are implemented at native handle and session boundaries; application callers still must stop using any returned borrowed pixel span before commit/discard.

## Generated models and change locations

Native records in `types/*.h` are the only field and constant source. jextract generates FFM layouts and accessors. The pinned libclang AST generator reads the same headers and emits Java `model` classes which call those accessors. Output models copy native values into an owned arena; nested records and raw byte views retain that owner. The generated `PixelBuffer` metadata copy clears its borrowed `pixels` pointer; `PixelFrame` exposes the writable borrow through a separate scope that ends on commit or discard. Input builders call the corresponding native `owh_*_init` function and overwrite only fields whose setters were called. Unknown pointers fail generation unless the C declaration supplies bounded ownership metadata. Kotlin consumes generated Java values directly.

| Change | Primary location | Related work |
| --- | --- | --- |
| Add/change a public value field or constant | `include/overwolf_hook/types/` | Add ownership/bounds metadata where required; regenerate FFM and JVM models |
| Add a public native operation | `include/overwolf_hook/abi/` | Implement the adapter and controller/resource surface; update native and language references |
| Change a controller operation | C++ headers or `jvm/java/...` | Keep it a facade over the existing ABI; update Kotlin adapters and examples |
| Change shared transport behavior | `modules/ipc/` and its tests | Preserve typed Core contracts and update flow/sequence documentation |
| Change event capture | `modules/agent/` or `modules/overwolf/` | Keep producer-side losses distinct from SDK stream losses |
| Change state reading | `modules/overwolf/src/state/` | Preserve passive access, bounds, and owned result lifetime |

See [shared contracts](../reference/contracts.md), [native API](../reference/native-api.md), and the [JVM references](../reference/java-api.md) for callable details.

Client-side memory limits account for actual pixel-buffer mappings and follow their
backing until the last frame/receipt pin is released. Overlay counts and shared-texture
import costs are enforced by the Agent; the Client does not reserve estimated
texture bytes or maintain a second remote-overlay counter.

`AttachedSession::create<Method>` owns resource-ID allocation and the ordered
Create call. Event and overlay code prepare their domain-specific inputs through
that boundary instead of coordinating a separate lock/reserve/send sequence.
The SDK context itself is caller-serialized; held resources pin their original
attached session and do not require a back-reference to the SDK object.
