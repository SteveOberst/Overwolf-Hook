#include "overwolf/process.hpp"
#include <overwolf_hook/abi/overwolf.h>
#include <owh/platform/result.hpp>

using namespace overwolf_hook;
extern "C" {
OWH_CLIENT owh_result __cdecl owh_overwolf_installation_query(owh_overwolf_installation *out) {
    if (!out || out->size != sizeof(*out))
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
    *out = {};
    out->size = sizeof(*out);
    return client::host::installation(*out);
}
OWH_CLIENT owh_result __cdecl owh_overwolf_process_query(uint32_t pid, uint64_t started,
                                                        owh_overwolf_process *out) {
    if (!out || out->size != sizeof(*out))
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
    *out = {};
    out->size = sizeof(*out);
    return client::host::process(pid, started, *out);
}
OWH_CLIENT owh_result __cdecl owh_overwolf_start(uint32_t timeout, owh_overwolf_process *out) {
    if (!out || out->size != sizeof(*out))
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
    *out = {};
    out->size = sizeof(*out);
    return client::host::start(timeout, *out);
}
OWH_CLIENT owh_result __cdecl owh_overwolf_stop(uint32_t pid, uint64_t started, uint32_t timeout) {
    return client::host::stop(pid, started, timeout);
}
}
