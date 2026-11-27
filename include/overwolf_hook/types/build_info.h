#ifndef OVERWOLF_HOOK_TYPES_BUILD_INFO_H
#define OVERWOLF_HOOK_TYPES_BUILD_INFO_H
#include <overwolf_hook/types/common.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { OWH_SDK_VERSION_CAPACITY = 128 };
/** SDK release version and the build number assigned by its producer. */
typedef struct OWH_META("model=BuildInfo;output") owh_build_info {
    char sdk_version[OWH_SDK_VERSION_CAPACITY];
    uint64_t build_number;
} owh_build_info;
#ifdef __cplusplus
}
static_assert(sizeof(owh_build_info) == 136);
#endif
#endif
