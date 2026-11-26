#ifndef OVERWOLF_HOOK_TYPES_CLIENT_H
#define OVERWOLF_HOOK_TYPES_CLIENT_H

#include <overwolf_hook/types/common.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OWH_CLIENT_OPTIONS_INIT {sizeof(owh_client_options), OWH_ABI_VERSION}

enum { OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE = 1u };
/** RESTART is the default: request normal exit, launch Overwolf normally, then
 * arm the early startup boundary. Failure never silently falls back to HOT.
 * HOT uses a running process and accepts a potentially crashing initial race. */
enum { OWH_ATTACH_RESTART = 0u, OWH_ATTACH_HOT = 1u };

/** Explicit owner selection and per-session resource budgets. process_id=0
 * means discover exactly one Overwolf main process. process_started=0 binds the
 * observed creation FILETIME before any active access. Unknown flags are errors. NULL options
 * selects defaults, including RESTART. An explicit process selector identifies
 * the old process to restart; the new identity is discovered after launch.
 */
typedef struct OWH_META(
    "model=ClientOptions;input;init=owh_client_options_init;size=size;abi=abi_version")
    owh_client_options {
    uint32_t size;
    uint32_t abi_version;
    uint32_t process_id;
    uint32_t experimental_features;
    uint64_t process_started;
    uint64_t pixel_buffer_budget_bytes;
    uint64_t shared_texture_budget_bytes;
    uint32_t maximum_overlays;
    uint32_t maximum_receipts_per_overlay;
    uint32_t attach_mode;
} owh_client_options;

enum {
    OWH_CHECK_HOST_IDENTITY = 1u,
    OWH_CHECK_AGENT_MODULE = 2u,
    OWH_CHECK_AGENT_HASH = 4u,
    OWH_CHECK_COMMON_TARGETS = 8u,
    OWH_CHECK_LIFECYCLE_TARGETS = 16u,
    OWH_CHECK_PIXEL_BUFFER_TARGETS = 32u,
    OWH_CHECK_SHARED_TEXTURE_TARGETS = 64u
};
enum { OWH_BUILD_UNREVIEWED = 0u, OWH_BUILD_REVIEWED = 1u };

/** Read-only, point-in-time preflight, independent of Attach and shared-texture opt-in.
 * checked & ~passed identifies failed checks; absent bits mean NOT CHECKED.
 * AGENT_HASH passes when a digest could be read, including unknown digests.
 * reviewed_build is meaningful only when AGENT_HASH passed, and refers to this
 * library's retained build review, not Overwolf
 * endorsement. Unknown versions/hashes never prohibit use.
 *
 * Target checks inspect the on-disk module mapped without executing it: unique
 * signatures, exports, function bounds and cross-references. They do not prove
 * every private object field, loaded-memory identity, GPU adapter/settings,
 * permissions needed by a later injection, capture compatibility, or visibility.
 * Feature activation rechecks its required targets in loaded memory. A failed required check
 * diagnoses a blocker for that path at this instant; a pass is no safety or
 * functionality guarantee. Hardware/software blockers without an implemented
 * detector are not reported as checked. No game process is inspected.
 */
typedef struct OWH_META("model=ClientSanityReport;output;size=size") owh_client_sanity_report {
    uint32_t size;
    uint32_t checked;
    uint32_t passed;
    uint32_t reviewed_build;
    uint32_t process_id;
    uint32_t reserved;
    uint64_t process_started;
    uint8_t agent_sha256[32];
    owh_result host_status;
    owh_result module_status;
    owh_result profile_status;
    owh_result hash_status;
} owh_client_sanity_report;

#ifdef __cplusplus
}
static_assert(sizeof(owh_client_options) == 56);
static_assert(sizeof(owh_client_sanity_report) == 160);
#endif
#endif
