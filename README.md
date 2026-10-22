# Overwolf Hook

Overwolf Hook lets your desktop application read the game state and events that
Overwolf collects, without building, installing or running a separate Overwolf
app. Connect your own tools to that data, record it for analysis, or receive game
events in an existing C, C++, Java or Kotlin application.

**Requirements:** Windows x64, the x64 Visual C++ Redistributable, and Overwolf
running with the relevant game's provider active. The library reads existing
data; it does not start providers or enable their features. Java/Kotlin require
**Java 25**.

**Experimental:** access depends on private Overwolf transports. Unsupported
layouts fail explicitly; unfamiliar consumer builds produce a compatibility
warning. The blocking receive API and optional Kotlin API require **v0.5.0-beta.1 or
newer**; v0.4.0-beta.1 provides the Java SDK with polling.

- [Native C/C++](#native-c-and-c)
- [Java and Kotlin](#java-and-kotlin)
- [How the data fits together](#how-the-data-fits-together)
- [Command-line tools and recordings](#command-line-tools-and-recordings)
- [Compatibility and limits](#compatibility-and-limits)
- [Contributing and source builds](CONTRIBUTING.md)

## Native C and C++

Download the Windows x64 ZIP from [Releases](https://github.com/SteveOberst/Overwolf-Hook/releases)
and compare it with that release's `SHA256SUMS`. Extract it to `out/install` for
the examples below. It includes the DLLs, command-line tools, headers and CMake
targets. Source builds are described in [CONTRIBUTING.md](CONTRIBUTING.md#build-and-test).

List available source channels without installing a hook:

```powershell
./out/install/bin/owh_info.exe list
```

Channels may be absent when no relevant provider is active. Their presence alone
does not establish a current match or fresh data.

To link the C ABI from C or C++, add the extracted package to `CMAKE_PREFIX_PATH`:

```cmake
find_package(OverwolfHook CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE OverwolfHook::Client)
```

### Read state

Discover source channels with [discovery.h](include/overwolf_hook/discovery.h).
For a channel with `OWH_CHANNEL_INFO`, [state.h](include/overwolf_hook/state.h)
copies its stored fields. This excerpt reads and releases one snapshot:

```c
owh_state* state = NULL;
owh_result result = owh_state_read(OWH_ABI_VERSION, source_channel, 100, &state);
if (result.status == OWH_OK) {
    printf("fields=%u\n", owh_state_count(state));
    owh_state_close(state);
}
```

The timeout is in milliseconds. Returned names and value bytes belong to `state`
until close. A failed read is not an empty state. Complete, buildable examples
with error handling: [discovery](examples/c/list_channels.c) and
[state reads](examples/c/read_state.c).

### Receive events

Start the controller with the PID of the main `Overwolf.exe`, not a game process
or helper. Supply an absolute capture-DLL path:

```powershell
Get-Process Overwolf | Select-Object Id
./out/install/bin/owh_probe.exe stream <PID> "$PWD/out/install/bin/owh_capture.dll" all --stop-file "$PWD/capture.STOP"
```

The controller prints a **bridge channel** for your reader. `all` includes later
source services; a discovered source channel can replace it. The stop file must
not already exist. Include [probe.h](include/overwolf_hook/probe.h) for the raw
frame layout. Reception after opening a reader looks like this:

```c
owh_probe_frame frame = {0};
owh_record record = {0};
owh_result result = owh_reader_receive(reader, &frame, sizeof(frame), OWH_WAIT_INFINITE, &record);
if (result.status == OWH_OK) {
    printf("sequence=%llu lost=%llu\n",
        (unsigned long long)record.sequence, (unsigned long long)record.lost_before);
}
```

See the [complete reader](examples/c/read_events.c) for opening, looping and
errors. `owh_reader_receive` waits on Windows notifications, not a polling timer.
An explicit millisecond timeout returns `OWH_TIMEOUT` without consuming a record.
`OWH_CLOSED` means retained records have drained after producer closure or exit.

Each reader owns its cursor. `OWH_RETAINED` starts at the oldest available record;
`OWH_NEW_ONLY` starts at reader open. `OWH_BUFFER_TOO_SMALL` reports the required
size without consuming data. To stop a blocked reader, call `owh_reader_cancel`,
join its receiving thread, then close it. Serialize reads on one reader; do not
race close with any operation. `owh_reader_next` remains available for polling.

Ctrl+C/Ctrl+Break or creating the stop file stops the controller. Closing a reader
only detaches that reader. For programmatic startup, `--ready-file <new path>`
creates a marker after the bridge is ready and its channel is flushed to stdout.
The caller owns removal of control files; the JVM SDK handles them automatically.
The `owh_probe_start/stop/query` exports are in-host control, not remote injection
functions for the caller's process.

## Java and Kotlin

The Java SDK bundles its matching native binaries and uses generated FFM
bindings. It needs no JNI code, jextract installation or third-party JVM runtime
library. The optional Kotlin API adds Duration overloads and coroutine Flows;
it reuses the Java SDK's values and native lifetime handling.

### Install with Gradle

GitHub release assets are available as Ivy dependencies. Add the restricted
repository to `settings.gradle.kts` alongside your existing repositories:

```kotlin
dependencyResolutionManagement {
    repositories {
        mavenCentral()
        exclusiveContent {
            forRepository {
                ivy {
                    url = uri("https://github.com/SteveOberst/Overwolf-Hook/releases/download")
                    patternLayout {
                        artifact("v[revision]/[artifact]-[revision](-[classifier]).[ext]")
                        ivy("v[revision]/[module]-[revision].ivy.xml")
                    }
                    metadataSources { ivyDescriptor(); artifact() }
                }
            }
            filter {
                includeModule("dev.sxlver.native", "overwolf-hook")
                includeModule("dev.sxlver.native", "overwolf-hook-kotlin")
            }
        }
    }
}
```

Set `overwolfHookVersion` in `gradle.properties` to the release you use. In
`build.gradle.kts`, choose the Java SDK or the optional Kotlin API:

```kotlin
val version = providers.gradleProperty("overwolfHookVersion").get()
dependencies {
    implementation("dev.sxlver.native:overwolf-hook:$version:windows-x64@jar")
    // Kotlin alternative; includes the SDK, Kotlin stdlib and coroutines transitively:
    // implementation("dev.sxlver.native:overwolf-hook-kotlin:$version")
}
tasks.withType<JavaExec>().configureEach {
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}
```

Gradle downloads and caches the dependency. No manual JAR/DLL copying or custom
extraction task is needed. The native runtime remains Windows x64.
For Application-plugin distributions, also set
`applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")` inside
`application { }`, as in the [example build](examples/kotlin/build.gradle.kts).
Launchers outside Gradle must pass the same JVM flag.

### Java

Read stored fields with the [Java example](examples/java/ReadState.java):

```java
var hook = OverwolfHook.open();
for (var channel : hook.channels()) {
    if (!channel.info()) continue;
    for (var field : hook.readState(channel.id()).fields()) {
        System.out.println(field.categoryText() + "." + field.keyText());
    }
}
```

`capture()` selects the single running Overwolf host. `findHost()` returns an
optional host, and `capture(pid)` selects it explicitly. Blocking reception can
be bounded by a timeout:

```java
try (var capture = hook.capture()) {
    switch (capture.receive(java.time.Duration.ofSeconds(10))) {
        case EventReader.Event event -> System.out.println(event.sequence());
        case EventReader.Status status -> System.out.println(status);
    }
}
```

See [the complete receiving loop](examples/java/ReceiveEvents.java). Use an I/O
thread. `receive()` has no deadline; `receive(Duration)` may return `TIMEOUT`.
`cancelReceive()` wakes the primary receiver; close cancels before freeing native
memory and stops the controller. Java thread interruption alone does not cancel
a native wait. `capture.openReader()` creates another independently owned cursor.

### Kotlin

The [Kotlin project](examples/kotlin) uses the optional dependency. Its
[Flow example](examples/kotlin/src/main/kotlin/ReceiveEvents.kt) owns the capture
for the duration of collection. It runs until the producer closes or the caller
cancels the coroutine:

```kotlin
import dev.sxlver.overwolfhook.OverwolfHook
import dev.sxlver.overwolfhook.kotlin.captureEvents
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.runBlocking

fun main() = runBlocking {
    OverwolfHook.open().captureEvents().collect { event ->
        println("channel=${event.sourceChannel} sequence=${event.sequence}")
    }
}
```

Add a timeout only when the application needs one; the SDK does not impose a
duration.

`captureEvents()` is cold: each collection starts its own capture on an I/O
thread. For an existing capture, import `dev.sxlver.overwolfhook.kotlin.events`
and collect `capture.events` instead. Each collector gets an independent retained
reader; ending one does not stop other readers or the caller-owned capture.
See [sharing a capture](examples/kotlin/src/main/kotlin/CaptureReaders.kt).

Kotlin timeout overloads use `kotlin.time.Duration`:

```kotlin
import dev.sxlver.overwolfhook.kotlin.receive
import kotlin.time.Duration.Companion.milliseconds

val result = capture.receive(500.milliseconds) // blocking; use an I/O thread
```

Finite receive timeouts must be whole milliseconds from 0 to 4,294,967,294;
`Duration.INFINITE` is allowed for receives. `readState(channel, timeout)` has
its existing 0..1000 ms lock-timeout bound.

Flows preserve native failures and overwrite counts. They cancel and join their
reader before freeing it. There is no extra event buffer by default; a slow
collector can still overrun the native ring. Cancellation can discard in-flight
data. A quiet stream does not prove that no gameplay events occurred: the active
provider must support and produce those events. Java records already provide
Kotlin properties such as `event.sequence`; there are no parallel Kotlin
event/state models.

## How the data fits together

Overwolf's game-events provider (GEP) produces stored **state** and discrete
**events**. Available features depend on the game; start with
[Overwolf's API reference](https://dev.overwolf.com/ow-native/reference/games/events/)
and the [game-specific documentation](#games-and-data-availability).

```text
Game provider -> Overwolf shared memory
                  +-- stored state -> passive read -> your application
                  +-- event consumer -> hook -> separate bridge -> your application
```

A **source channel** identifies an existing Overwolf transport, not a public game
ID. A **bridge channel** identifies our capture stream. State reads copy InfoDB
fields at that moment and may miss intermediate changes. Events are copied after
the original consumer reads them. The hook runs in `Overwolf.exe`, not the game.

Raw bytes and timestamps are retained; the library does not promise the public
API's JSON layout or assign data to a match. `producerTicks` is not UTC/game time.
Failed reads, successful empty state and unwritten fields remain distinct.

## Command-line tools and recordings

```powershell
./out/install/bin/owh_info.exe read <source-channel>
./out/install/bin/owh_info.exe watch 0 "$PWD/state.jsonl" --stop-file "$PWD/capture.STOP"
./out/install/bin/owh_probe.exe <PID> "$PWD/out/install/bin/owh_capture.dll" 0 "$PWD/events.bin" all --stop-file "$PWD/capture.STOP"
python tools/inspect_capture.py events.bin
```

`owh_info watch` polls state every 250 ms and retains changed raw fields,
discovery results and failures. It writes `owh-infodb-jsonl-1` with original
name/value bytes as hex, producer ticks, reception QPC and UTC FILETIME.
It is separate from signalled event reception.

Both recorders accept `0` until stopped or a positive duration in seconds. They
never overwrite output. The directory must exist; files grow until stopped or a
reported write failure. Keep stdout/stderr for compatibility warnings, timing
anchors and counters. Remove the stop file before another run.

In the SDK, `hook.recordEvents(newPath)` starts an explicit full recording;
`capture.diagnostics()` exposes controller details until close. Capture shutdown
disables interception, drains active work and releases session handles. Capture
and consumer DLLs and the disabled trampoline stay resident until Overwolf exits
so outstanding calls can return safely; stop is not DLL unloading. A Windows job
owns the controller, and the hook watches its owner's lifetime. There is no
implicit restart in a replacement host.

## Compatibility and limits

The event hook targets the classic `OWGameEventsConsumer.dll` transport, locating
the reader by unique code signatures and checking its entry bytes. Missing or
ambiguous signatures and changed entry bytes are hard errors; existing hooks
are not overwritten. An unknown or unreadable DLL hash is a warning. Matching
signatures do not prove private layout compatibility; an untested build may
crash the host. See the [reviewed profile](CONTRIBUTING.md#consumer-compatibility).

- Each bridge has at most 64 independent readers and a bounded ring. Overwrites
  are reported per reader. Upstream loss cannot be recovered; delivery is not
  guaranteed lossless. Contention and rejected capture attempts remain explicit.
- Use matching client/capture binaries. The private bridge is wire version 2;
  old mappings are rejected. The public C ABI and raw recording frames remain
  ABI 1. Event recordings are not a stable archival format.
- Shared objects use the current Windows session and default process ACLs; they
  are not a hostile-peer security boundary. Review raw captures before sharing.
- SDK events and state fields own Java copies of their bytes. Native field
  storage belongs to its read result until close. `OverwolfHook.open()` loads
  one matching native build per SDK classloader.

### Games and data availability

These are upstream documentation examples, not per-game certification by this
library. Availability depends on the provider and supported transport.

| Game | Overwolf documentation |
| --- | --- |
| League of Legends | [Game values and events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/league-of-legends/) |
| Fortnite | [Game values and events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/fortnite/) |
| Valorant | [Game values and events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/valorant/) |
| Apex Legends | [Game values and events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/apex-legends/) |
| Counter-Strike 2 | [Game values and events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/counter-strike-2/) |
| … | [All supported games](https://dev.overwolf.com/ow-native/live-game-data-gep/game-events-status-health/) |

## Help and contributing

[Open an issue](https://github.com/SteveOberst/Overwolf-Hook/issues) with versions,
failure status and a minimal reproduction. See [CONTRIBUTING.md](CONTRIBUTING.md)
for source builds, tests and releases, and [CHANGELOG.md](CHANGELOG.md) for changes.

## License

[MIT](LICENSE). MinHook's BSD/decoder notices are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Not affiliated with Overwolf or
Riot Games.
