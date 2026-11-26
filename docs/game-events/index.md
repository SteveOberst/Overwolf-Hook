# Receive game-event records

Receive raw game-event records in your existing native or JVM application,
without maintaining an Overwolf-platform app to forward them to it. The SDK
integration needs no Overwolf developer account or approved platform app;
the Overwolf desktop client must still be installed. This guide gets you from
a connected SDK to a live stream and shows where to pass
records to your application logic. Decoding a game-specific payload is separate;
the SDK does not provide a universal gameplay-event model.

Overwolf obtains game data through game-specific integrations, often called providers. This library copies event records that Overwolf already reads. It does not enable a missing provider or add support for another game. Use Overwolf with a supported game after the example attaches.

## Before you begin

Add the [dependency](../getting-started/installation.md) first. These are
complete console programs; use the same calls in your own application after
you have verified delivery. C++ can use the [resource-owning event API](../reference/cpp-api.md#events)
or compile this C program in a C target.

!!! warning "The sample's default attach can restart Overwolf"

    The examples use default restart attach, which may close and relaunch
    Overwolf. Attach before entering the game, then generate events while the
    example is waiting. Detach does not stop Overwolf. See [attach modes](../reference/contracts.md#attach-modes).

## Connect, subscribe, and read

Open and attach an **OverwolfHook** context (`owh_sdk` in C). Use its
**EventController** to create an **EventSubscription**, then receive records
from that stream. Each subscription has its own read position. Closing one
does not close other subscriptions or the SDK context.

The sample subscribes directly. If you need a readiness query first, see
[event capabilities](../reference/java-api.md#eventcontroller) or the
[C contract](../reference/c/events.md). Use the same SDK context if your
application also creates overlays.

## Receive an event

The program attaches once, subscribes to all observed channels and prints a
listening message. It then receives events for five minutes, leaving time to
enter a game and generate activity. Each receive waits at most one second; a
quiet interval keeps the same subscription open.

Output includes the channel, raw byte count, known loss and a hexadecimal
preview of the first 16 bytes. The SDK exposes one opaque raw byte range, not
a universal decoded event name or game-data object.

To choose which records your application should handle, see [finding your game's event definitions](data-model.md#find-your-games-event-definitions).

1. Save the program at the path in your language tab.
2. Select its entry point using that tab's CMake or Gradle configuration.
3. Run it outside the game; default attach can restart Overwolf.
4. Wait for **Listening for 5 minutes** in the console.
5. Enter the supported game while the program runs.
6. Generate activity in the game.
7. Inspect the received records. After five minutes the program closes its
   subscription and detaches; this does not stop Overwolf.

=== "C"

    [View or download `receive_events.c`](../assets/examples/c/receive_events.c).

    To use the minimal CMake project from installation, save this program as `main.c`, then run:

    ```powershell
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    ./build/Release/my_app.exe
    ```

    ??? example "Complete source"

        ```c
        --8<-- "docs/assets/examples/c/receive_events.c"
        ```

=== "Java"

    [View or download `ReceiveEvents.java`](../assets/examples/java/ReceiveEvents.java). Save it as `src/main/java/examples/events/ReceiveEvents.java`.

    For the Gradle `application` plugin, select this example's entry point:

    ```kotlin
    application { mainClass.set("examples.events.ReceiveEvents") }
    ```

    Run `./gradlew.bat run`. In an existing application, call the relevant operations from your own code instead of replacing your main class.

    ??? example "Complete source"

        ```java
        --8<-- "docs/assets/examples/java/ReceiveEvents.java"
        ```

=== "Kotlin"

    [View or download `ReceiveEvents.kt`](../assets/examples/kotlin/ReceiveEvents.kt). Save it as `src/main/kotlin/examples/events/ReceiveEvents.kt`.

    For the Gradle `application` plugin, select this example's entry point:

    ```kotlin
    application { mainClass.set("examples.events.ReceiveEventsKt") }
    ```

    Run `./gradlew.bat run`. In an existing application, call the relevant operations from your own code instead of replacing your main class.

    ??? example "Complete source"

        ```kotlin
        --8<-- "docs/assets/examples/kotlin/ReceiveEvents.kt"
        ```

## Check delivery

Wait for **Listening for 5 minutes** before switching to the game. The
subscription is already active at that point. Keep the program running while
you play; it ends the observation window, closes the subscription and detaches
when the five minutes expire. Close the console to stop it earlier.

The hexadecimal preview proves which bytes arrived without assuming an encoding
or field schema. It does not decode an event name. The channel identifies a
data source, not a public game ID; see [event data and delivery](data-model.md).

A quiet receive interval is a valid outcome: the game may have produced no relevant event, or its data may not be available. Check [troubleshooting](../troubleshooting.md) before treating that as a library failure.

## Pass records to your application

The examples call `handle_event` / `handleEvent` after each successful receive.
Replace that helper body with your UI/model update, recorder, or parser call.
The receive loop and subscription lifetime remain the same. Its argument is
`owh_event` in C or the owned generated `Event` in Java/Kotlin.

Pass exactly `raw_size` bytes to your parser; the JVM `raw` view is already
bounded. Copy a C handler's record or needed bytes if work continues after
the handler returns. JVM `Event` owns its copy and can be retained; use
`copyBytes()` only when the receiving API needs an array.

The handler receives one complete copied record:

=== "C"

    ```c
    --8<-- "docs/assets/examples/c/receive_events.c:handler"
    ```

=== "Java"

    ```java
    --8<-- "docs/assets/examples/java/ReceiveEvents.java:handler"
    ```

=== "Kotlin"

    ```kotlin
    --8<-- "docs/assets/examples/kotlin/ReceiveEvents.kt:handler"
    ```

When observing several sources, route records using `sourceChannel` (C
`source_channel`). Check `lostBefore` (C `lost_before`) before treating a series
as complete. See [record metadata and decoding](data-model.md#understand-a-received-event)
for the field meanings and provider-specific payload limits.

## Collect events with Kotlin Flow

For a coroutine-based application, `observe()` replaces the explicit receive
loop. This complete console program bounds collection to five minutes. In a UI,
collect in your UI/model's coroutine scope instead; cancellation closes that
collection's subscription after its bounded native receive returns.

To run the [complete Flow example](../assets/examples/kotlin/ObserveEvents.kt):

1. Save it as `src/main/kotlin/examples/events/flow/ObserveEvents.kt`.
2. Set Gradle's `application.mainClass` to `examples.events.flow.ObserveEventsKt`.
3. Run `./gradlew.bat run` with native access enabled as in installation.
4. Enter the supported game after the console prints **Collecting for 5 minutes**.

The source uses `runBlocking` for a console application:

??? example "Complete Flow source"

    ```kotlin
    --8<-- "docs/assets/examples/kotlin/ObserveEvents.kt"
    ```

Errors other than the normal stopped
stream propagate to the collecting coroutine; handle them at your application's
coroutine boundary. Each collection owns its own subscription; starting another
collection does not replay earlier records.

## Delivery and cleanup

Each subscription starts with future records; it does not replay history.
A timeout means no matching record arrived during that wait. Receive again
when needed. A slow subscriber can lose records; see
[delivery and loss](data-model.md#live-delivery) for the bounds and counters.

Serialize calls and close on each subscription. Close subscriptions before
detaching the SDK context. If native close returns an error, the local handle
is still consumed; do not retry that handle. The Agent continues cleanup.
For an explicit cancellation token, signal and join its wait before closing
the token; see [cancellation contracts](../reference/c/wait.md).

See [event data and delivery](data-model.md) for record layout, timestamps, and counter units, or [event API details](../reference/c/events.md).
