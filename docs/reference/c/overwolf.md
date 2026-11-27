---
title: "Overwolf management: C ABI reference"
description: "Look up C operations for discovering Overwolf's installation and process, reading versions, launching it and requesting normal exit."
---

# Overwolf management: C ABI

Look up the operations declared in `<overwolf_hook/abi/overwolf.h>`.
See the [C ABI index](../native-api.md) for linking and shared result/ownership
rules, or [data records](../data-records.md) for field meanings.

These operations require no SDK context or Agent connection. Initialize each
output record's `size` to `sizeof(record)` before calling.

For usage, see [Overwolf management](../../overwolf.md).

## `owh_overwolf_installation_query`

Returns installation directory, launcher path and installed product version. `NOT_FOUND` means installation discovery found no usable installation:

```c
--8<-- "docs/assets/generated/reference/owh_overwolf_installation_query.h"
```

Initialize `output.size`. Returns `OK` with UTF-8 paths/version, `NOT_FOUND` when no usable installation is found, or a discovery error.

## `owh_overwolf_process_query`

Returns the main-process PID, creation time, executable and version. PID `0` discovers one main process; multiple matches are `INVALID_ARGUMENT`. A nonzero creation time must match:

```c
--8<-- "docs/assets/generated/reference/owh_overwolf_process_query.h"
```

Initialize `output.size`. Returns `OK` with a verified identity, `NOT_FOUND` for absence, or `INVALID_ARGUMENT` for an ambiguous/mismatched selection. Access/query failures remain errors.

## `owh_overwolf_start`

Returns the current main process or launches the installed launcher and waits for a verified main process. Does not attach:

```c
--8<-- "docs/assets/generated/reference/owh_overwolf_start.h"
```

Initialize `output.size`; timeout must be finite and positive. Returns `OK` with the verified process, `NOT_FOUND` for a missing installation, or a launch/discovery/`TIMEOUT` error. Timeout does not undo a launch.

## `owh_overwolf_stop`

Revalidates a nonzero PID/creation-time pair, requests cooperative shutdown, and waits for exit. Never forces termination:

```c
--8<-- "docs/assets/generated/reference/owh_overwolf_stop.h"
```

Supply a nonzero PID and creation time and a finite positive timeout. Returns `OK` on observed exit; `NOT_FOUND` for absence, `INVALID_ARGUMENT` for a mismatched or ambiguous identity, `INCOMPATIBLE` for an image outside the installed directory, `UNSUPPORTED` for an unavailable shutdown event, or `TIMEOUT` for the exit deadline. Native access failures remain errors. The global request is sent only to the sole installed main process. Timeout does not undo a shutdown request.

Start/stop timeouts must be 1 through 4294967294 milliseconds. `TIMEOUT` ends
the wait; a launch or shutdown request can still take effect afterward.
`UNSUPPORTED` from stop means no usable shutdown event was found. Queries
distinguish absence from access/validation failures. Records are point-in-time
values, not owning process handles. Installation and running-process versions
can differ; neither is the SDK version. See [managing Overwolf](../../overwolf.md)
for task examples and further error behavior.
