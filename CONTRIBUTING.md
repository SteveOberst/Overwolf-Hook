# Contributing

User documentation starts at the [overview](docs/index.md).

## Build the SDK

Use Windows x64, Visual Studio C++ tools and Windows SDK, CMake 3.25+, and Python 3. The JVM build also requires JDK 25. Native-only builds need no Java or Kotlin. For the Visual Studio 2022 toolchain used by CI:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

`architecture.boundaries` checks resolved includes, the public/private module
boundary, and Agent ownership of pixel queues. CMake 3.27+ also requests its
configured product dependency graph on the first configure. With CMake 3.25 or
3.26, configure twice to enable that graph check; the first run still checks
source boundaries. CI checks both. The catalog-extension fixture compiles one
extra method from a temporary catalog to verify the generated proxy and router.

The `windows-x64` preset uses Visual Studio 2022. Visual Studio 2026 uses `windows-x64-vs2026` and CMake 4.2+. Keep local paths in ignored `CMakeUserPresets.json`.

The Client and Agent DLLs link directly into `build/native/CONFIGURATION`. Tests
declare their own build dependencies; building one test target does not build the
whole SDK. The Client's common sources compile once in `owh_client_objects`;
production and fixture variants compile only their Overwolf control implementation
separately. Agent fixtures remain separate because their compile definitions affect
type layouts.

## JVM bindings

`jvm` is a Gradle root with `:java` and `:kotlin` subprojects. Java owns the JDK-only
core API, generated bindings, and bundled native pair. Kotlin depends on Java and
adds its language facade and coroutine support. CMake builds native code; Gradle
owns binding generation, compilation, tests, and JVM publication. Set `JAVA_HOME`
to JDK 25, build the matching native pair first, then run:

```powershell
./jvm/gradlew.bat -p jvm check assemble "-PnativeDirectory=$PWD/build/native/Debug" --no-daemon
```

Generated FFM bindings are build outputs: edit public C headers or handwritten APIs
and regenerate through Gradle. The language subprojects write to `jvm/build/java`
and `jvm/build/kotlin`. Use `-PowhBuildDirectory=PATH` to relocate the complete JVM
build. `publish` stages both components and their Ivy dependency metadata under
`jvm/build/repository/vVERSION`. Artifact names are `overwolf-hook-VERSION-windows-x64.jar`
and `overwolf-hook-kotlin-VERSION.jar`; `VERSION` comes from `version.txt`.
CI runs raw Java FFM fixtures and external Java/Kotlin consumers after both builds;
these checks do not belong to the CMake dependency graph.

`gradlew check` verifies handle lifetimes, packaged artifacts, and model generation
from the native declarations. It also checks that new model fields propagate and
that pointer fields require explicit ownership metadata.

Gradle supplies the Java compiler, Java launcher, Kotlin compiler classpath, and
jextract runtime to the Python binding tests. Run them through `:java:bindingModelsTest`
or `check`; they do not discover toolchains independently. The pinned jextract
archive is cached by SHA-256, so changing `jvm/tools/jextract.json` selects a new
cache entry. Set `-Pjextract=PATH_TO_JEXTRACT_BAT` to use an explicit local toolchain.

## Internal diagnostic tools

Configure with `-DOWH_BUILD_TOOLS=ON` to build the maintainer-only `owh_info` and `owh_controller` executables and their optional tests. The normal build defaults to OFF; the test presets enable them explicitly. Tools are never installed into the SDK package. Their commands and recording formats are internal and have no public compatibility promise. See [tool notes](tools/README.md).

## Documentation

The documentation toolchain is separate from CMake and Gradle. With Python 3.11 or newer:

```powershell
python -m venv .venv-docs
./.venv-docs/Scripts/python.exe -m pip install -r tools/docs/requirements.txt
./.venv-docs/Scripts/python.exe tools/docs/build.py build
./.venv-docs/Scripts/python.exe tools/docs/build.py serve
```

On Linux/macOS use `.venv-docs/bin/python`. Preview binds to `127.0.0.1:8000`; stop it with Ctrl+C. The strict build checks local links, fragments, images, Mermaid embeds, and starter downloads. Output is under ignored `build/docs`. Starter archives are made from the existing projects.

Give each content page a descriptive `title` and a unique `description` in its
Markdown front matter. Keep short navigation labels in `zensical.toml`. The
theme derives search and social-preview metadata from those fields; the site
check verifies metadata, canonical URLs, and sitemap coverage. Static verification
files are copied unchanged and are not articles in the navigation or sitemap.

Use **pixel-buffer submission** and **shared-texture binding** for the two overlay
handoff mechanisms. They do not identify a rendering backend. Use DRAM/VRAM only
when describing actual storage, and CPU/GPU only for actual hardware work.
SDK API names use `pixel_buffer` / `PixelBuffer` and `shared_texture` /
`SharedTexture` (for example, `createPixelBuffer`, `pixelBufferStatus`,
`createSharedTexture`, `sharedTextureStatus`). Keep real upstream symbol/API
names, such as Overwolf's `onAcceleratedPaintWidget` and D3D11's
`CPUAccessFlags`, unchanged. Do not present those names as SDK route names.

Write diagrams directly in Markdown using fenced `mermaid` blocks and standard
Mermaid syntax, without theme variables or renderer configuration. Keep any
styling minimal and local to the diagram.
Keep an `accTitle` and `accDescr` in each diagram. Check the rendered page in both
site themes after changes: the build checks inclusion and links, while visual
layout and technical meaning still need review.

Guides use common task headings with linked `C`, `Java`, and `Kotlin` tabs, in that order.
When a guide includes a distinct C++ example, put `C++` after `C`.
Put shared requirements outside tabs and language-specific ownership beside its code.
Keep H2/H3 headings outside tabs so the page outline only contains visible sections.
Include runnable source with `pymdownx.snippets` and link the source beside it; do not
maintain a second inline copy. Keep file, build/run, and expected-result instructions
visible. Long complete programs can use a closed `??? example "Complete source"`
listing inside their language tab; keep prerequisites and ownership constraints
outside the disclosure. The builder runs from the repository root. Use named
snippet regions rather than line ranges. API reference pages remain language-specific. C operations are grouped by ABI
header under `docs/reference/c/`; `native-api.md` is the operation index. Shared
connection/lifetime rules live in `reference/contracts.md`. Record meanings and
generated layouts live in `reference/data-records.md`; constants live in
`reference/constants.md`. Keep contributor-only implementation details in
`development/internals.md`, linked from the user-facing `architecture.md`. Public guides explain the user's task before
introducing Overwolf internals. Describe overlay handoff as pixel-buffer submission
or shared-texture binding, independently of how the application renders. Use the
matching SDK identifiers in examples; keep upstream Overwolf/D3D11 names exact.

Public installation instructions target the released SDK and show automatic CMake/
Gradle dependency setup for existing applications. Starters are optional. Local
candidate repositories and release validation belong here, not in the user manual.
Zensical reads the authored `docs/` tree directly and owns live rebuilds. The
built-in macros extension reads `version.txt` for the installation page. The same
file supplies SDK versions in dependency downloads and standalone JVM starters.
Keep `sdkVersion` in the starter source files as a template; the website builder
and consumer smoke resolve it before use. Release Please updates `version.txt`.
Dependency templates under `docs/assets/integration/` use `@OWH_SDK_VERSION@`; the helper renders
these into ignored `docs/assets/generated/integration/`, alongside ignored starter
archives under `docs/downloads/`. Re-run the helper after changing a starter or
dependency template; ordinary page, snippet and stylesheet edits rebuild through
Zensical without a second watcher. Test the generated dependency downloads against
a staged release mirror before release. Do not substitute an unverified public
download claim. The website is the user manual; the native ZIP contains a link,
licenses and SDK files, without a second offline docs/starter distribution.


On Windows, compile the starter projects and documentation examples as independent
SDK consumers. Supply matching installed native and locally staged JVM artifacts:

```powershell
python tools/docs/smoke.py --sdk-dir C:/sdk/overwolf-hook --ivy-repository C:/sdk/ivy-repository
```

The work directory must be empty. Use `--work-dir` for another run, `--offline` for
cached Gradle dependencies, or `--generator "Visual Studio 18 2026"` for VS2026/CMake 4.2+.
The smoke compiles the starters, every public example (including the named
Overwolf helper snippets), and installation load checks, then runs separate SDK-loading consumers; it never
starts their rendering entry points or attaches to Overwolf.
The Release CI job runs this same smoke after staging matching native/JVM artifacts.
API header and JVM changes also trigger the documentation site check.
The site builder extracts C declarations, record layouts and constant values
from the public-header manifest. Edit the headers rather than generated files;
add an explanation in `tools/docs/reference.py` when introducing an enum value.
It is separate from product regression tests. Preserve old page URLs with native
redirect maps when consolidating articles. Verify changed links, examples and layout
during editing, then check the complete site on desktop/mobile and in both color schemes.

## Changes and boundaries

Tests belong under `tests` or JVM test source sets. Consumer fixtures use installed CMake packages or staged Ivy artifacts; copyable applications under `starters` remain independent. Public CI does not require Overwolf or a game. Live integration tests require an explicitly selected test session.

Target only the verified Overwolf main process. Never open, patch, or inject into a game process. Keep private research notes in ignored `.workspace`; do not commit proprietary binaries, decompiler output, or raw recordings. Follow `.editorconfig`, preserve matching Client/Agent builds, and identify breaking changes in Conventional Commits. Session cleanup does not unload the Agent; restart Overwolf before testing a different Agent build.

Release Please manages versions and release assets. Do not overwrite a published release with binaries from another source or ABI. Pull requests validate docs without deployment; deployment follows a successful stable SDK release and GitHub Pages configuration.
