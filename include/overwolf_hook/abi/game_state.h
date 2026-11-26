#ifndef OVERWOLF_HOOK_ABI_GAME_STATE_H
#define OVERWOLF_HOOK_ABI_GAME_STATE_H
#include <overwolf_hook/abi/export.h>
#include <overwolf_hook/types/discovery.h>
#include <overwolf_hook/types/state.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Passive operations: no SDK context, Agent attachment, hooks, injection, or provider
 * object creation. Discovery is point-in-time; channels can disappear before
 * a later read. A successful empty list is distinct from an enumeration error.
 */
OWH_CLIENT owh_result __cdecl owh_game_state_sources(owh_channels **);
/** Returns count for this owned point-in-time source list; null returns zero. */
OWH_CLIENT uint32_t __cdecl owh_source_list_count(const owh_channels *);
OWH_CLIENT owh_result __cdecl owh_source_list_at(const owh_channels *, uint32_t index,
                                                 owh_source_channel *);
OWH_CLIENT void __cdecl owh_source_list_close(owh_channels *);
/** Copies an existing provider InfoDB mapping under its mutex. Timeout is
 * 0..1000 ms. No mapping/mutex handle remains open on return. OK with zero
 * fields is a structurally empty database. NOT_FOUND means mapping/mutex
 * missing; BUSY means lock deadline; CORRUPT means invalid bounds/metadata or
 * abandoned mutex. No partial snapshot is returned. Channel is not a game ID.
 * Supports the reviewed layout up to 4 MiB and 4096 fields. Current source
 * names and value decoding metadata are read passively from Overwolf main;
 * classic named mappings remain readable without a resident Agent.
 */
OWH_CLIENT owh_result __cdecl owh_game_state_read(uint32_t source_channel,
                                                  uint32_t timeout_ms, owh_state **);
/** Selects one Overwolf-reported producer when several share a channel.
 * Zero producer_process_id selects the unique source for the channel; an
 * ambiguous channel returns INVALID_ARGUMENT. Never opens the producer process.
 * Other snapshot/timeout/ownership rules match owh_game_state_read. */
OWH_CLIENT owh_result __cdecl owh_game_state_read_from(uint32_t source_channel,
    uint32_t producer_process_id, uint32_t timeout_ms, owh_state **);
/** Returns field count for the owned snapshot; null returns zero. */
OWH_CLIENT uint32_t __cdecl owh_snapshot_count(const owh_state *);
/** Pointers in the returned field remain valid until snapshot_close. */
OWH_CLIENT owh_result __cdecl owh_snapshot_at(const owh_state *, uint32_t index, owh_state_field *);
OWH_CLIENT void __cdecl owh_snapshot_close(owh_state *);
#ifdef __cplusplus
}
#endif
#endif
