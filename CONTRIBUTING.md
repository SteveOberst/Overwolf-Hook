# Contributing to Overwolf Hook

This guide covers changing and testing the library. For installation and use,
start with the [README](README.md).

## Build and test

Use Windows x64 with Visual Studio 2022 or newer (C++ tools and Windows SDK),
CMake 3.25+ and Python 3. Automatic jextract downloads also require Python's
`tarfile` extraction-filter support. Install JDK 25 if building
the JVM SDK or running FFM examples. Native-only builds do not need Java.

```powershell
git clone https://github.com/SteveOberst/Overwolf-Hook.git
cd Overwolf-Hook
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix out/install
```

For Visual Studio 2026, select `-G "Visual Studio 18 2026"`. The included
`windows-x64` configure preset and `debug`/`release` build/test presets target
that version. MinHook is fetched through CMake FetchContent with a pinned hash;
no Overwolf SDK or running game is needed to build.

Run one test group with, for example:

```powershell
ctest --test-dir build -C Release -R "^transport\." --output-on-failure
```

## JVM SDK and generated bindings

Set `JAVA_HOME` to JDK 25, then enable the optional JVM build:

```powershell
cmake -S . -B build -DOWH_JVM_BINDINGS=ON -DOWH_FFM_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The JVM sources are two sibling modules: [jvm/java](jvm/java) owns the Java SDK,
its jextract configuration and CMake build; [jvm/kotlin](jvm/kotlin) owns the
optional Kotlin API and its Gradle build. Kotlin depends on the Java SDK.

The Java SDK has two parts:

- **Generated FFM bindings** in `dev.sxlver.overwolfhook.bindings`: jextract reads
  the public [C headers](include/overwolf_hook) and generates function bindings,
  constants and struct layouts. These are build outputs, not checked-in sources.
- **Handwritten API** in [jvm/java/src/main/java](jvm/java/src/main/java): `OverwolfHook`,
  `Capture`, `EventReader` and supporting classes provide discovery, owned Java
  values, native loading and capture lifetime over those bindings.

[jvm/java/build.py](jvm/java/build.py) downloads the checksum-pinned jextract 25
from [jvm/java/toolchain.json](jvm/java/toolchain.json), derives the `owh_*`/`OWH_*` symbol
filter from the headers, generates bindings, and compiles both parts with Java 25.
The JAR bundles the matching native binaries and licenses. The generator is not
bundled or run by consumers. `OWH_JEXTRACT` can select a local jextract 25 executable.

Keep ABI changes in the headers and regenerate through the build. Do not patch
generated Java. Changes to the convenient API belong in its handwritten classes;
update the C/Java/Kotlin examples when their usage changes.

### Optional Kotlin API

[jvm/kotlin](jvm/kotlin) is a separate Gradle library over the Java SDK. Its
Duration overloads and Flows are handwritten Kotlin; it does not generate more
FFM bindings, duplicate event/state types, or bundle native binaries a second time.
The Java/native build remains independent of Kotlin and coroutines.

After the Java SDK build above, stage that exact source-built dependency and run
the Kotlin tests (all paths below are relative to the repository root):

```powershell
$version = (Get-Content version.txt).Trim()
$repository = "$PWD/build/kotlin-test-repository"
$release = New-Item -ItemType Directory -Force -Path "$repository/v$version"
Copy-Item "build/jvm/java/Release/overwolf-hook-$version-windows-x64.jar" -Destination $release.FullName
./examples/kotlin/gradlew.bat -p jvm/kotlin check publish "-I$PWD/tests/kotlin_source_build.init.gradle" "-DoverwolfHookTestRepository=$repository" "-PowhCaptureDll=$PWD/build/Release/owh_capture.dll"
```

`publish` here writes only to `jvm/kotlin/build/repository`; it does not contact a
public package registry. Its Ivy descriptor declares the Java SDK, Kotlin stdlib
and coroutine dependencies, so the consumer only declares the Kotlin artifact.

The [Kotlin example](examples/kotlin) always uses module coordinates. CI copies
the freshly built SDK and Kotlin JAR/descriptor to a temporary Ivy repository,
using [the test init script](tests/kotlin_source_build.init.gradle) to redirect
only our modules. The repository override belongs to tests, not example dependency
declarations. Exact source-build/example commands are in
[the CI workflow](.github/workflows/ci.yml).

Kotlin tests create our own native bridge through the public C ABI; `owhCaptureDll`
selects the matching source-built test writer. They test cancellation/closure,
independent collectors, failures, exact retained bytes/loss and Duration bounds.
They do not need an Overwolf consumer. The optional owned-host script can also
run Kotlin capture lifecycle tests when passed its test classpath as a fifth
argument; its JVM fixtures and requirements are described below.
`OwnedCaptureTestKt` is a child-JVM test harness in test sources, not a library
entry point or part of the published JAR.

## Tests and optional fixtures

[CI](.github/workflows/ci.yml) builds and tests Debug and Release, generates the
JVM SDK, compiles installed C examples, and runs Java/Kotlin examples against
owned IPC fixtures. The suites cover bridge bounds/loss, independent readers,
notification races, timeout/cancellation, producer death, state/discovery,
recording, signature matching and lifecycle. No test injects into a running
third-party process. Synthetic tests do not certify real-game data coverage.

Additional controller tests can use a locally supplied consumer DLL:

```powershell
cmake -S . -B build "-DOWH_REVIEWED_CONSUMER=C:/path/to/OWGameEventsConsumer.dll"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

These tests execute the consumer reader against synthetic storage in our own
host process. They cover capture, silent-producer shutdown, console/file stop,
owner death, restart, host exit, and an unchanged-code/changed-hash case. The DLL
is never downloaded or redistributed. They are not enabled in public CI.

Useful configuration switches:

| Option | Purpose |
| --- | --- |
| `OWH_JVM_BINDINGS=ON` | Generate, compile and package the JVM SDK. |
| `OWH_FFM_TESTS=ON` | Run the standalone Java FFM examples. |
| `OWH_REVIEWED_CONSUMER` | Enable additional owned-host consumer tests. |
| `OWH_OVERWOLF_PROBE=OFF` | Omit the event hook/controller; keep state and discovery. |
| `OWH_HOOK_TESTS=OFF` | Omit trampoline tests; combine with the previous option to avoid MinHook. |
| `BUILD_TESTING=OFF` | Build without test targets and their prerequisites. |

The bundled JVM SDK requires the event hook/controller to be enabled.

## Consumer compatibility

The reference [profile](src/capture/overwolf/profile.hpp) was reviewed against
`OWGameEventsConsumer.dll` **2.0.0.5**, from Overwolf **0.311.1.1**:

```text
SHA-256: 52c417c374c9331b6bb8d13b784cb1ab51ff5f975ff1c51b8ee102a4d16e7426
```

[reader.cpp](src/capture/overwolf/reader.cpp) matches two body signatures inside
executable PE x64 function ranges from the exception table. RIP-relative address
bytes are masked; there is no fixed-address fallback. Exactly one function and
the expected entry bytes must match, including after restart.

An unknown hash is advisory; structural contradictions are hard errors. A
signature match alone does not verify private object layouts or semantics. Review
those assumptions when a consumer changes, retain tests for ambiguity/relocation
and changed entry bytes, and report unverified behavior explicitly. Do not add
Overwolf binaries, live recordings or decompiler output to the repository.

## Making changes

| Location | Responsibility |
| --- | --- |
| [include/overwolf_hook](include/overwolf_hook) | Public C ABI. |
| [src](src) | Native state, discovery, bridge and capture implementation. |
| [jvm/java](jvm/java) | Java SDK, jextract configuration, CMake build and artifact tests. |
| [jvm/kotlin](jvm/kotlin) | Optional Kotlin API, coroutine tests and Ivy publication. |
| [tools](tools) | Native controllers and recording inspector. |
| [tests](tests) | Owned fixtures and executable checks. |
| [examples](examples) | Consumer examples compiled by CI. |

Follow [.editorconfig](.editorconfig). Keep raw values and explicit failure states
intact; changes to private transport assumptions need corresponding fixture tests.
For a pull request, explain the behavior changed and how you verified it. For a
bug report, include versions, status/Win32 errors and a small reproduction without
unreviewed raw captures.

### Documentation

Use [GitHub's README guidance](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes)
for the landing page: purpose, benefit, prerequisites and a working first example.
Keep developer setup and release procedures in this guide. Apply the
[Diátaxis distinction](https://diataxis.fr/start-here/) between instructions,
reference and explanation without adding pages merely to fill a template.
Commands and examples must match code or executed checks; label unreleased
features and limits explicitly. Keep the native usage before the JVM usage.

## Releases

Use [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/): `fix:`
for fixes, `feat:` for features, and `!` or `BREAKING CHANGE:` for breaking changes.
[Release Please](.github/workflows/release.yml) opens a version/changelog PR after
green main builds. Reviewing and merging that PR creates the tagged release;
the verified Windows ZIP, Java SDK JAR, optional Kotlin JAR/Ivy descriptor and
`SHA256SUMS` are attached automatically.
`version.txt` is the build version; the release manifest records the last published
version. Do not replace published binaries with a different build of the same version.

To package a local native ZIP after building:

```powershell
cpack --config build/CPackConfig.cmake -C Release -B out
```

Review that packages contain matching binaries and license notices, and no test
classes, local recordings or proprietary DLLs. The Kotlin JAR/descriptor are
separate release assets; they do not change the native ZIP into a Kotlin build.
CI tests the source-built Kotlin dependency before publication, rather than
depending on an earlier released JAR.
