# Events: C ABI

Look up the operations declared in `<overwolf_hook/abi/events.h>`.
See the [C ABI index](../native-api.md) for linking and shared result/ownership
rules, or [data records](../data-records.md) for field meanings.

For usage, see [event subscription](../../game-events/index.md).

## `owh_event_options_init`

Initializes event filter defaults:

```c
--8<-- "docs/assets/generated/reference/owh_event_options_init.h"
```

Returns nothing. Supply writable storage for the corresponding input record; null is a no-op. Writes the native size/defaults. See [data records](../data-records.md#data-records).

## `owh_event_query_capabilities`

Reads current event capture capability:

```c
--8<-- "docs/assets/generated/reference/owh_event_query_capabilities.h"
```

Requires an attachment; initialize `output.size`. Outer `OK` returns the snapshot; its `status` describes capture readiness. It installs no hook and guarantees no delivery.

`AGENT_NOT_READY` means Overwolf has not loaded its event consumer yet; query
again after initialization. `INCOMPATIBLE` means a loaded consumer does not
match the required targets. An unreviewed file hash alone does not make the
consumer incompatible or reject subscriptions.

## `owh_event_subscribe`

Opens an independent event stream; null options select all channels:

```c
--8<-- "docs/assets/generated/reference/owh_event_subscribe.h"
```

Requires an attachment and writable handle output; options may be null. Returns `OK` with an independent live stream, or filter, source-readiness, resource or session/transport failure. An unreviewed-hash warning alone does not reject a stream.

## `owh_subscription_query`

Reads subscription state, hook flag, warnings, clock anchor, and last result:

```c
--8<-- "docs/assets/generated/reference/owh_subscription_query.h"
```

Requires a subscription; initialize `output.size`. Returns `OK` with state/warnings/clock anchor/last result, or a subscription/session error.

## `owh_subscription_poll`

Consumes the next matching event without waiting:

```c
--8<-- "docs/assets/generated/reference/owh_subscription_poll.h"
```

Supply writable `owh_event` (no `.size` initialization). Returns `OK` with a copied record, `NO_DATA` when none matches, `BUSY` on short ring contention, or a closed/lost/corrupt stream error.

## `owh_subscription_receive`

Consumes a matching event with optional timeout/token; null options wait indefinitely without cancellation:

```c
--8<-- "docs/assets/generated/reference/owh_subscription_receive.h"
```

Supply writable `owh_event`; initialize any wait options. Null options wait indefinitely. Returns `OK` with a copied record, `TIMEOUT`, `CANCELLED`, or a stream/native wait error. Read the event only on `OK`.

## `owh_subscription_close`

Ends the live stream and consumes local ownership; there is no retained drain guarantee:

```c
--8<-- "docs/assets/generated/reference/owh_subscription_close.h"
```

Requires a valid subscription handle. Returns the cleanup result and consumes local ownership even on remote failure. No replay or drain is promised.

`owh_event_capabilities.status` diagnoses event-source readiness independently
of overlay rendering. The query does not install hooks. A successful result
does not guarantee that this game will produce events.

`owh_event` is a copied record with sequence, `lost_before`, capture QPC,
source channel, raw byte count, and bounded raw bytes. It does not decode the
game/provider schema. `lost_before` reports known stream and SDK-side losses,
not loss before the hook.

Capture stops when there are no active subscriptions. The shared ring is capped
at 4 MiB and 64 slots; ingress is capped at 1 MiB and 16 entries. Loss is global
and carried to the next matching event, so a filtered subscription can report
loss from other channels.
