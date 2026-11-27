---
title: "Read game-state snapshots"
description: "Read state fields already published by Overwolf, select a provider source and interpret snapshots without assuming freshness or universal game support."
---

<a id="read-the-latest-game-state"></a>
# Read available game state

Read state fields already published by Overwolf for a supported game, for example
to populate your application's interface. A read copies the available fields; it
does not wait for an update or establish that those values are recent.

A state read returns a **snapshot**: a copy of the fields available at that moment. An event instead describes something Overwolf observed changing. Use [events](../game-events/index.md) when you want to react to those observations; read state when you want the values available now.

If you use both, see [state and event correlation](../game-events/data-model.md#combine-events-with-current-values).
The SDK does not pair a snapshot with a particular event or guarantee that
their data was produced at the same time.

Overwolf's game-specific integrations, called providers, publish this data. A **channel** is the numbered data source the library can read. You discover the available channels rather than guessing a number; a channel number is not an Overwolf game ID. The `OWH_CHANNEL_INFO` flag in the API identifies channels that contain state fields.

This feature reads data already exposed by Overwolf. `sdk.gameState` is a passive controller; its `sources()` and `read(channel, timeout)` operations do not require `attach()`, create an overlay, or start an event subscription. Run Overwolf with a supported game to make the game's available data accessible.

## Select a source

Discovery returns each channel with its observed kinds and, when Overwolf
associates it with a producer, `producer_process_id`. This PID distinguishes
two producers sharing a channel; it does not cause the SDK to access a game
process. A classic named source without an association reports zero.

Pass the discovered record to `read(source)` or `storage(source)` in
C++/Java/Kotlin to preserve that selection. In C, call
`owh_game_state_read_from(source.source_channel, source.producer_process_id,
timeout_ms, &snapshot)`. The channel-only calls remain available. When a
current channel has multiple producers, select one explicitly rather than
relying on an arbitrary match.

The Client reads the current mapping names and value-decoding metadata from
Overwolf main, then copies the existing state under its mutex. Some providers
use GUID names instead of `ow_info_` names. These are source details the SDK
resolves for you; you don't need to attach an Agent or connect to a game-side
provider. The returned values contain the provider data after its transport
decoding, without an SDK-imposed game schema.

## Read the available fields

The example finds state channels and prints each field's category, key, byte count and a hexadecimal value preview. A category groups related fields, a key names a field, and the value is its data. Field names and value formats depend on the game/provider; the library preserves the bytes instead of guessing their meaning.

Add the [dependency](../getting-started/installation.md) to your application first. Choose your language to view the code and run it as a separate console example.
C++ callers can use the passive [GameStateController API](../reference/cpp-api.md#game-state)
or compile this C program in a C target.

1. Start Overwolf.
2. Start the supported game whose data you want to inspect.
3. Save the program at the path in your language tab.
4. Select its entry point using that tab's CMake or Gradle configuration.
5. Run the program to print the discovered fields.
6. Optional: Run it again with `CATEGORY KEY` arguments to select one field.
   Use names from your game's Info Updates reference or the observed output.

=== "C"

    [View or download `read_state.c`](../assets/examples/c/read_state.c).

    To use the minimal CMake project from installation, save this program as `main.c`, then run:

    ```powershell
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    ./build/Release/my_app.exe
    ```

    ??? example "Complete source"

        ```c
        --8<-- "docs/assets/examples/c/read_state.c"
        ```

=== "Java"

    [View or download `ReadState.java`](../assets/examples/java/ReadState.java). Save it as `src/main/java/examples/state/ReadState.java`.

    For the Gradle `application` plugin, select this example's entry point:

    ```kotlin
    application { mainClass.set("examples.state.ReadState") }
    ```

    Run `./gradlew.bat run`. In an existing application, call the relevant operations from your own code instead of replacing your main class.

    ??? example "Complete source"

        ```java
        --8<-- "docs/assets/examples/java/ReadState.java"
        ```

=== "Kotlin"

    [View or download `ReadState.kt`](../assets/examples/kotlin/ReadState.kt). Save it as `src/main/kotlin/examples/state/ReadState.kt`.

    For the Gradle `application` plugin, select this example's entry point:

    ```kotlin
    application { mainClass.set("examples.state.ReadStateKt") }
    ```

    Run `./gradlew.bat run`. In an existing application, call the relevant operations from your own code instead of replacing your main class.

    ??? example "Complete source"

        ```kotlin
        --8<-- "docs/assets/examples/kotlin/ReadState.kt"
        ```

## Check the read result

Each output line shows a channel, category, key, value byte count and up to
16 value bytes in hexadecimal. The preview reads the actual value using its
explicit length; it does not assume that the value is a string. Category
and key previews are limited to 64 bytes each. Printable ASCII is shown directly;
quotes, backslashes and other bytes use `\xNN`, without assuming a text encoding.
The program reports **No state (INFO) channels** after filtering, or **snapshot
contains zero fields** for a successful empty read.

No state channels can mean no supported game is running or Overwolf has no data
exposed through this path. A source can also disappear between discovery and reading.

Native field pointers are valid until `owh_snapshot_close`; generated Java and Kotlin `StateField` values own copies. Values may contain zero bytes or non-text data: use the explicit byte length, not C-string functions. C++ `StateFieldView` retains its snapshot owner and exposes the canonical native record.

## Find the fields for your game

Overwolf documents persistent game data as **Info Updates**, separately from
one-time events. Its [Game Events Provider overview](https://dev.overwolf.com/ow-native/live-game-data-gep/live-game-data-gep-intro/)
links the supported games in its navigation. Open your game's page and look for
the **Info Updates** tables: they describe each field's category, key, values,
and game-specific limitations.

For example, the [League of Legends reference](https://dev.overwolf.com/ow-native/live-game-data-gep/supported-games/league-of-legends/)
documents these fields:

| Category | Key | Meaning |
| --- | --- | --- |
| `game_info` | `matchStarted` | Match-start flag; for example, the string `"true"` |
| `game_info` | `matchId` | Match identifier |
| `summoner_info` | `champion` | Selected champion |
| `summoner_info` | `region` | Player's region |
| `match_info` | `game_mode` | Game mode |

Use that reference to interpret data, not as a promise that every documented
field will be present. This library reads the provider's existing state; it does
not request all GEP features or supply a fixed schema. Enumerate your snapshot
first, handle missing fields, and decode each value according to the game's
reference. Entries listed only under **Events** are not automatically state fields.

## Select and use a value

The example's optional arguments filter the fields **after** each snapshot is
read. They do not request more data from Overwolf or enable a missing field.
Replace `CATEGORY` and `KEY` with the exact ASCII names observed in your output.
Use the full names without display quotes, `\xNN` escapes, or a truncation
marker (`...`); those are output formatting, not parts of the field names.

For example, if the League of Legends field is present, select `game_info matchStarted`.
With the native program, run `./build/Release/my_app.exe game_info matchStarted`.
For the Java or Kotlin Gradle application, run
`./gradlew.bat run --args="game_info matchStarted"`.
**Requested field not present** means these snapshots did not contain that
category/key; it is not a decoded `false` value.

The filter compares the ASCII field names used by this example. The value is
still a byte range: in C, use `field.bytes` with `field.size` while its snapshot
is alive; in Java/Kotlin, `field.getBytes()` / `field.bytes` supplies the owned
`ByteView`. Use `copyBytes()` when an application parser needs an array, or
read the view directly. Decode only according to the game's observed format;
the public Overwolf Info Updates schema is context, not a guarantee of this
private transport's encoding.

## Interpret read outcomes

Discovery is non-atomic, so a mapping can disappear before the read. `NOT_FOUND` means a required mapping or mutex is missing; `BUSY` means the bounded lock wait expired; `CORRUPT` means invalid bounds or metadata, including an abandoned mutex. These differ from a successful read with zero fields, which confirms a structurally empty database. No partial state is returned.

The reader supports the reviewed classic InfoDB layout (up to 4 MiB and 4096 fields). Presence does not prove a live match or recent update. Producer ticks and reserved lengths are protocol values, not UTC or gameplay units. Keep raw bytes for application-specific decoding. To receive live event records, see [subscribe and receive events](../game-events/index.md).

State reads without an explicit timeout use `OWH_STATE_DEFAULT_TIMEOUT_MS`
(100 ms) in C++, Java and Kotlin. Pass `0` explicitly for a nonblocking lock
attempt. The deadline applies to acquiring the provider's state mutex; it does
not wait for a newer game update.
Explicit state deadlines must be 0 through 1000 milliseconds; larger values
return `INVALID_ARGUMENT` rather than waiting longer.

## Next steps

Use [events](../game-events/index.md) to react to changes, or consult the
[C](../reference/c/game-state.md), [C++](../reference/cpp-api.md#game-state),
[Java](../reference/java-api.md#gamestatecontroller), and
[Kotlin](../reference/kotlin-api.md) references for integration into your application.
