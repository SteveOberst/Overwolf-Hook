# Overwolf Hook

Read Overwolf game data from your own application without building or running
an Overwolf app. The Windows x64 library provides native C/C++ access and a
Java 25 SDK usable from Java and Kotlin.

Overwolf must be running with the relevant game's provider active. This library
reads existing provider data; it does not start providers or enable their features.

**Experimental:** interception validates reader signatures and entry bytes;
unreviewed consumer builds produce a compatibility warning. State reads support
the classic InfoDB layout. See [compatibility and limits](#compatibility-and-limits).

## How the data fits together

Overwolf's game-events provider (GEP) produces two kinds of information:

- **State:** values such as the current level or match phase. Overwolf's public
  API exposes current info and info updates.
- **Events:** individual occurrences such as a kill. These arrive separately
  from state updates.

The available features and their meanings depend on the game. Start with
[Overwolf's API reference](https://dev.overwolf.com/ow-native/reference/games/events/)
and the [game-specific documentation](#games-and-data-availability) to understand
the data you want to use.

Overwolf Hook accesses the underlying transport, rather than implementing that
JavaScript API:

```text
Game provider -> Overwolf shared memory
                  |
                  +-- stored state -> passive read -> your application
                  |
                  +-- event consumer -> hook -> separate bridge -> your application
```

For **state**, the library copies existing category/key/value entries from
Overwolf's InfoDB. No hook or event controller is needed. Reads give you the
stored values at that moment; polling may miss intermediate changes.

For **events**, a controller loads the capture DLL into `Overwolf.exe`, not the
game process. The hook copies records after the original consumer has read them
and publishes them to a separate bridge. Your readers have independent cursors:
they do not remove records from Overwolf's queue or from each other.

Two identifiers appear in the API:

- A **source channel** identifies an existing Overwolf transport. Discover it;
  it is not a public Overwolf game ID, and no game-ID conversion is assumed.
- A **bridge channel** identifies the separate event stream created by a capture.
  The native controller prints it; the JVM SDK connects to it automatically.

The library preserves raw values and event buffers. It does not turn them into
game-specific objects or promise that their layout matches the public API's JSON.
Mapping presence alone does not establish an active match or fresh data; retained
state may outlive a game session. Keep source lifetime and game semantics in your
application.

## Java and Kotlin

The JVM JAR bundles the matching native binaries and a small API built on
generated FFM bindings. Applications need Java 25 and native access enabled;
they do not need jextract, JNI code or runtime dependencies.

The JVM API requires **v0.4.0-beta.1 or newer**. Download the JAR release asset or
[build it locally](#build-the-jvm-artifact-locally).
See [Gradle setup](#fetch-from-gradle) for release-asset dependencies.

### Find channels and read state

Import `dev.sxlver.overwolfhook.OverwolfHook`. Discover the channels, select those
with state storage, and read their fields. Both operations perform explicit I/O
and can fail independently.

Java:

```java
var hook = OverwolfHook.open();
for (var channel : hook.channels()) {
    if (!channel.info()) continue;
    var state = hook.readState(channel.id());
    for (var field : state.fields()) {
        System.out.println(field.categoryText() + "." + field.keyText());
        System.out.println(java.util.HexFormat.of().formatHex(field.bytes()));
    }
}
```

Kotlin:

```kotlin
val hook = OverwolfHook.open()
for (channel in hook.channels().filter { it.info() }) {
    val state = hook.readState(channel.id())
    for (field in state.fields()) {
        println("${field.categoryText()}.${field.keyText()}")
        println(java.util.HexFormat.of().formatHex(field.bytes()))
    }
}
```

`State` owns Java copies of the fields. Original name bytes remain available
alongside the UTF-8 convenience methods. Values can be binary or contain NULs;
`producerTicks()` is the original producer timestamp, not UTC or game time.

The complete [Java](examples/java/ReadState.java) and
[Kotlin](examples/kotlin/src/main/kotlin/ReadState.kt) examples handle
`NativeFailure`, which retains the native status and Win32 error. A failed read
is not an empty state. A successful empty state, an unwritten field and an
unavailable channel are different outcomes.

### Receive events and close the capture

`capture()` selects the single running Overwolf host. Use `capture(pid)` when
selecting it explicitly, or `findHost()` to discover an optional host first.
Import `dev.sxlver.overwolfhook.EventReader` as well.
These loops receive for ten seconds without writing a recording:

Java ([complete example](examples/java/ReceiveEvents.java)):

```java
try (var capture = hook.capture()) {
    long started = System.nanoTime();
    while (System.nanoTime() - started < 10_000_000_000L) {
        switch (capture.poll()) {
            case EventReader.Event event -> System.out.printf(
                "channel=%d sequence=%d bytes=%d%n",
                event.sourceChannel(), event.sequence(), event.bytes().length);
            case EventReader.Status status -> {
                if (status == EventReader.Status.CLOSED) return;
                Thread.sleep(2);
            }
        }
    }
}
```

Kotlin ([complete example](examples/kotlin/src/main/kotlin/ReceiveEvents.kt)):

```kotlin
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeSource

hook.capture().use { capture ->
    val started = TimeSource.Monotonic.markNow()
    while (started.elapsedNow() < 10.seconds) {
        when (val result = capture.poll()) {
            is EventReader.Event -> println(
                "channel=${result.sourceChannel()} sequence=${result.sequence()} bytes=${result.bytes().size}")
            EventReader.Status.CLOSED -> return
            EventReader.Status.NO_DATA, EventReader.Status.BUSY -> Thread.sleep(2)
        }
    }
}
```

Use an I/O thread. Polling is explicit; there is no hidden retry or subscription
loop. `NO_DATA` and `BUSY` are polling outcomes, not evidence that no gameplay
event occurred. Each event retains its raw bytes, source channel, sequence,
capture QPC timestamp and per-reader overwrite count (`lostBefore()`).

Closing the capture requests a graceful stop and releases its reader and
controller. A Windows job owns the controller so it cannot survive abrupt JVM
termination; the native hook also watches that controller's lifetime.
`capture.diagnostics()` exposes startup/profile errors and timing information
until close. For an explicit full recording, use `hook.recordEvents(newPath)`;
`recordEvents(newPath, pid)` selects the host explicitly. The caller owns that
file and its disk usage.

`OverwolfHook.open()` extracts verified binaries into a content-addressed
temporary directory and loads the client DLL once. `open(binDirectory)` selects
an explicit matching binary directory. Only one native build is loaded per SDK
classloader. Generated bindings remain available in
`dev.sxlver.overwolfhook.bindings`; the [raw FFM example](examples/java/FfmRoundtrip.java)
illustrates that lower-level API, which normal callers do not need.

## Native C and C++

The native package exposes a C ABI. Link it from either language using the
installed CMake target:

```cmake
find_package(OverwolfHook CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE OverwolfHook::Client)
```

### Find channels and read state

Use [discovery.h](include/overwolf_hook/discovery.h) to enumerate source channels:

```c
owh_channels* channels = NULL;
owh_result result = owh_channels_discover(OWH_ABI_VERSION, &channels);
if (result.status == OWH_OK) {
    for (uint32_t i = 0; i < owh_channels_count(channels); ++i) {
        owh_source_channel channel = {0};
        if (owh_channels_at(channels, i, &channel).status == OWH_OK) {
            printf("channel=%u kinds=%u\n", channel.source_channel, channel.kinds);
        }
    }
    owh_channels_close(channels);
}
```

Kind flags are `OWH_CHANNEL_INFO` and `OWH_CHANNEL_EVENTS`. Discovery enumerates
existing mappings in your Windows session without guessing IDs or opening a
foreign process. It retains no provider handles; repeat discovery as sources
appear and disappear. Discovery success does not guarantee a later read succeeds.

For an info channel, use [state.h](include/overwolf_hook/state.h):

```c
owh_state* state = NULL;
owh_result result = owh_state_read(OWH_ABI_VERSION, source_channel, 100, &state);
if (result.status == OWH_OK) {
    for (uint32_t i = 0; i < owh_state_count(state); ++i) {
        owh_state_field field = {0};
        if (owh_state_at(state, i, &field).status == OWH_OK) {
            printf("%s.%s: %u bytes\n", field.category, field.key, field.size);
        }
    }
    owh_state_close(state);
}
```

The third argument is the lock timeout in milliseconds. The reader copies at
most 4 MiB under the existing InfoDB mutex and releases provider handles before
decoding. Returned names and value bytes belong to `state` until close; copy
anything you retain. Non-OK statuses preserve failures such as unavailable,
busy or corrupt storage rather than returning an empty successful read.
Complete examples: [channel discovery](examples/c/list_channels.c) and
[state reads](examples/c/read_state.c).

### Receive events and stop the controller

Unlike the JVM facade, the native reader does not start a remote capture.
Start the bundled controller with the PID of `Overwolf.exe`, not a helper or
game process, and an absolute DLL path:

```powershell
Get-Process Overwolf | Select-Object Id
./out/install/bin/owh_probe.exe stream <PID> "$PWD/out/install/bin/owh_capture.dll" all --stop-file "$PWD/capture.STOP"
```

It prints the **bridge channel** to pass to your reader. `all` captures every
source service the reviewed consumer reads, including later services. A specific
discovered source channel can replace `all`. The stop file must not already exist.

Include [probe.h](include/overwolf_hook/probe.h) for the frame layout, then open
the bridge with `owh_reader_open`. The full
[C reader](examples/c/read_events.c) handles a receiving loop and errors:

```c
owh_reader* reader = NULL;
owh_result result = owh_reader_open(OWH_ABI_VERSION, bridge_channel, OWH_RETAINED, &reader);
if (result.status == OWH_OK) {
    owh_probe_frame frame = {0};
    owh_record record = {0};
    result = owh_reader_next(reader, &frame, sizeof(frame), &record);
    if (result.status == OWH_OK) {
        printf("channel=%u sequence=%llu lost=%llu\n", frame.source_channel,
            (unsigned long long)record.sequence, (unsigned long long)record.lost_before);
    }
    owh_reader_close(reader);
}
```

This snippet polls once. For a continuous reader, retry `OWH_NO_DATA` and
`OWH_BUSY`, drain until `OWH_CLOSED`, and report other failures. `OWH_RETAINED`
starts at the oldest available record; `OWH_NEW_ONLY` starts at reader open.
`OWH_BUFFER_TOO_SMALL` reports the required size without consuming the record.
Serialize operations on each reader and do not race them with close.

Stop the controller with Ctrl+C/Ctrl+Break or create its stop file:

```powershell
New-Item -ItemType File "$PWD/capture.STOP"
```

Closing a reader only detaches that reader; it does not stop the controller.
The exported `owh_probe_start/stop/query` functions are **in-host control**:
calling them in your application does not install a hook into another process.

Build the complete native examples against your installation:

```powershell
cmake -S examples/c -B build/example -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$PWD/out/install"
cmake --build build/example --config Release
./build/example/Release/list_channels.exe
./build/example/Release/read_state.exe <source-channel>
./build/example/Release/read_events.exe <bridge-channel>
```

## Capture lifetime

Stopping disables interception, drains active capture work and closes the
completed session's writer, worker and synchronization handles. The capture and
consumer DLLs and disabled trampoline remain resident until Overwolf exits;
outstanding calls must still be able to return through valid code. Stop is not
a DLL-unload operation.

The original call, return value and `LastError` are preserved. A controller exit
requests hook shutdown. A host exit ends capture; no hook is silently installed
into a replacement host. Start a new capture for a new Overwolf process.

## Command-line tools and recordings

`owh_info` provides discovery and passive state reads without installing a hook:

```powershell
./out/install/bin/owh_info.exe list
./out/install/bin/owh_info.exe read <source-channel>
./out/install/bin/owh_info.exe watch 0 "$PWD/state.jsonl" --stop-file "$PWD/capture.STOP"
```

`watch` polls every 250 ms, retaining changed raw fields, timestamps, errors,
channel disappearance/reappearance and discovery results. The JSONL format
`owh-infodb-jsonl-1` retains names/values as hex, producer ticks, reception QPC
and UTC FILETIME. A clean exit appends `stopped_filetime_utc`. Intermediate
updates may collapse; timestamps are not converted into game time or assigned
to a match.

For events, omit `stream` and supply a duration plus a new output file:

```powershell
./out/install/bin/owh_probe.exe <PID> "$PWD/out/install/bin/owh_capture.dll" 0 "$PWD/events.bin" all --stop-file "$PWD/capture.STOP"
python tools/inspect_capture.py events.bin
```

Both recorders accept `0` for capture until stopped or a positive duration in
seconds. They never overwrite output or delete caller files. Output directories
must already exist; recordings grow until stopped or a reported write failure.
Keep controller stdout/stderr for profile errors, timing anchors and counters.
The same initially absent stop file can stop both recorders. Remove it yourself
before another run. Event records are drained on graceful stop and host exit;
final provider diagnostics are unavailable after host exit.

The broader native API requires v0.2.0-beta.1+, discovery and stoppable recording
require v0.3.0-beta.1+, and the JVM SDK and live-only `stream` command require v0.4.0-beta.1+.

## Build

Requires Windows x64, Visual Studio 2022 or newer with C++ tools/Windows SDK,
and CMake 3.25+. Python 3 is required for tests and the recording inspector;
Java 25 is needed only for JVM bindings and examples; native-only builds do not require it.

```powershell
git clone https://github.com/SteveOberst/Overwolf-Hook.git
cd Overwolf-Hook
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix out/install
```

For Visual Studio 2026 use `-G "Visual Studio 18 2026"`, or the included
`windows-x64` configure preset and `release` build/test presets.
MinHook is fetched automatically through CMake FetchContent with a pinned hash.
No manual ZIP, checkout, Overwolf SDK or running game is needed to build.

The installed native package contains `owh_capture.dll`, `owh_client.dll`, `owh_probe.exe`, `owh_info.exe`,
C headers, CMake targets and license notices. The DLLs use the Microsoft C++
runtime; install the x64 Visual C++ Redistributable if it is missing.

### Build the JVM artifact locally

Set `JAVA_HOME` to JDK 25, then enable the optional CMake feature:

```powershell
cmake -S . -B build -DOWH_JVM_BINDINGS=ON
cmake --build build --config Release
$version = (Get-Content version.txt).Trim()
$jar = (Resolve-Path "build/jvm/Release/overwolf-hook-$version-windows-x64.jar").Path
java --enable-native-access=ALL-UNNAMED -cp "$jar" examples/java/ReadState.java
./examples/kotlin/gradlew.bat -p examples/kotlin run "-PoverwolfHookJar=$jar"
```

The builder downloads the checksum-pinned generator from [jvm/toolchain.json](jvm/toolchain.json),
filters its symbol inventory to the library's public `owh_*`/`OWH_*` declarations,
then compiles the generated bindings, facade and examples. No generated Java
sources are checked in. `OWH_JEXTRACT` can select a local jextract 25 executable.
Native-only builds leave this feature off. CI enables it and tests the resulting
JAR against owned IPC fixtures; its optional owned-host test also covers capture,
cross-thread close, JVM death and restart.

### Fetch from Gradle

Both artifacts can be Gradle Ivy dependencies. These coordinates describe GitHub
release assets, not a Maven publication.
In `settings.gradle.kts`, add a repository restricted to this one module:

```kotlin
dependencyResolutionManagement {
    repositories {
        exclusiveContent {
            forRepository {
                ivy {
                    url = uri("https://github.com/SteveOberst/Overwolf-Hook/releases/download")
                    patternLayout { artifact("v[revision]/[artifact]-[revision]-[classifier].[ext]") }
                    metadataSources { artifact() }
                }
            }
            filter { includeModule("dev.sxlver.native", "overwolf-hook") }
        }
    }
}
```

For the JVM API, select a release containing the JAR and add it directly:

```kotlin
val overwolfHookVersion = providers.gradleProperty("overwolfHookVersion").get()
dependencies {
    implementation("dev.sxlver.native:overwolf-hook:$overwolfHookVersion:windows-x64@jar")
}
tasks.withType<JavaExec>().configureEach {
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}
```

Set `overwolfHookVersion` in your project's `gradle.properties`. The JAR already
contains its native files and license notices; consumers do not need an extraction
task. For custom native-only packaging instead, keep the ZIP separate:

```kotlin
val overwolfHook by configurations.creating {
    isCanBeConsumed = false
    isTransitive = false
}
dependencies {
    add(overwolfHook.name, "dev.sxlver.native:overwolf-hook:0.3.0-beta.1") {
        artifact { classifier = "windows-x64"; type = "zip"; extension = "zip" }
    }
}
```

Gradle retrieves and caches the native ZIP. Your native-only packaging task needs to verify
its SHA-256 against the release's `SHA256SUMS`, extract the required binaries
and retain their license notices. Use the resulting client DLL path for FFM;
do not add the ZIP to the JVM classpath.

## Compatibility and limits

The reference profile in [profile.hpp](src/capture/overwolf/profile.hpp) was reviewed
against `OWGameEventsConsumer.dll` **2.0.0.5**, from Overwolf **0.311.1.1**:

```text
SHA-256: 52c417c374c9331b6bb8d13b784cb1ab51ff5f975ff1c51b8ee102a4d16e7426
```

The reader is located by two body signatures inside executable PE x64 function
ranges, using the image's exception table. RIP-relative address bytes are masked;
there is no fixed reader address or RVA fallback. Exactly one function must match,
and its entry bytes must match before interception is enabled, including restart.
Missing or ambiguous matches, changed entry bytes and invalid PE ranges are hard
errors. A pre-existing hook is not overwritten.

An unknown SHA-256, or inability to check it, is **advisory**: structurally matching
code may run, with a warning that compatibility is untested and capture could
crash the host. The CLI prints that warning; JVM callers can read it through
`capture.diagnostics()`, and in-host C callers through `owh_probe_statistics.warnings`.
A signature match does not prove that every private ABI/layout assumption survived
an update. Changes to those assumptions require a profile review; broader compiler
changes may also require new signatures.

The hook is game-independent at the event-channel boundary. It captures services
that the matching classic consumer reads; it does not support other Overwolf
transports implicitly.

### Games and data availability

Overwolf documents game values and events for the following popular examples.
Availability depends on the game's provider and supported transport; this list
is an upstream reference, not a certification of every game with this library.

| Game              | Overwolf documentation                                                                                                           |
|-------------------|----------------------------------------------------------------------------------------------------------------------------------|
| League of Legends | [Game values and events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/league-of-legends/)               |
| Fortnite          | [Game values and events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/fortnite/)                        |
| Apex Legends      | [Game values and events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/apex-legends/)                    |
| Counter-Strike 2  | [Game values and events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/counter-strike-2/)                |
| …                 | [All supported games and current availability](https://dev.overwolf.com/ow-native/live-game-data-gep/game-events-status-health/) |

### Transport limits

- The event hook captures discrete events; `owh_state_read` separately copies
  persistent InfoDB values. Neither claims every game state field or a canonical
  per-game schema. Both retain original values for the caller to interpret.
- Bridge rings have sequence numbers and per-reader overwrite counts. Producer loss,
  Overwolf's original ring overwrites and its consumer age filter are upstream
  and cannot be recovered here. No lossless-delivery guarantee is made.
- Capture publishes without waiting for readers. Contention returns `OWH_BUSY`;
  rejected captures and malformed ranges have separate counters.
- Shared objects use the current Windows session and default process ACLs. They
  are not a hostile-peer security boundary. Raw records can contain identifiers;
  review captures before sharing them.
- The experimental recording is a sequence of native x64 `owh_record` plus
  `owh_probe_frame` structs (ABI 1), not a stable archival format.

## Tests and releases

CI runs transport, discovery, state recording, stop/lifecycle and MinHook tests
in Debug/Release, compiles the installed C examples, and runs the JVM examples.
Signature tests cover relocated functions, address displacements, missing and
ambiguous matches, changed layouts/entry bytes and malformed images.
Recorder tests retain opaque bytes and check errors/removal/reappearance and
stop behavior. The inspector streams large recordings without retaining every
sequence number in memory. No test injects into a running third-party process.
Optional owned-host tests execute the real consumer reader against synthetic
layout storage and exercise the controller through stop/restart, console stop,
hard controller death and host exit:

```powershell
cmake -S . -B build "-DOWH_REVIEWED_CONSUMER=C:/path/to/OWGameEventsConsumer.dll"
```

These optional tests require your local reviewed DLL; it is never downloaded or
redistributed. They also verify a changed file hash with unchanged code: capture
continues and reports the warning. Synthetic inputs are not real-match validation.

Enable the JVM tests with `-DOWH_FFM_TESTS=ON` and, if necessary,
`-DJava_JAVA_EXECUTABLE=C:/path/to/java.exe`. Set `OWH_OVERWOLF_PROBE=OFF` to
build the bridge/state/discovery APIs and state CLI without the event hook;
also disable `OWH_HOOK_TESTS` to avoid MinHook completely. Use `BUILD_TESTING=OFF`
for a library-only build without test prerequisites.

Use [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/):
`fix:` for fixes, `feat:` for features, and `!` / `BREAKING CHANGE:` for breaking
changes. Release Please opens a version/changelog PR after green main builds.
Review and merge that PR to create a tagged release; the tested Windows ZIP, JVM JAR and
SHA-256 file are then attached automatically. `version.txt` is the build version;
the release manifest records the last published version.

Run `cpack --config build/CPackConfig.cmake -C Release -B out` for a local ZIP.
For issues, include the tool version, consumer version/hash, failure status and
a minimal reproduction. Do not attach unreviewed raw captures.

## License

[MIT](LICENSE). MinHook's BSD and decoder notices are preserved in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This project is not affiliated
with Overwolf or Riot Games.
