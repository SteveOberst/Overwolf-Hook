# Overwolf Hook

Read Overwolf game data without building or running an Overwolf app. Inspect
current provider values, capture discrete events, or feed both into your own
tools. Overwolf must still be running with the relevant game's provider active.

The Windows x64 library exposes two operations through a C ABI usable from
C/C++ and Java/Kotlin FFM: passive reads of the classic InfoDB shared memory,
and an event hook that copies records into a separate shared-memory channel.
State reads own their data; event readers have independent cursors.

The broader API and `OverwolfHook` names below describe the next release.
Release 0.1.0-beta.1 contains only the former `OverwolfEventHook` event API.

**Experimental:** the event hook targets one reviewed consumer build. The state
reader targets one reviewed classic InfoDB layout. Synthetic tests establish
memory ownership and bounds, not complete per-game coverage. The earlier event
hook and Python InfoDB prototype have short live evidence; the new native state
reader still needs live match validation.

## Build

Requires Windows x64, Visual Studio 2022 or newer with C++ tools/Windows SDK,
and CMake 3.25+. Java 25 is needed only for the optional FFM example/test.

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

The installed package contains `owh_capture.dll`, `owh_client.dll`, `owh_probe.exe`,
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

Source channels are internal transport IDs, not public game IDs. The event frames
below retain observed source channels. Automatic enumeration of persistent-only
channels is not implemented. Readers must establish the relevant source lifetime;
each state read itself releases every provider handle before returning.

The installed [C state example](examples/c/read_state.c) prints keys and hex bytes:

```powershell
./build/example/Release/read_state.exe <source-channel>
```

## Capture events

Start Overwolf normally with the game's overlay enabled, then enter a game.
Use the PID of `Overwolf.exe`, not a browser/helper or a game process.

```powershell
Get-Process Overwolf | Select-Object Id
./out/install/bin/owh_probe.exe <PID> "$PWD/out/install/bin/owh_capture.dll" 20 "$PWD/events.bin"
python tools/inspect_capture.py events.bin
```

Replace `<PID>` with the displayed process ID. The CLI accepts a duration of
1–120 seconds and requires a new output file. It prints its channel name,
sample event names and capture counters, then stops the hook and drains the
reader. Keep the DLL path unchanged while Overwolf is running.

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
watches the controller process and requests shutdown if it exits; abrupt-owner
failure handling still needs dedicated live testing.

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

CI runs the transport, passive-state, process-lifecycle and MinHook tests in
Debug/Release, compiles the installed C examples, and runs the JVM example.
No test injects into a running third-party process. An optional owned-host test
executes the real consumer reader against synthetic layout storage:

```powershell
cmake -S . -B build "-DOWH_REVIEWED_CONSUMER=C:/path/to/OWGameEventsConsumer.dll"
```

Enable the JVM test with `-DOWH_FFM_TESTS=ON` and, if necessary,
`-DJava_JAVA_EXECUTABLE=C:/path/to/java.exe`. Set `OWH_OVERWOLF_PROBE=OFF` to
build only the bridge; also disable `OWH_HOOK_TESTS` to avoid MinHook completely.

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
