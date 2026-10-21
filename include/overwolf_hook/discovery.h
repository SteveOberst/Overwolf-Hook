#ifndef OVERWOLF_HOOK_DISCOVERY_H
#define OVERWOLF_HOOK_DISCOVERY_H
#include <overwolf_hook/bridge.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct owh_channels owh_channels;
enum { OWH_CHANNEL_INFO = 1u, OWH_CHANNEL_EVENTS = 2u };

/** Original transport identity and observed mapping kinds. Presence does not
 * certify readability, compatibility, producer health, a game ID or a live match.
 * Discovery is a non-atomic enumeration; objects may disappear at any time.
 */
typedef struct owh_source_channel {
    uint32_t source_channel;
    uint32_t kinds;
} owh_source_channel;

/** Enumerate exact classic ow_info_<id>/ow_events_<id> Section objects in the
 * current Windows session. No brute-force IDs, foreign process reads, injection,
 * SDK registration or provider object creation. No provider handles are retained.
 * OK with zero entries is a successful empty enumeration; directory access or
 * unsupported OS APIs return an explicit error, never a successful empty list.
 * Re-enumerate to observe added/removed channels. Results are sorted by ID.
 */
OWH_CLIENT owh_result __cdecl owh_channels_discover(uint32_t abi_version, owh_channels** output);
OWH_CLIENT uint32_t __cdecl owh_channels_count(const owh_channels* channels);
OWH_CLIENT owh_result __cdecl owh_channels_at(const owh_channels* channels,
    uint32_t index, owh_source_channel* output);
OWH_CLIENT void __cdecl owh_channels_close(owh_channels* channels);

#ifdef __cplusplus
}
#endif
#endif
