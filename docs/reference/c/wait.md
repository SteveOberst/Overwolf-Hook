---
title: "Waits and cancellation: C ABI reference"
description: "Look up C wait and cancellation operations, timeout results and ownership requirements for interruptible SDK waits."
---

# Waits and cancellation: C ABI

Look up the operations declared in `<overwolf_hook/abi/wait.h>`.
See the [C ABI index](../native-api.md) for linking and shared result/ownership
rules, or [data records](../data-records.md) for field meanings.

For usage, see [tracked publication](../../overlays/pixel-buffers.md#track-publication-when-needed) and [shared wait ownership](../contracts.md#contexts-and-ownership).

## `owh_wait_options_init`

Initializes timeout and cancellation input:

```c
--8<-- "docs/assets/generated/reference/owh_wait_options_init.h"
```

Returns nothing. Supply writable storage for the corresponding input record; null is a no-op. Writes the native size/defaults. See [data records](../data-records.md#data-records).

## `owh_cancel_create`

Creates an owned cancellation token shared by waits:

```c
--8<-- "docs/assets/generated/reference/owh_cancel_create.h"
```

Supply a writable handle output. Returns `OK` with ownership of a one-shot token, `INVALID_ARGUMENT` for a null output, or an allocation/native event error.

## `owh_cancel_signal`

Signals a token to end borrowing waits:

```c
--8<-- "docs/assets/generated/reference/owh_cancel_signal.h"
```

Returns nothing. Null is accepted. Signals the one-shot token and may run beside waits borrowing it; it does not cancel the underlying publication.

## `owh_cancel_close`

Releases a cancellation token:

```c
--8<-- "docs/assets/generated/reference/owh_cancel_close.h"
```

Returns nothing. Null is accepted. Signal and join all borrowing waits first, then consume this local handle.
