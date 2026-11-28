---
title: "Troubleshoot SDK loading and live features"
description: "Diagnose native loading, Agent connections, missing game data and invisible overlays using SDK error details and feature-specific checks."
---

# Troubleshooting

Find out whether a failure comes from local library loading, the Agent connection,
or a live feature. Start with the operation that failed and preserve its `owh_result` fields:
status, failure stage, error domain, and native code. A source channel is an
Overwolf transport identifier, not a public game ID. The SDK context owns
attachment state; its `overlays` and `events` controllers use that session.
`gameState` reads existing provider mappings without attach. For native result
values, see the [status and flag lookup](reference/constants.md#values-and-meanings).

## Fix local library loading

If loading fails before `attach()`, starting a game will not fix that phase.
Use the [installation load check](getting-started/installation.md#check-that-the-library-loads):

1. Verify that your application is x64. On the JVM, run `java --version` using
   the same launcher as the application and confirm Java 25.
2. For native applications, confirm both matching DLLs were copied beside the
   executable. For Java/Kotlin, use the normal dependency artifact with bundled
   DLLs; do not separately copy native binaries into your project.
3. Install the x64 Visual C++ Redistributable if it is missing. A DLL can exist
   and still fail to load when one of its dependencies is absent.
4. Run the load check again. Exit code `0` confirms the local library loaded;
   only then continue to attach or a passive controller.

Java and Kotlin can call `OverwolfHook.load()` to isolate local loading from
context creation. It does not open a context or connect to Overwolf. Other SDK
entry points also load the Client automatically if needed. See
[native library loading](reference/java-api.md#native-library-loading) for
custom directory selection and loading failures.

Common local symptoms have different causes:

| Symptom | Action and expected result |
| --- | --- |
| `UnsupportedClassVersionError` | Select JDK 25 for both build and application launch, then rerun the load check. Changing only Gradle's JVM does not necessarily change another launcher. |
| Native-access warning or denial | Add `--enable-native-access=ALL-UNNAMED` to the application's JVM options, then run the same entry point again. |
| `UnsatisfiedLinkError` or missing-DLL error | Check x64 architecture, matching DLL packaging and the Redistributable. Preserve the complete loader error, including any named dependency. |
| Missing runtime payload / extraction `IOException` | Confirm the dependency includes its native resources and the application's temporary directory is writable; rerun without manually mixing another release's DLLs into it. |
| A different native build is already loaded | A previous explicit `load(Path)` or `open(Path)` selected another canonical directory. Use that same selection or start a new application process to switch; default factories reuse the selected pair. |

<a id="diagnose-attach-and-feature-failures"></a>
## Diagnose an Agent connection

Use the failing operation and result stage to choose a check:

| Symptom | What to check |
| --- | --- |
| `NOT_FOUND` while attaching or probing | Restart mode without a process selector can launch Overwolf when none is running. An explicit PID that is not running returns `NOT_FOUND`; a reused PID with a different creation time returns `INVALID_ARGUMENT`. HOT requires an existing process and never launches Overwolf. `probeAgent()` only reads a resident Agent. |
| Attach returns `TIMEOUT` or `UNSUPPORTED` at `OVERWOLF_STOP` | Restart mode could not observe exit after the cooperative shutdown request, or Overwolf's shutdown event is unavailable. The SDK does not force termination. Query the process again before retrying; a timed-out request can still complete. |
| Attach returns `SYSTEM_ERROR` at `BOOTSTRAP` with `ERROR_NOT_READY` | Restart mode missed the early `runAgent` lookup or found `OWAgent.dll` already loaded before its observer was armed. It does not fall back to HOT. After Overwolf exits normally, retry restart attach without an old-process selector; choose HOT only if its initial crash risk is acceptable. |
| HOT attach crashes Overwolf during startup | HOT deliberately accepts the initial Agent-initialization race. Use default restart mode when the controlled early startup boundary is required. |
| `INCOMPATIBLE` or profile/target validation failure | Run `sdk.check()` and inspect which checks failed. Deploy matching Client and Agent DLLs. A passing preflight is point-in-time and does not guarantee later feature behavior. |
| `ping` times out, then existing resources report `AGENT_LOST` | An in-flight RPC timeout closes the session. Close old resources, detach, and recreate them after a successful attach. Default attach can restart Overwolf; see [timeout effects](reference/contracts.md#timeouts-and-session-loss). A receipt/receive timeout has different effects. |

## Check process and integration readiness

On an opened SDK context, `sdk.check(options)` in Java/Kotlin
(`sdk.check_client(&options)` in C++) runs passive preflight checks and returns a
generated `ClientSanityReport`. The report's `checked` and `passed` fields
distinguish a failed check from a check that was not run. A report does not
attach a session, create a hook, window, texture, or source. `probeAgent()`
reads identity of a resident Agent without starting it; `agentInfo()` requires
attach.

For missing game data, check the stages separately:

1. Query `OverwolfController.runningProcess()` to establish whether Overwolf
   main is running. A process result alone says nothing about a game's provider.
2. For state, run the [state example](game-state/reading-state.md). An empty
   source list, a zero-field snapshot and an error are distinct observations.
3. For events, run the [event example](game-events/index.md) and wait for its
   listening message before generating activity in the supported game. Query
   the subscription's `info()` if delivery stops; inspect state, warnings and
   `lastResult` rather than treating a quiet receive as failure.
4. Compare observed fields/records with the game's provider reference. Discovery,
   attach and this SDK do not enable a missing provider or unsupported game.

## Diagnose event delivery

For a quiet or stopped subscription, distinguish source readiness from stream results:

| Symptom | What to check |
| --- | --- |
| No events arrive | Attach the SDK context and create an event subscription. `NO_DATA` from poll, `BUSY`, receive timeout, and stream close are different outcomes. Check the provider and selected source channel. See [event delivery](game-events/data-model.md). |

## Diagnose state reads

Discovery and reading are separate operations. Use the read result to distinguish
absence, contention, invalid data, and a successful empty snapshot:

| Symptom | What to check |
| --- | --- |
| No source channels or expected state fields | Confirm Overwolf and the provider are running. Discovery does not start them; a successful empty inventory or snapshot is different from an error. See [state reads](game-state/reading-state.md). |
| State read returns `NOT_FOUND`, `BUSY`, or `CORRUPT` | The mapping may have disappeared, its mutex may not be available before the deadline, or its bounds/metadata may be invalid. Rediscover and read again when appropriate. |

## Diagnose image transfer

Source creation, publication, and display have different prerequisites:

| Symptom | What to check |
| --- | --- |
| Overlay source is not visible | A successful commit only queues a pixel-buffer image. `READY` or `PUBLISHED` reports source/publisher acceptance, not display. Check game compatibility, Overwolf overlay settings, image alpha, geometry, and row stride. |
| `OPT_IN_REQUIRED` for D3D11 | Set `OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE` in initialized `ClientOptions` before attach. Review the [shared-texture limits](overlays/shared-textures.md). |
| D3D11 texture bind fails | Check [allocation requirements](overlays/shared-textures.md#prepare-the-allocation), adapter LUID, legacy token, and completion of initial GPU work. Capability queries do not import a texture. |
| shared-texture source stays `AWAITING_TEXTURE` | This state belongs to expert unbound creation. Prefer `createSharedTexture(spec, texture)`; otherwise complete and bind the texture or close the unused source. |
| Pixel acquisition returns no frame | This is the optional direct-write path. Prefer managed `submit` to retain the latest image or apply FIFO backpressure; direct callers finish/discard borrows and avoid a tight retry loop. |
| Tracked submit returns `RESOURCE_EXHAUSTED` | Close unused receipt handles, including completed ones. The capability report gives the limit. With latest-image delivery, failure can occur after the previous pending image was replaced; see [publication tracking](overlays/pixel-buffers.md#track-publication-when-needed). |
| Pixel `poll`, acquire, or try-submit stalls briefly | These calls can wait for shared metadata even though they do not wait for publication or slot capacity. See [timeout effects](reference/contracts.md#timeouts-and-session-loss) before calling them on a latency-sensitive thread. |

### Separate source acceptance from display

1. Confirm a compatible game is running and its overlay is enabled in Overwolf.
2. For pixel-buffer submission, provide a complete straight-alpha BGRA8 image
   with the correct source stride. Direct-write callers use the returned stride.
   Check that visible pixels have nonzero alpha; see [image format and clearing](overlays/pixel-buffers.md#draw-a-complete-image).
3. Check physical-pixel position and size. Close and recreate to move or resize.
4. For shared-texture binding, complete initial GPU work before binding and keep the
   allocation unchanged and alive through overlay close.

## Handle lost resources and cleanup errors

Use the resource's owning session and consume its handle only once:

| Symptom | What to check |
| --- | --- |
| `AGENT_LOST` or `DEVICE_LOST` | Stop using resources created by the lost attachment. Detach/close the context and create new resources after a successful reattach. Old handles do not migrate. |
| Explicit resource close reports an error | Overlay and subscription handles are still consumed. Do not retry them. Preserve the returned error. C++ destructors do not throw and discard cleanup errors; explicit close is required when the caller needs the result. The Agent continues deferred cleanup. |

<a id="changing-native-builds"></a>
## Switch native SDK builds

Client/Agent compatibility uses the SDK version from `version.txt` and a
64-bit build number. Local builds use build number `0` unless configured
otherwise with `-DOWH_BUILD_NUMBER=BUILD_NUMBER`; CI builds use their workflow run
ID. If a resident Agent has a
different pair, restart Overwolf so the matching bundled Agent is loaded.
The Agent can remain resident after the application detaches. A Java class loader can load
only one canonical DLL pair; start a new application process to switch builds.

Replace `BUILD_NUMBER` with the unsigned 64-bit build number assigned to your
native build. It is separate from the release version in `version.txt`.
