# Native C starter

> **Attach may restart Overwolf.** Initialized options select the default
> `OWH_ATTACH_RESTART` mode. If Overwolf is running, attach requests a normal
> close and waits up to ten seconds for that process to exit before launching
> it again. It never force-kills Overwolf; tray-shutdown routing is not proven
> for every version. If Overwolf is absent, attach starts it. SDK close does
> not stop Overwolf. Read [attach modes](../../docs/reference/contracts.md#attach-modes)
> before changing the mode.

Copy this directory for an independent C17 application. It does not build the SDK or participate in product tests.

Requirements: Windows x64, Visual Studio C++ tools and Windows SDK, CMake 3.25+, and internet access on the first build. Select an SDK release with `OVERWOLF_HOOK_VERSION`, or point `OverwolfHook_DIR` at an installed SDK package. For example, with Visual Studio 2022:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DOVERWOLF_HOOK_VERSION=replace-with-release-version
cmake --build build --config Release
./build/Release/first_overlay.exe
```

CMake deploys the matching Client and Agent DLLs beside the executable. For Visual Studio 2026, use `Visual Studio 18 2026` and CMake 4.2+.

For live output, start Overwolf and a compatible game with its overlay enabled, then run `./build/Release/first_overlay.exe`. The sample submits an opaque orange panel and keeps the source open until you press Enter. A successful submission does not confirm that Overwolf displayed it.

See the [installation guide](../../docs/getting-started/installation.md) and [first-overlay task](../../docs/overlays/first-overlay.md). This project is MIT licensed.
