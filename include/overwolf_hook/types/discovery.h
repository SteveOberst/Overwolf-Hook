#ifndef OVERWOLF_HOOK_TYPES_DISCOVERY_H
#define OVERWOLF_HOOK_TYPES_DISCOVERY_H
#include <overwolf_hook/types/common.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct owh_channels owh_channels;
enum { OWH_CHANNEL_INFO = 1u, OWH_CHANNEL_EVENTS = 2u };

/** Original transport identity and observed mapping kinds. Presence does not
 * certify readability, compatibility, producer health, a game ID or a live match.
 * Discovery is a non-atomic enumeration; objects may disappear at any time.
 */
typedef struct OWH_META("model=SourceChannel;output") owh_source_channel {
    uint32_t source_channel;
    uint32_t kinds;
    /** Producer PID reported by Overwolf; zero for a classic unassociated source.
     * Use it to distinguish producers sharing the same channel. */
    uint32_t producer_process_id;
} owh_source_channel;

#ifdef __cplusplus
}
#endif
#endif
