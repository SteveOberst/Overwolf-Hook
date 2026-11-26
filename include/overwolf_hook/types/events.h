#ifndef OVERWOLF_HOOK_TYPES_EVENTS_H
#define OVERWOLF_HOOK_TYPES_EVENTS_H

#include <overwolf_hook/types/client.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Current event-source readiness. This read-only probe neither installs hooks
 * nor waits on overlay work. A successful probe is no delivery guarantee. */
typedef struct OWH_META("model=EventCapabilities;output;size=size") owh_event_capabilities {
    uint32_t size, process_id;
    uint64_t process_started, session_id;
    owh_result status;
} owh_event_capabilities;

typedef struct owh_subscription owh_subscription;

enum {
    OWH_SUBSCRIPTION_STARTING = 1u,
    OWH_SUBSCRIPTION_ACTIVE = 2u,
    OWH_SUBSCRIPTION_STOPPING = 3u,
    OWH_SUBSCRIPTION_LOST = 4u,
    OWH_SUBSCRIPTION_FAILED = 5u
};

enum { OWH_EVENTS_ALL_CHANNELS = 0u };
enum {
    OWH_SUBSCRIPTION_WARNING_UNREVIEWED_CONSUMER = 1u,
    OWH_SUBSCRIPTION_WARNING_HASH_UNAVAILABLE = 2u
};

/** Complete copied record; raw bytes contain the original name/payload, not
 * canonical game semantics. lost_before reports ring overwrite and known capture
 * ingress/publication loss before this record. Ingress losses happen before
 * subscriber filtering, so a filtered subscription's loss count is conservative. */
typedef struct OWH_META("model=Event;output") owh_event {
    uint64_t sequence, lost_before, captured_qpc;
    uint32_t source_channel, producer_process_id, raw_size;
    unsigned char raw[255 + 63 * 1024];
} owh_event;
typedef struct OWH_META("model=EventOptions;input;init=owh_event_options_init;size=size")
    owh_event_options {
    uint32_t size;
    /** Overwolf event channel, or OWH_EVENTS_ALL_CHANNELS for all channels
     *  read by the reviewed consumer. This is not a public Overwolf game ID.
     */
    uint32_t source_channel;
    /** Optional producing game-process identity reported by Overwolf; zero
     * accepts every producer. This does not open or inspect that process. */
    uint32_t producer_process_id;
} owh_event_options;

typedef struct OWH_META("model=SubscriptionInfo;output;size=size") owh_subscription_info {
    uint32_t size;
    uint32_t state;
    uint32_t hook_enabled;
    uint32_t warnings;
    uint64_t qpc_frequency;
    uint64_t started_qpc;
    uint64_t started_utc_filetime;
    owh_result last_result;
} owh_subscription_info;

#define OWH_EVENT_OPTIONS_INIT {sizeof(owh_event_options), OWH_EVENTS_ALL_CHANNELS}
#ifdef __cplusplus
}
static_assert(sizeof(owh_event_capabilities) == 48);
static_assert(sizeof(owh_event) == 64808);
static_assert(sizeof(owh_event_options) == 12);
static_assert(sizeof(owh_subscription_info) == 64);
#endif
#endif
