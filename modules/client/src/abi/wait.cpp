#include "handles.hpp"
#include <overwolf_hook/abi/wait.h>
using namespace overwolf_hook;
using namespace overwolf_hook::client::abi;
extern "C" {
OWH_CLIENT void __cdecl owh_wait_options_init(owh_wait_options *out) {
    if (out)
        *out = {sizeof(*out), OWH_WAIT_INFINITE, nullptr};
}
OWH_CLIENT owh_result __cdecl owh_cancel_create(owh_cancel **out) {
    if (out)
        *out = nullptr;
    if (!out)
        return invalid();
    return boundary([&] {
        auto token = std::make_unique<owh_cancel>();
        token->event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!token->event)
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_ALLOCATION, OWH_ERROR_WIN32,
                               GetLastError());
        *out = token.release();
        return ok;
    });
}
OWH_CLIENT void __cdecl owh_cancel_signal(owh_cancel *token) {
    if (token)
        SetEvent(token->event.get());
}
OWH_CLIENT void __cdecl owh_cancel_close(owh_cancel *token) { delete token; }
}
