# Game state: C ABI

Look up the operations declared in `<overwolf_hook/abi/game_state.h>`.
See the [C ABI index](../native-api.md) for linking and shared result/ownership
rules, or [data records](../data-records.md) for field meanings.

For usage, see [reading state](../../game-state/reading-state.md).

## `owh_game_state_sources`

Returns a point-in-time inventory of existing source channels without an SDK context or Agent attachment:

```c
--8<-- "docs/assets/generated/reference/owh_game_state_sources.h"
```

Supply a writable source-list handle output. Returns `OK` with an owned list, which may be empty, or enumeration/allocation failure. No Agent is required.

## `owh_source_list_count`

Gets the number of entries in an owned discovery list:

```c
--8<-- "docs/assets/generated/reference/owh_source_list_count.h"
```

Returns the `uint32_t` count directly; null returns zero. No `owh_result` or output record is involved.

## `owh_source_list_at`

Gets one source-channel entry by zero-based index:

```c
--8<-- "docs/assets/generated/reference/owh_source_list_at.h"
```

Supply a list, an index below its count, and writable `owh_source_channel` (no `.size`). Returns `OK` with a copied entry; `INVALID_ARGUMENT` for a null list/output or an out-of-range index.

## `owh_source_list_close`

Releases a discovery list:

```c
--8<-- "docs/assets/generated/reference/owh_source_list_close.h"
```

Returns nothing. Null is accepted. Consumes the owned point-in-time list.

## `owh_game_state_read`

Copies bounded fields from the existing provider mapping; timeout is 0 through 1000 ms. Zero attempts the mutex without waiting. No SDK context is required:

```c
--8<-- "docs/assets/generated/reference/owh_game_state_read.h"
```

Supply a nonzero source channel, timeout 0 to 1000 ms, and writable snapshot handle output. Returns `OK` with an owned snapshot (possibly empty), `NOT_FOUND` for missing storage, `BUSY` for its lock deadline, `CORRUPT` for invalid storage/abandoned lock, or an argument/native error. No partial snapshot is returned.

## `owh_game_state_read_from`

Copies a snapshot from the selected producer on a channel:

```c
--8<-- "docs/assets/generated/reference/owh_game_state_read_from.h"
```

Supply the discovered `source_channel` and `producer_process_id`, a timeout
0 to 1000 ms, and a writable snapshot handle output. Producer PID zero uses the
channel-only selection of `owh_game_state_read`; multiple current producers
for that channel return `INVALID_ARGUMENT`. An explicit producer that is no
longer present returns `NOT_FOUND`. Other timeout, snapshot ownership, and
read-error rules match `owh_game_state_read`. The SDK never opens the producer
process. A failure to resolve required Overwolf source metadata is an error,
not a successful empty snapshot.

## `owh_snapshot_count`

Gets the number of fields in an owned snapshot:

```c
--8<-- "docs/assets/generated/reference/owh_snapshot_count.h"
```

Returns the `uint32_t` field count directly; null returns zero.

## `owh_snapshot_at`

Gets one field by zero-based index:

```c
--8<-- "docs/assets/generated/reference/owh_snapshot_at.h"
```

Supply a snapshot, an index below its count, and writable `owh_state_field` (no output `.size` initialization). Returns `OK`, or `INVALID_ARGUMENT` for null/out-of-range input. The field pointers borrow from the snapshot; `field.size` is its value byte length.

## `owh_snapshot_close`

Releases a snapshot:

```c
--8<-- "docs/assets/generated/reference/owh_snapshot_close.h"
```

Returns nothing. Null is accepted. Consumes the snapshot and invalidates its borrowed field pointers.

State records preserve provider category, key, value bytes, and provider ticks.
They do not imply a text encoding. The mapping may disappear after discovery;
a successful empty snapshot differs from a failed read.

For workflow examples see [game state](../../game-state/reading-state.md),
[events](../../game-events/index.md), and [overlays](../../overlays/index.md).
