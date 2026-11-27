#ifndef OVERWOLF_HOOK_ABI_SDK_H
#define OVERWOLF_HOOK_ABI_SDK_H
#include <overwolf_hook/abi/export.h>
#include <overwolf_hook/types/agent.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct owh_sdk owh_sdk;
/** Each context has independent attach state. Agent code remains resident after
 * detach; old resources cannot migrate to a later attachment. */
/** Creates an independent context and clears output before work. Ownership is
 * transferred only on success. The call does not attach or start feature hooks.
 */
OWH_CLIENT owh_result __cdecl owh_sdk_open(owh_sdk **output);
/** Closes the context and consumes it. Agent code remains resident in Overwolf.
 */
OWH_CLIENT void __cdecl owh_sdk_close(owh_sdk *);
/** Attaches only to the identity-checked Overwolf main process. NULL options
 * normally restart Overwolf and require the early startup boundary; a missed
 * boundary fails rather than falling back. OWH_ATTACH_HOT uses a running process
 * without restarting it and accepts a potentially crashing initial race.
 * Normal exit can time out or be unsupported; no forced termination is used.
 * Detach/close never stops the launched Overwolf process.
 */
OWH_CLIENT owh_result __cdecl owh_sdk_attach(owh_sdk *, const owh_client_options *);
/** Ends the active session locally even when remote cleanup reports an error.
 * Resources from that session do not migrate to a later attach.
 */
OWH_CLIENT owh_result __cdecl owh_sdk_detach(owh_sdk *);
/** Read-only point-in-time preflight, independent of SDK context and attach. OK means a report
 * was produced even if checks failed. No session, hook, window, GPU device, or
 * remote thread is created. Output size is validated before writing.
 */
OWH_CLIENT owh_result __cdecl owh_sdk_check(const owh_client_options *,
                                            owh_client_sanity_report *);
/** Reads resident Agent identity without SDK context, attaching, or starting it. */
OWH_CLIENT owh_result __cdecl owh_sdk_probe_agent(const owh_client_options *,
                                                  owh_agent_info *);
/** Returns cached attachment identity after a local connection-lifetime check.
 * Sends no Agent request; use probe_agent for a passive resident-Agent query. */
OWH_CLIENT owh_result __cdecl owh_sdk_agent_info(owh_sdk *, owh_agent_info *);
/** One bounded liveness request; does not start a recurring heartbeat and
 * does not certify capture, graphics availability, or visible presentation.
 * An exchange timeout closes the entire session and retires its resources.
 * A timeout at the local request gate before exchange leaves it open.
 * timeout_ms is finite (zero is accepted); INFINITE is INVALID_ARGUMENT. */
OWH_CLIENT owh_result __cdecl owh_sdk_ping(owh_sdk *, uint32_t timeout_ms);
OWH_CLIENT uint32_t __cdecl owh_sdk_abi_version(void);
/** Copies the loaded Client's SDK version/build number; does not attach. */
OWH_CLIENT owh_result __cdecl owh_sdk_build_info(owh_build_info *);
OWH_CLIENT void __cdecl owh_client_options_init(owh_client_options *);
#ifdef __cplusplus
}
#endif
#endif
