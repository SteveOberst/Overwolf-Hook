#ifndef OVERWOLF_HOOK_TYPES_WAIT_H
#define OVERWOLF_HOOK_TYPES_WAIT_H
#include <overwolf_hook/types/common.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct owh_cancel owh_cancel;
typedef struct OWH_META("model=WaitOptions;input;init=owh_wait_options_init;size=size")
    owh_wait_options {
    uint32_t size;
    uint32_t timeout_ms;
    owh_cancel *cancellation OWH_META("pointer=borrowed");
} owh_wait_options;
#ifdef __cplusplus
}
#endif
#endif
