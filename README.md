# Overwolf-Hook

A Windows x64 library and CLI that copies Overwolf's League event records into a
separate shared-memory channel. Consumers read through a small C ABI, including
Java/Kotlin through FFM. Multiple readers have independent cursors.

**Experimental:** the hook targets one reviewed Overwolf consumer build. It has
passed an owned-host test and a short live capture; complete Arena coverage,
burst load and reconnect behavior have not been verified.

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

The installed package contains `oet_capture.dll`, `oet_client.dll`, `oet_probe.exe`,
C headers, CMake targets and license notices. The DLLs use the Microsoft C++
runtime; install the x64 Visual C++ Redistributable if it is missing.

## Capture

Start Overwolf normally with League's overlay enabled, then enter a match.
Use the PID of `Overwolf.exe`, not a browser/helper or the League process.

```powershell
Get-Process Overwolf | Select-Object Id
./out/install/bin/oet_probe.exe <PID> "$PWD/out/install/bin/oet_capture.dll" 20 "$PWD/events.bin"
python tools/inspect_capture.py events.bin
```

Replace `<PID>` with the displayed process ID. The CLI accepts a duration of
1–120 seconds and requires a new output file. It prints its channel name,
sample event names and capture counters, then stops the hook and drains the
reader. Keep the DLL path unchanged while Overwolf is running.

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

The installed CMake package is named `EventTap`:

```cmake
find_package(EventTap CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE EventTap::Client)
```

Use [bridge.h](include/event_tap/bridge.h) for the client/transport API and
[probe.h](include/event_tap/probe.h) for capture frames and in-host control exports.
The control exports operate inside the capture host; calling them in your own
process does not install a remote hook. Use the CLI to start a capture.

The [C reader example](examples/c/read_events.c) reads the channel printed by the
CLI. Build it against an installation:

```powershell
cmake -S examples/c -B build/example -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$PWD/out/install"
cmake --build build/example --config Release
./build/example/Release/read_events.exe <channel>
```

Readers choose `OET_RETAINED` (oldest available record) or `OET_NEW_ONLY` (a
boundary at reader open). `oet_reader_next` copies one record into caller-owned
memory. `OET_BUFFER_TOO_SMALL` reports the required size without consuming it;
`OET_NO_DATA`, `OET_NOT_FOUND` and `OET_CLOSED` are distinct. Close every reader.
Serialize calls on each handle and do not race them with close.

## Use from the JVM

[FfmRoundtrip.java](examples/java/FfmRoundtrip.java) is a runnable Java 25 example
of native symbol lookup, C struct layouts, calls and cleanup. It uses a private
test channel and does not touch Overwolf:

```powershell
java --enable-native-access=ALL-UNNAMED examples/java/FfmRoundtrip.java "$PWD/out/install/bin/oet_capture.dll" "$PWD/out/install/bin/oet_client.dll"
```

Kotlin can use the same `java.lang.foreign` API. No JVM or JNI code runs in
Overwolf; only the capture DLL does. There is no published Maven/Gradle wrapper.

## Compatibility and limits

The current profile in [profile.hpp](src/capture/overwolf/profile.hpp) was reviewed
against `OWGameEventsConsumer.dll` **2.0.0.5**, from Overwolf **0.311.1.1**:

```text
SHA-256: 52c417c374c9331b6bb8d13b784cb1ab51ff5f975ff1c51b8ee102a4d16e7426
Reader RVA: 0xa200
```

An unreviewed file hash or unexpected entry bytes refuses this hook. A familiar
filename is insufficient to reuse a private RVA/layout in another build.
The current CLI selects League channel `10054261`; other channels need explicit
configuration in the in-host options.

- This captures discrete events, not persistent InfoDB values or every game
  state field. Names/payloads stay raw; no canonical League schema is claimed.
- Our ring has sequence numbers and per-reader overwrite counts. Producer loss,
  Overwolf's original ring overwrites and its consumer age filter are upstream
  and cannot be recovered here. No lossless-delivery guarantee is made.
- Capture publishes without waiting for readers. Contention returns `OET_BUSY`;
  rejected captures and malformed ranges have separate counters.
- Shared objects use the current Windows session and default process ACLs. They
  are not a hostile-peer security boundary. Raw records can contain identifiers;
  review captures before sharing them.
- The experimental recording is a sequence of native x64 `oet_record` plus
  `oet_probe_frame` structs (ABI 1), not a stable archival format.

A 20-second live run on 2026-10-21 forwarded 15 observed damage events with zero
reported bridge overwrites/rejections. The original entry bytes were verified
restored afterward and the same Overwolf process remained responsive. This does
not establish complete tracker or Arena-offer support.

## Tests and releases

CI runs the transport, process-lifecycle and MinHook tests in Debug/Release,
checks the installed C example, and runs the JVM example against the DLLs.
No test injects into a running third-party process. An optional owned-host test
executes the real consumer reader against synthetic layout storage:

```powershell
cmake -S . -B build "-DOET_REVIEWED_CONSUMER=C:/path/to/OWGameEventsConsumer.dll"
```

Enable the JVM test with `-DOET_FFM_TESTS=ON` and, if necessary,
`-DJava_JAVA_EXECUTABLE=C:/path/to/java.exe`. Set `OET_OVERWOLF_PROBE=OFF` to
build only the bridge; also disable `OET_HOOK_TESTS` to avoid MinHook completely.

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
