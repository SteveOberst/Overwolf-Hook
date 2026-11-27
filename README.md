# Overwolf Hook

[![Latest release](https://img.shields.io/github/v/release/SteveOberst/Overwolf-Hook?include_prereleases&label=release)](https://github.com/SteveOberst/Overwolf-Hook/releases)
[![Windows x64](https://img.shields.io/badge/platform-Windows%20x64-0078D4)](https://SteveOberst.github.io/Overwolf-Hook/getting-started/installation/)
[![MIT license](https://img.shields.io/github/license/SteveOberst/Overwolf-Hook)](LICENSE)

**Use Overwolf's game events, game state, and overlays in your own application, without building an app for the Overwolf platform.**

Sometimes the overhead of [Overwolf's app development and release process](https://dev.overwolf.com/ow-native/getting-started/release-your-app/)
is more than a small or private project needs. You might only want an overlay
for a few friends, game events for a personal tool, or a new feature in an
existing desktop application. Registering as a developer, getting an app
approved, and maintaining a separate Overwolf-platform app can be a lot to
take on for that.

Overwolf Hook removes the need for that separate app. This Windows x64 SDK lets
your **C, C++, Java, or Kotlin** application use Overwolf's game integrations
without an Overwolf developer account or an approved Overwolf-platform app.
Keep your own project, UI, renderer, and release process, and share your
application as a native or JVM program.

**The Overwolf desktop client is still required.** The SDK integrates with its
main process and uses private Overwolf internals, so Overwolf updates can affect
compatibility. It does not open, modify, or inject into game processes.

**[Read the documentation →](https://SteveOberst.github.io/Overwolf-Hook/)**

## Features

- **[Game events](https://SteveOberst.github.io/Overwolf-Hook/game-events/):** react
  to changes and occurrences reported by Overwolf's game integrations.
- **[Overlays](https://SteveOberst.github.io/Overwolf-Hook/overlays/):** render with
  your own rendering library, then publish the resulting images. An optional,
  experimental path binds a compatible shared D3D11 texture.
- **[Game state](https://SteveOberst.github.io/Overwolf-Hook/game-state/):** read
  snapshots of the values Overwolf already has for a supported game.

## Get started

Follow [SDK installation](https://SteveOberst.github.io/Overwolf-Hook/getting-started/installation/)
to add the dependency through **CMake or Gradle**, then
[create your first overlay](https://SteveOberst.github.io/Overwolf-Hook/overlays/first-overlay/)
or [subscribe to game events](https://SteveOberst.github.io/Overwolf-Hook/game-events/).
Java and Kotlin packages include the matching native libraries.

You need Windows x64, the x64 Visual C++ Redistributable, and an Overwolf
installation. Java and Kotlin also require Java 25. Available game data depends
on the game's provider; displaying overlays requires Overwolf's overlay to be
enabled for the compatible game.

Prefer a small starting project? Use the optional [C](starters/native/README.md),
[Java](starters/java/README.md), or [Kotlin](starters/kotlin/README.md) starter.
The [documentation sources](docs/index.md) are maintained in this repository.

## Project links

[Troubleshooting](https://SteveOberst.github.io/Overwolf-Hook/troubleshooting/)
· [Contributing](CONTRIBUTING.md)
· [Report an issue](https://github.com/SteveOberst/Overwolf-Hook/issues)
· [Releases](https://github.com/SteveOberst/Overwolf-Hook/releases)
· [Third-party notices](THIRD_PARTY_NOTICES.md)

Licensed under [MIT](LICENSE). Independent of and not endorsed by Overwolf or Riot Games.
