# Overwolf Hook

Read Overwolf game data without building or running an Overwolf app. Inspect
current provider values, capture discrete events, or feed both into your own
tools. Overwolf must still be running with the relevant game's provider active.

The Windows x64 library exposes two operations through a C ABI usable from
C/C++ and Java/Kotlin FFM: passive reads of the classic InfoDB shared memory,
and an event hook that copies records into a separate shared-memory channel.
State reads own their data; event readers have independent cursors.

The broader API and `OverwolfHook` names below require version 0.2.0-beta.1 or newer.
Release 0.1.0-beta.1 contains only the former `OverwolfEventHook` event API.
Channel discovery and the stoppable recorders require version 0.3.0-beta.1 or newer.

**Experimental:** the event hook targets one reviewed consumer build. The state
reader targets one reviewed classic InfoDB layout. Synthetic tests establish
memory ownership and bounds, not complete per-game coverage. The earlier event
hook and Python InfoDB prototype have short live evidence; the new native state
reader still needs live match validation.

## Build

Requires Windows x64, Visual Studio 2022 or newer with C++ tools/Windows SDK,
and CMake 3.25+. Python 3 is required for tests and the recording inspector;
Java 25 is needed only for the optional FFM example/test.

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

The installed package contains `owh_capture.dll`, `owh_client.dll`, `owh_probe.exe`, `owh_info.exe`,
C headers, CMake targets and license notices. The DLLs use the Microsoft C++
runtime; install the x64 Visual C++ Redistributable if it is missing.

## Read current state

Use [state.h](include/overwolf_hook/state.h) to copy the provider's current
categories, keys, original value bytes, reservation sizes and per-key timestamps:

```c
owh_state* state = NULL;
owh_result result = owh_state_read(OWH_ABI_VERSION, source_channel, 100, &state);
if (result.status == OWH_OK) {
    for (uint32_t i = 0; i < owh_state_count(state); ++i) {
        owh_state_field field = {0};
        if (owh_state_at(state, i, &field).status == OWH_OK) {
            /* Consume field.bytes[0..field.size), or copy it before close. */
        }
    }
    owh_state_close(state);
}
```

The reader opens an existing `ow_info_<source_channel>` mapping read-only,
copies at most 4 MiB under its existing mutex, closes all provider handles, then
validates and decodes the copy. It never injects code, initializes an SDK
consumer, or waits on/signals Overwolf's notification events. No event controller
is needed for this operation. Values may contain binary data or embedded NULs;
timestamps are original producer ticks, not UTC. No JSON or per-game semantics
are imposed. Names and bytes belong to `state` until `owh_state_close`.

`OWH_NOT_FOUND`, `OWH_BUSY`, `OWH_CORRUPT` and `OWH_SYSTEM_ERROR` are failures
to obtain a complete state, not empty results. `OWH_OK` with zero fields is a
structurally empty database. A field with timestamp zero is preserved and does
not establish a known gameplay value. A retained value does not prove an active
selection phase or even a live match. Changes between reads may collapse into
the latest value; this is not a change-history API.

Source channels are internal transport IDs, not public game IDs. Readers must
establish the relevant source lifetime; each state read releases every provider
handle before returning. No consumer can infer a current match from a retained
mapping alone.

The installed [C state example](examples/c/read_state.c) prints keys and hex bytes:

```powershell
./build/example/Release/read_state.exe <source-channel>
```

## Discover channels

[discovery.h](include/overwolf_hook/discovery.h) lists the exact classic
`ow_info_<id>` and `ow_events_<id>` Section objects in your Windows session.
No ID range is guessed, and no foreign process is opened. The list owns only
IDs and kind flags; it retains no provider object handles. Re-enumeration finds
channels that appear or disappear while your application runs.

```c
owh_channels* channels = NULL;
owh_result result = owh_channels_discover(OWH_ABI_VERSION, &channels);
if (result.status == OWH_OK) {
    for (uint32_t i = 0; i < owh_channels_count(channels); ++i) {
        owh_source_channel channel = {0};
        if (owh_channels_at(channels, i, &channel).status == OWH_OK) {
            /* channel.kinds: OWH_CHANNEL_INFO and/or OWH_CHANNEL_EVENTS */
        }
    }
    owh_channels_close(channels);
}
```

The [C discovery example](examples/c/list_channels.c) prints the list. The
installed CLI can also list channels or read one without installing a hook:

```powershell
./out/install/bin/owh_info.exe list
./out/install/bin/owh_info.exe read <source-channel>
```

Discovery reports **mapping presence**, not read permission, layout compatibility,
an active producer, a public game ID or a live match. Read results remain separate.
Enumeration is not atomic; a source may vanish immediately afterward. A successful
empty list is distinct from a discovery error (for example access denied under a
restricted process token). No administrator elevation is requested automatically.
The implementation dynamically looks up Microsoft's
[NtOpenDirectoryObject](https://learn.microsoft.com/en-us/windows/win32/devnotes/ntopendirectoryobject)
and [NtQueryDirectoryObject](https://learn.microsoft.com/en-us/windows/win32/devnotes/ntquerydirectoryobject);
unavailable functions fail explicitly rather than yielding an empty list.

## Record current state

The state recorder repeatedly discovers InfoDB channels and records changed
values, errors, disappearance and reappearance. Unknown names/values are retained.
It polls every 250 ms; intermediate updates can collapse into the latest value.
There is no delivery guarantee or inferred game-state freshness.

```powershell
./out/install/bin/owh_info.exe watch 0 "$PWD/state.jsonl" --stop-file "$PWD/capture.STOP"
```

Use `0` to run until stopped, or any representable positive duration in seconds.
Ctrl+C/Ctrl+Break or creating the specified stop file requests a clean stop.
The output must be new, and the stop file must not exist at startup. Parent
directories must already exist. Output grows until stop or a reported write
failure; provision disk space for your recording.

The JSONL header identifies `owh-infodb-jsonl-1` and QPC frequency. Each changed
source record includes reception QPC, UTC Windows FILETIME, source channel,
status and original Win32 error. Successful records include complete fields with
`category_hex`, `key_hex`, `value_hex`, original `producer_ticks` and reservation
sizes. Hex preserves non-UTF-8 names and binary payloads. Disappearance uses
`removed: true`; discovery failures use `discovery_status` and clear the comparison
baseline. An error is never an empty successful state. A clean exit appends
`stopped_filetime_utc`; its absence means completion was not confirmed. Timestamp
changes are retained even when value bytes match. Original timestamps are not
converted into game time. State snapshots are not correlated with a particular
match by this tool.

## Capture events

Start Overwolf normally with the game's overlay enabled, then enter a game.
Use the PID of `Overwolf.exe`, not a browser/helper or a game process.

```powershell
Get-Process Overwolf | Select-Object Id
./out/install/bin/owh_probe.exe <PID> "$PWD/out/install/bin/owh_capture.dll" 20 "$PWD/events.bin"
python tools/inspect_capture.py events.bin
```

Replace `<PID>` with the displayed process ID. Use any representable positive
duration in seconds, or `0` for capture until stopped. The output file must be new.
The CLI prints its channel, host process identity, QPC/UTC timing anchor, sample
event names and final counters. Keep its stdout/stderr beside the raw recording.
It stops the hook and drains the reader at the deadline, on Ctrl+C/Ctrl+Break or
when the selected host exits. Keep the DLL path unchanged while Overwolf runs.

An explicit stop file also works for a controller started without an interactive
console. It can be shared with the state recorder:

```powershell
./out/install/bin/owh_probe.exe <PID> "$PWD/out/install/bin/owh_capture.dll" 0 "$PWD/events.bin" all --stop-file "$PWD/capture.STOP"
# In another terminal, run the state-recorder command above.
# Stop both from a third terminal:
New-Item -ItemType File "$PWD/capture.STOP"
```

Each controller requires that stop file to be absent when it starts. Delete it
yourself before a new capture. The tool never deletes caller files. Graceful stop
flushes output and drains forwarded event records. If Overwolf exits, retained
bridge records are drained and final provider diagnostics are explicitly
unavailable. No new hook is silently installed into a replacement process; start
a new controller for a new host. Ordinary match/provider channel changes within
the same host are covered by all-channel capture and repeated state discovery.

By default, capture includes all `ow_events_<channel>` services read by the
reviewed consumer, including channels that appear after capture starts. Every
frame retains its actual `source_channel`, so consumers can separate sources.
An optional last argument restricts capture to one channel:

```powershell
./out/install/bin/owh_probe.exe <PID> "$PWD/out/install/bin/owh_capture.dll" 20 "$PWD/events.bin" 10054261
```

`10054261` is the channel observed in the League test, not its public Overwolf
game ID (`5426`). Channel IDs are internal transport identities; this library
does not assume a universal game-ID-to-channel formula. Omit the argument or
pass `all` to avoid needing one. For in-host C ABI control, set
`owh_probe_options.source_channel` to `OWH_ALL_CHANNELS` or a specific channel.

The controller loads the capture DLL into the selected Overwolf host and invokes
explicit start/stop exports. The hook copies raw records after the original
consumer read returns, before payload transformation. It does not hook the game,
change authorization settings, or wait on Overwolf's notification events.

Capture and consumer DLLs remain resident until Overwolf exits, even after the
hook stops. This avoids unloading code while a callback might still reference it.
The original call, return value and `LastError` are retained. The native worker
watches the controller process and requests shutdown if it exits. Owned-host
tests cover abrupt controller death, entry restoration and subsequent restart;
this does not certify every real-host shutdown circumstance.

## Use from C/C++

The installed CMake package is named `OverwolfHook`:

```cmake
find_package(OverwolfHook CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE OverwolfHook::Client)
```

Use [bridge.h](include/overwolf_hook/bridge.h) for the client/transport API and
[probe.h](include/overwolf_hook/probe.h) for capture frames and in-host control exports.
The control exports operate inside the capture host; calling them in your own
process does not install a remote hook. Use the CLI to start a capture.

The [C reader example](examples/c/read_events.c) reads the channel printed by the
CLI. Build it against an installation:

```powershell
cmake -S examples/c -B build/example -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$PWD/out/install"
cmake --build build/example --config Release
./build/example/Release/read_events.exe <channel>
```

Readers choose `OWH_RETAINED` (oldest available record) or `OWH_NEW_ONLY` (a
boundary at reader open). `owh_reader_next` copies one record into caller-owned
memory. `OWH_BUFFER_TOO_SMALL` reports the required size without consuming it;
`OWH_NO_DATA`, `OWH_NOT_FOUND` and `OWH_CLOSED` are distinct. Close every reader.
Serialize calls on each handle and do not race them with close.

## Use from the JVM

[FfmRoundtrip.java](examples/java/FfmRoundtrip.java) is a runnable Java 25 example
of native symbol lookup, C struct layouts, calls and cleanup. It uses a private
test channel and does not touch Overwolf:

```powershell
java --enable-native-access=ALL-UNNAMED examples/java/FfmRoundtrip.java "$PWD/out/install/bin/owh_capture.dll" "$PWD/out/install/bin/owh_client.dll"
```

Kotlin can use the same `java.lang.foreign` API. No JVM or JNI code runs in
Overwolf; only the capture DLL does. There is no published Maven/Gradle wrapper.

[FfmStateRead.java](examples/java/FfmStateRead.java) reads current fields through
the separate state API and prints their original bytes as hex:

```powershell
java --enable-native-access=ALL-UNNAMED examples/java/FfmStateRead.java "$PWD/out/install/bin/owh_client.dll" <source-channel>
```

The state example loads only the client DLL in your own JVM. It needs an existing
provider channel; it does not load the capture DLL or modify Overwolf.

[FfmChannels.java](examples/java/FfmChannels.java) discovers current source
channels with the same client DLL:

```powershell
java --enable-native-access=ALL-UNNAMED examples/java/FfmChannels.java "$PWD/out/install/bin/owh_client.dll"
```

### Fetch from Gradle

The release ZIP can be a Gradle Ivy dependency. This coordinate describes a
GitHub release artifact; it is not a Maven publication or a Kotlin wrapper.
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

Declare a separate native configuration in `build.gradle.kts`:

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

Gradle retrieves and caches the ZIP. Your packaging task still needs to verify
its SHA-256 against the release's `SHA256SUMS`, extract the required binaries
and retain their license notices. Use the resulting client DLL path for FFM;
do not add the ZIP to the JVM classpath. LeagueLab's Windows integration has
tested this release retrieval, packaging and Kotlin FFM path.

## Compatibility and limits

The current profile in [profile.hpp](src/capture/overwolf/profile.hpp) was reviewed
against `OWGameEventsConsumer.dll` **2.0.0.5**, from Overwolf **0.311.1.1**:

```text
SHA-256: 52c417c374c9331b6bb8d13b784cb1ab51ff5f975ff1c51b8ee102a4d16e7426
Reader RVA: 0xa200
```

An unreviewed file hash or unexpected entry bytes refuses this hook. A familiar
filename is insufficient to reuse a private RVA/layout in another build.
The hook is game-independent at the event-channel boundary. It captures channels
that the reviewed classic consumer actually reads; it does not start game
providers, enable features, or support every other Overwolf transport implicitly.

### Games and event availability

Overwolf's [game-event support list](https://dev.overwolf.com/ow-native/live-game-data-gep/game-events-status-health/)
is the upstream reference. Overlay support alone does not imply event support.
Examples of games with documented Overwolf event APIs are listed below. This
is not a claim that every game uses this hook's reviewed transport or has been
tested with this tool.

| Game | Overwolf event documentation | Hook verification |
| --- | --- | --- |
| League of Legends | [Events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/league-of-legends/) | Short live capture; full coverage unverified |
| Fortnite | [Events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/fortnite/) | Not tested |
| Apex Legends | [Events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/apex-legends/) | Not tested |
| Counter-Strike 2 | [Events](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/counter-strike-2/) | Not tested |

All-channel selection and filtering are also tested with multiple synthetic
channels through the actual reviewed consumer. That verifies multiplexing, not
additional games' producer behavior.

### Transport limits

- The event hook captures discrete events; `owh_state_read` separately copies
  persistent InfoDB values. Neither claims every game state field or a canonical
  per-game schema. Both retain original values for the caller to interpret.
- Our ring has sequence numbers and per-reader overwrite counts. Producer loss,
  Overwolf's original ring overwrites and its consumer age filter are upstream
  and cannot be recovered here. No lossless-delivery guarantee is made.
- Capture publishes without waiting for readers. Contention returns `OWH_BUSY`;
  rejected captures and malformed ranges have separate counters.
- Shared objects use the current Windows session and default process ACLs. They
  are not a hostile-peer security boundary. Raw records can contain identifiers;
  review captures before sharing them.
- The experimental recording is a sequence of native x64 `owh_record` plus
  `owh_probe_frame` structs (ABI 1), not a stable archival format.

A 20-second live run on 2026-10-21 forwarded 15 observed damage events with zero
reported bridge overwrites/rejections. The original entry bytes were verified
restored afterward and the same Overwolf process remained responsive. This does
not establish complete tracker or Arena-offer support.

## Tests and releases

CI runs transport, discovery, state recording, stop/lifecycle and MinHook tests
in Debug/Release, compiles the installed C examples, and runs the JVM examples.
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
redistributed. Their synthetic gameplay inputs are not real-match validation.

Enable the JVM tests with `-DOWH_FFM_TESTS=ON` and, if necessary,
`-DJava_JAVA_EXECUTABLE=C:/path/to/java.exe`. Set `OWH_OVERWOLF_PROBE=OFF` to
build the bridge/state/discovery APIs and state CLI without the event hook;
also disable `OWH_HOOK_TESTS` to avoid MinHook completely. Use `BUILD_TESTING=OFF`
for a library-only build without test prerequisites.

Use [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/):
`fix:` for fixes, `feat:` for features, and `!` / `BREAKING CHANGE:` for breaking
changes. Release Please opens a version/changelog PR after green main builds.
Review and merge that PR to create a tagged release; the tested Windows ZIP and
SHA-256 file are then attached automatically. `version.txt` is the build version;
the release manifest records the last published version.

Run `cpack --config build/CPackConfig.cmake -C Release -B out` for a local ZIP.
For issues, include the tool version, consumer version/hash, failure status and
a minimal reproduction. Do not attach unreviewed raw captures.

## License

[MIT](LICENSE). MinHook's BSD and decoder notices are preserved in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This project is not affiliated
with Overwolf or Riot Games.
