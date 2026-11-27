---
title: "Find, start and stop Overwolf"
description: "Find the Overwolf installation and running process, inspect its version, and request normal start or stop without attaching to the SDK Agent."
---

# Manage Overwolf

Use `OverwolfController` to find Overwolf's installation and running main process,
inspect its version, or request a normal start or stop. These are local operations:
you do not need to attach to the Agent or create an overlay first.

Choose the action your application needs:

- [Find the installation and installed version](#check-the-installation-and-version).
- [Find or revalidate a running process](#find-the-running-process).
- [Start Overwolf without attaching](#start-overwolf).
- [Request normal exit](#request-normal-exit).

## Before you begin

Add the [dependency](getting-started/installation.md) first. Java and Kotlin
reuse the selected native library, loading the bundled pair on first use.
The snippets are callable helpers; Java methods propagate loading/native
exceptions to their caller. C callers check the returned status. C++ callers can construct
`overwolf_hook::OverwolfController{}` directly; see the
[C++ reference](reference/cpp-api.md#overwolf-installation-and-process-control).

## Check the installation and version

Use the installation query to decide whether to offer Overwolf-dependent
features or ask the user to install Overwolf. It returns the installation
directory, launcher path and installed Overwolf version. Finding the
installation does not mean Overwolf is running. Query the installed location
and version:

=== "C"

    ```c
    --8<-- "docs/assets/examples/c/overwolf_control.c:installation"
    ```

=== "Java"

    ```java
    --8<-- "docs/assets/examples/java/ManageOverwolf.java:installation"
    ```

=== "Kotlin"

    ```kotlin
    --8<-- "docs/assets/examples/kotlin/ManageOverwolf.kt:installation"
    ```

The [process query](#find-the-running-process) returns the running executable's version instead.
After an update, it can differ from the installed version. Neither value is
the SDK version. An empty version field means the version metadata was
unavailable. Native text fields are NUL-terminated UTF-8; generated JVM byte
views provide `utf8()` for reading them as text.

## Find the running process

Query the verified main-process identity to check whether Overwolf is running.
These helpers return an absent result when it is not found; they do not launch it:

=== "C"

    ```c
    --8<-- "docs/assets/examples/c/overwolf_control.c:process"
    ```

=== "Java"

    ```java
    --8<-- "docs/assets/examples/java/ManageOverwolf.java:process"
    ```

=== "Kotlin"

    ```kotlin
    --8<-- "docs/assets/examples/kotlin/ManageOverwolf.kt:process"
    ```

Process and installation queries return point-in-time values. Java and Kotlin
reuse the selected DLLs, or load the bundled pair on first use. The controller owns no session and needs
no `close()`. It is also available through `sdk.overwolf()`.

An absent result means no matching installation or process was found. Access
errors remain errors. If more than one main process matches, select a PID
explicitly rather than relying on an arbitrary choice. Process descriptors
include creation time so a reused PID cannot select a different process.

The executable name only identifies candidates. The shared native selector
checks the x64 process, your Windows session and user, and the main application
package beside it (`OverWolf.Client.Core.dll` and `OWServer.dll`). This separates
the running application from the installation-root bootstrapper without pinning
a version directory or hash. It identifies the process role; it is not a
signature-authentication guarantee. Query, start, attach, and passive source
discovery use the same selector.

To revalidate a saved process descriptor, pass both its PID and creation time:
`runningProcess(process.getProcessId(), process.getProcessStarted())` in Java,
`runningProcess(process.processId, process.processStarted)` in Kotlin, or
`running_process(process.process_id, process.process_started)` in C++.
In C, pass those fields to `owh_overwolf_process_query` with an initialized
output record. PID `0` requests discovery; creation time `0` accepts the
creation time observed by this query. A mismatched nonzero creation time is
`INVALID_ARGUMENT`. Keep `process_started` as an identity token rather than
constructing it from a timestamp yourself.

## Start Overwolf

Use start when your application needs Overwolf running without attaching the
Agent, for example before passive state reads. These snippets only start or
find Overwolf; they do not stop it afterward.

=== "C"

    ```c
    --8<-- "docs/assets/examples/c/overwolf_control.c:start"
    ```

=== "Java"

    ```java
    --8<-- "docs/assets/examples/java/ManageOverwolf.java:start"
    ```

=== "Kotlin"

    ```kotlin
    --8<-- "docs/assets/examples/kotlin/ManageOverwolf.kt:start"
    ```

`start(timeout)` uses the installed launcher and waits for a verified main
process. If Overwolf is already running, it returns that process. The launcher
can delegate to another process; the SDK does not assume its PID is the main
process. Overwolf continues running when your application exits.

For an explicit timeout, use `start(20000)` in C++/Java/Kotlin or pass
`20000` as the first C argument. Timeouts are milliseconds, finite and positive
(1 through 4294967294). A start timeout does not undo a launch; query again
before deciding whether another action is needed.

## Request normal exit

Only stop Overwolf when your application intends to close it for the user.
This affects Overwolf and its other active integrations, not just your SDK
session. Close your overlays/subscriptions and detach before requesting exit.
These helpers query the current identity and request normal exit:

=== "C"

    ```c
    --8<-- "docs/assets/examples/c/overwolf_control.c:stop"
    ```

=== "Java"

    ```java
    --8<-- "docs/assets/examples/java/ManageOverwolf.java:stop"
    ```

=== "Kotlin"

    ```kotlin
    --8<-- "docs/assets/examples/kotlin/ManageOverwolf.kt:stop"
    ```

`stop(process, timeout)` signals Overwolf's existing cooperative shutdown event
and waits for that exact process to exit. It revalidates that the selected
process is the sole installed Overwolf main process before sending this global
request. `UNSUPPORTED` means the shutdown event is unavailable; `TIMEOUT` means
the process did not exit within the deadline. No forced termination follows.
Default C++/JVM timeouts are ten seconds; pass an explicit timeout in
milliseconds as the second argument to `stop`. A timeout ends the wait, not
the shutdown request. The process can still exit afterward; query again
to observe its current state. Stop revalidates PID and creation time before
acting and does not terminate a process tree.

Starting Overwolf does not attach the Agent or establish that its overlay
internals are ready. Use the separate SDK attachment and feature-capability
operations for those tasks. The controller does not install, update or monitor
Overwolf in the background.

## Complete sources and reference

Complete helper sources: [C](assets/examples/c/overwolf_control.c),
[Java](assets/examples/java/ManageOverwolf.java), and
[Kotlin](assets/examples/kotlin/ManageOverwolf.kt). Call only the operation
your application intends to perform; start/stop are separate active actions.


Look up exact arguments and return codes in the [Overwolf C ABI](reference/c/overwolf.md) or the [Java controller reference](reference/java-api.md#overwolfcontroller).
