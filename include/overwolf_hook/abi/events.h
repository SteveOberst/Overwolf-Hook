#ifndef OVERWOLF_HOOK_ABI_EVENTS_H
#define OVERWOLF_HOOK_ABI_EVENTS_H
#include <overwolf_hook/abi/sdk.h>
#include <overwolf_hook/types/events.h>
#include <overwolf_hook/abi/wait.h>
#ifdef __cplusplus
extern "C" {
#endif
OWH_CLIENT void __cdecl owh_event_options_init(owh_event_options *);
OWH_CLIENT owh_result __cdecl owh_event_query_capabilities(owh_sdk *, owh_event_capabilities *);
/** Opens an independent event stream. The Agent shares capture across
 * subscriptions. NULL options select all channels. Output transfers ownership
 * only after source preparation and stream allocation succeed.
 */
OWH_CLIENT owh_result __cdecl owh_event_subscribe(owh_sdk *, const owh_event_options *,
                                                  owh_subscription **);
/** Copies state, hook flag, warnings, last result, and capture clock anchor. */
OWH_CLIENT owh_result __cdecl owh_subscription_query(owh_subscription *, owh_subscription_info *);
/** Always consumes local subscription ownership, including remote failure. */
OWH_CLIENT owh_result __cdecl owh_subscription_close(owh_subscription *);
OWH_CLIENT owh_result __cdecl owh_subscription_poll(owh_subscription *, owh_event *);
/** NULL options wait indefinitely without cancellation. */
OWH_CLIENT owh_result __cdecl owh_subscription_receive(owh_subscription *,
                                                       const owh_wait_options *, owh_event *);
#ifdef __cplusplus
}
#endif
#endif
