#ifndef OVERWOLF_HOOK_TYPES_STATE_H
#define OVERWOLF_HOOK_TYPES_STATE_H

#include <overwolf_hook/types/common.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct owh_state owh_state;

/** Lock deadline used by convenience reads in every language. C calls still
 * pass their chosen timeout explicitly; zero performs a nonblocking read. */
enum { OWH_STATE_DEFAULT_TIMEOUT_MS = 100u };

/** One original InfoDB field. Pointers belong to the immutable state and remain
 * valid until owh_snapshot_close. Names are NUL-terminated provider bytes; values
 * have an explicit length and may contain NULs or non-text data. producer_ticks
 * and reserved_bytes are original protocol values, not UTC or gameplay units.
 * A zero timestamp/length is preserved; it does not establish an observed empty
 * gameplay value. Interpretation belongs to the game's consumer.
 */
typedef struct OWH_META("model=StateField;output") owh_state_field {
    const char *category OWH_META("cstring;max=4194304;copy=owned");
    const char *key OWH_META("cstring;max=4194304;copy=owned");
    const void *bytes OWH_META("span=size;copy=owned;max=4194304");
    uint64_t producer_ticks;
    uint32_t size;
    uint32_t reserved_bytes;
} owh_state_field;

#ifdef __cplusplus
}
#endif
#endif
