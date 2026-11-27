# Overwolf Hook SDK

The user manual is hosted at https://SteveOberst.github.io/Overwolf-Hook/.

Use Overwolf's game data and overlays from your existing native application
without the extra app-development and approval workflow of the Overwolf
platform. For a personal tool or a project shared with friends, that can be
more overhead than the project needs. The SDK requires no Overwolf developer
account or approved platform app; keep your own UI, renderer, and release process.
Live features still require an installed Overwolf desktop client.

The package contains the C ABI, C++ facade, matching Client/Agent DLLs and CMake targets. Keep both DLLs from this package together. Java/Kotlin dependencies bundle their native DLLs automatically; they do not require this ZIP.

After `find_package(OverwolfHook CONFIG REQUIRED)`, link your executable to
`OverwolfHook::Client` for C or `OverwolfHook::Cpp` for C++. Call
`overwolf_hook_deploy_runtime(YOUR_TARGET)` to copy both matching DLLs beside
that executable after the build. Replace `YOUR_TARGET` with your target's name.

Source and build instructions: https://github.com/SteveOberst/Overwolf-Hook/.
