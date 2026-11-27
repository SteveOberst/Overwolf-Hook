#ifndef OVERWOLF_HOOK_TYPES_AGENT_H
#define OVERWOLF_HOOK_TYPES_AGENT_H
#include <overwolf_hook/types/client.h>
#include <overwolf_hook/types/common.h>
#include <overwolf_hook/types/build_info.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { OWH_AGENT_STARTING = 1u, OWH_AGENT_READY = 2u };
/** Immutable resident Agent identity. Query never injects, installs hooks, or
 * creates an overlay. SDK version/build are available even on a pair mismatch. */
typedef struct OWH_META("model=AgentInfo;output;size=size") owh_agent_info {
    uint32_t size, abi_version, wire_version, server_state;
    uint32_t process_id, reserved;
    uint64_t process_started, instance_id;
    owh_build_info build_info;
} owh_agent_info;
#ifdef __cplusplus
}
static_assert(sizeof(owh_agent_info) == 176);
#endif
#endif
