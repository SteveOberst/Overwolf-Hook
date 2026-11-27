#ifndef OVERWOLF_HOOK_ABI_OVERWOLF_H
#define OVERWOLF_HOOK_ABI_OVERWOLF_H
#include <overwolf_hook/abi/export.h>
#include <overwolf_hook/types/overwolf.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Passive queries: NOT_FOUND means absent, not an access/query failure.
 * No SDK context or resident Agent is required. Initialize output.size.
 */
OWH_CLIENT owh_result __cdecl owh_overwolf_installation_query(owh_overwolf_installation *);
/** process_id=0 discovers exactly one main process. process_started=0 binds
 * the observed creation time. Ambiguous discovery is INVALID_ARGUMENT.
 */
OWH_CLIENT owh_result __cdecl owh_overwolf_process_query(uint32_t process_id,
                                                       uint64_t process_started,
                                                       owh_overwolf_process *);
/** Uses the installed launcher and waits up to timeout_ms for the verified
 * main process. Returns an already-running process without starting another.
 * Does not attach, load the Agent, or certify overlay readiness. Overwolf's
 * lifetime is independent of the caller; no custom parent/job is installed.
 */
OWH_CLIENT owh_result __cdecl owh_overwolf_start(uint32_t timeout_ms, owh_overwolf_process *);
/** Signals Overwolf's existing cooperative shutdown event and observes exit.
 * Revalidates the sole installed main process; no process-tree/name-wide kill.
 * An unavailable event returns UNSUPPORTED; an exit deadline returns TIMEOUT.
 */
OWH_CLIENT owh_result __cdecl owh_overwolf_stop(uint32_t process_id, uint64_t process_started,
                                              uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
