#ifndef OVERWOLF_HOOK_ABI_WAIT_H
#define OVERWOLF_HOOK_ABI_WAIT_H
#include <overwolf_hook/abi/export.h>
#include <overwolf_hook/types/wait.h>
#ifdef __cplusplus
extern "C" {
#endif
OWH_CLIENT void __cdecl owh_wait_options_init(owh_wait_options *);
OWH_CLIENT owh_result __cdecl owh_cancel_create(owh_cancel **output);
OWH_CLIENT void __cdecl owh_cancel_signal(owh_cancel *);
/** Signal and join all waits borrowing the token before closing it. */
OWH_CLIENT void __cdecl owh_cancel_close(owh_cancel *);
#ifdef __cplusplus
}
#endif
#endif
