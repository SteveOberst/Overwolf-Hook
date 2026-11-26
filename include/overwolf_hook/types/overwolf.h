#ifndef OVERWOLF_HOOK_TYPES_OVERWOLF_H
#define OVERWOLF_HOOK_TYPES_OVERWOLF_H
#include <overwolf_hook/types/common.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { OWH_OVERWOLF_PATH_BYTES = 4096, OWH_OVERWOLF_VERSION_BYTES = 64 };

/** Point-in-time installation discovery. Paths are NUL-terminated UTF-8.
 * This is Overwolf's version, not the SDK version. An update can make the
 * installed version differ from the executable of a running process.
 */
typedef struct OWH_META("model=OverwolfInstallation;output;size=size") owh_overwolf_installation {
    uint32_t size;
    char directory[OWH_OVERWOLF_PATH_BYTES];
    char launcher[OWH_OVERWOLF_PATH_BYTES];
    char version[OWH_OVERWOLF_VERSION_BYTES];
} owh_overwolf_installation;

/** A verified Overwolf main-process identity, not an owning OS process handle.
 * Revalidate PID and creation time before acting on this descriptor.
 */
typedef struct OWH_META("model=OverwolfProcess;output;size=size") owh_overwolf_process {
    uint32_t size, process_id;
    uint64_t process_started;
    char executable[OWH_OVERWOLF_PATH_BYTES];
    char version[OWH_OVERWOLF_VERSION_BYTES];
} owh_overwolf_process;

#ifdef __cplusplus
}
#endif
#endif
