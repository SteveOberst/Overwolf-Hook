---
title: "Create your first Overwolf overlay"
description: "Create a pixel-buffer overlay, submit a simple image and close its resources. Follow complete examples for native and JVM applications."
---

# Build your first overlay

Create an overlay in your own Windows application, fill one image and keep it
visible while the program runs. This walkthrough uses pixel-buffer submission and requires
no rendering framework.

## Before you begin

Complete the [dependency and load check](../getting-started/installation.md),
install Overwolf and enable its overlay for your compatible game. Begin outside
the game: the example must attach before you enter it.

!!! warning "The default attach can close and relaunch Overwolf"

    `sdk.attach()` uses restart mode. It may close a running Overwolf session,
    then starts Overwolf and attaches the Agent. Run the example before entering
    the game. Detaching does not stop Overwolf. See [attach modes](../reference/contracts.md#attach-modes)
    for startup requirements and the advanced HOT alternative.

## Run the example

The program creates a 256 x 128 source at (64, 64), clears it to transparent,
draws an orange panel with transparent margins, and queues one image. It keeps
the source open until you press Enter, then closes it and detaches.

1. Save the complete program using the path in your language tab.
2. Build the application with that tab's CMake or Gradle configuration.
3. Run its entry point outside the game.
4. Wait for **Image submitted** in the console.
5. Enter the compatible game while the program runs.
6. Inspect the image as described in [Check the result](#check-the-result).
7. Press Enter in the console to close the source and end the SDK session.
   Overwolf itself can remain running.

Use your existing project; the downloadable starter projects are optional.

=== "C"

    Save the [complete source](../downloads/native/main.c) as `main.c` in the CMake
    project from installation:

    ```powershell
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    ./build/Release/my_app.exe
    ```

    ??? example "Complete source"

        ```c
        --8<-- "starters/native/main.c"
        ```

=== "C++"

    Save the [complete C++ source](../assets/examples/cpp/first_overlay.cpp)
    as `main.cpp` in the [C++ CMake setup](../getting-started/installation.md).

    Build and run the C++ target:

    ```powershell
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    ./build/Release/my_app.exe
    ```

    ??? example "Complete source"

        ```cpp
        --8<-- "docs/assets/examples/cpp/first_overlay.cpp"
        ```

=== "Java"

    Save the [complete source](../downloads/java/src/main/java/FirstOverlay.java)
    as `src/main/java/FirstOverlay.java`.

    With Gradle's `application` plugin, set the entry point in `build.gradle.kts`:

    ```kotlin
    application { mainClass.set("FirstOverlay") }
    ```

    Run `./gradlew.bat run` with native access enabled as shown in installation.

    ??? example "Complete source"

        ```java
        --8<-- "starters/java/src/main/java/FirstOverlay.java"
        ```

=== "Kotlin"

    Save the [complete source](../assets/examples/kotlin/FirstOverlay.kt)
    as `src/main/kotlin/examples/overlays/FirstOverlay.kt`.

    With Gradle's `application` plugin, set the entry point in `build.gradle.kts`:

    ```kotlin
    application { mainClass.set("examples.overlays.FirstOverlayKt") }
    ```

    Run `./gradlew.bat run` with native access enabled as shown in installation.

    ??? example "Complete source"

        ```kotlin
        --8<-- "docs/assets/examples/kotlin/FirstOverlay.kt"
        ```

## Check the result

Confirm that the orange panel appears with transparent margins at the source's
configured position. Pressing Enter should remove this source. If it does not
appear, follow [overlay display checks](../troubleshooting.md#separate-source-acceptance-from-display).

## Understand the image contract

The example draws a complete straight-alpha BGRA8 image in its own buffer, then
calls `submit`. The SDK copies the image and manages shared-slot ownership. Your
buffer is reusable on return; no acquire/commit or manual busy retry is required.
Native publication happens later in the SDK integration, and successful
publication does not confirm display. Check that the correct game has
Overwolf's overlay enabled and inspect the game window while the program waits
for Enter. The source remains registered until the scope closes.

## Next steps

See [pixel-buffer submission](pixel-buffers.md) to send changing images from
your renderer and choose latest-image delivery or ordered backpressure. Use
[shared-texture binding](shared-textures.md) to hand off a completed D3D11
allocation without copying its pixels through the SDK's pixel-buffer slots.
