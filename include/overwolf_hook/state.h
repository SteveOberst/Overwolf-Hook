#ifndef OVERWOLF_HOOK_STATE_H
#define OVERWOLF_HOOK_STATE_H

#include <overwolf_hook/bridge.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct owh_state owh_state;

/** One original InfoDB field. Pointers belong to the immutable state and remain
 * valid until owh_state_close. Names are NUL-terminated provider bytes; values
 * have an explicit length and may contain NULs or non-text data. producer_ticks
 * and reserved_bytes are original protocol values, not UTC or gameplay units.
 * A zero timestamp/length is preserved; it does not establish an observed empty
 * gameplay value. Interpretation belongs to the game's consumer.
 */
typedef struct owh_state_field {
    const char* category;
    const char* key;
    const void* bytes;
    uint64_t producer_ticks;
    uint32_t size;
    uint32_t reserved_bytes;
} owh_state_field;

/** Copy an existing classic ow_info_<source_channel> database under its existing
 * mutex. No injection, DLL loading in Overwolf, SDK registration, or notification
 * waits/signals. Timeout is 0..1000 milliseconds. Every mapping/mutex handle is
 * released before return; the returned object owns its copied values.
 *
 * OK with count 0 is a confirmed structurally empty database. NOT_FOUND means
 * the mapping or its mutex is missing, BUSY means lock timeout, CORRUPT means
 * invalid bounds/metadata or an abandoned mutex. No partial state is returned.
 * Unknown ABI returns INCOMPATIBLE. Channels are provider transport identities,
 * not public game IDs. Mapping presence does not prove a current/live match.
 * This bounded reader supports the reviewed classic InfoDB layout (<=4 MiB,
 * <=4096 fields). It does not claim compatibility with other transports.
 */
OWH_CLIENT owh_result __cdecl owh_state_read(uint32_t abi_version,
    uint32_t source_channel, uint32_t timeout_ms, owh_state** output);

/** Immutable field count. Null returns 0; use read's result to establish availability. */
OWH_CLIENT uint32_t __cdecl owh_state_count(const owh_state* state);

/** Borrow one field. Out-of-range/null arguments return INVALID_ARGUMENT. */
OWH_CLIENT owh_result __cdecl owh_state_at(const owh_state* state,
    uint32_t index, owh_state_field* output);

/** Frees the owned copy. Null is accepted; borrowed pointers become invalid. */
OWH_CLIENT void __cdecl owh_state_close(owh_state* state);

#ifdef __cplusplus
}
#endif
#endif
