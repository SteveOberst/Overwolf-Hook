#ifndef OVERWOLF_HOOK_TYPES_COMMON_H
#define OVERWOLF_HOOK_TYPES_COMMON_H

#include <stddef.h>
#include <stdint.h>

/* Compiler-readable metadata for the generated JVM views. It has no ABI
 * effect and is ignored by non-Clang compilers. */
#if defined(__clang__)
#define OWH_META(value) __attribute__((annotate(value)))
#else
#define OWH_META(value)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define OWH_ABI_VERSION 1u
/** Raw C handles have no ownership gate. Serialize ordinary calls and close;
 * only signaling a wait token may run alongside waits borrowing it. Join those
 * waits before closing the token. */

typedef int32_t owh_status;
enum {
    OWH_OK = 0,
    OWH_NO_DATA = 1,
    OWH_CLOSED = 2,
    OWH_NOT_FOUND = 3,
    OWH_BUSY = 4,
    OWH_BUFFER_TOO_SMALL = 5,
    OWH_INVALID_ARGUMENT = 6,
    OWH_ALREADY_EXISTS = 7,
    OWH_INCOMPATIBLE = 8,
    OWH_CORRUPT = 9,
    OWH_SYSTEM_ERROR = 10,
    OWH_RESOURCE_EXHAUSTED = 11,
    OWH_CANCELLED = 12,
    OWH_TIMEOUT = 13,
    OWH_UNSUPPORTED = 14,
    OWH_AGENT_NOT_READY = 15,
    OWH_AGENT_LOST = 16,
    OWH_STALE_GENERATION = 17,
    OWH_DEVICE_LOST = 18,
    OWH_REPLACED = 20,
    OWH_OPT_IN_REQUIRED = 21
};
#define OWH_WAIT_INFINITE UINT32_MAX

enum { OWH_ERROR_NONE = 0, OWH_ERROR_WIN32 = 1, OWH_ERROR_HRESULT = 2 };
enum {
    OWH_STAGE_NONE = 0,
    OWH_STAGE_ARGUMENT = 1,
    OWH_STAGE_TARGET_DISCOVERY = 2,
    OWH_STAGE_BOOTSTRAP = 3,
    OWH_STAGE_HANDSHAKE = 4,
    OWH_STAGE_PROFILE_RESOLVE = 5,
    OWH_STAGE_AGENT_LIFETIME = 6,
    OWH_STAGE_CAPABILITY = 7,
    OWH_STAGE_ALLOCATION = 8,
    OWH_STAGE_WINDOW = 9,
    OWH_STAGE_SOURCE_REGISTER = 10,
    OWH_STAGE_SOURCE_PUBLISH = 11,
    OWH_STAGE_D3D11_ADAPTER = 12,
    OWH_STAGE_D3D11_DEVICE = 13,
    OWH_STAGE_D3D11_IMPORT = 14,
    OWH_STAGE_D3D11_VALIDATE = 15,
    OWH_STAGE_CONTROL_IO = 16,
    OWH_STAGE_CLEANUP = 17,
    OWH_STAGE_WAIT = 18,
    OWH_STAGE_OVERWOLF_START = 19,
    OWH_STAGE_OVERWOLF_STOP = 20
};
/** Stable ABI status, stage and original unsigned native error. Size is 24 bytes. */
typedef struct OWH_META("model=Result;output") owh_result {
    owh_status status;
    uint32_t failure_stage;
    uint32_t error_domain;
    uint32_t reserved;
    uint64_t native_code;
} owh_result;

#ifdef __cplusplus
}
static_assert(sizeof(owh_result) == 24);
static_assert(offsetof(owh_result, native_code) == 16);
#else
_Static_assert(sizeof(owh_result) == 24, "result size");
#endif
#endif
