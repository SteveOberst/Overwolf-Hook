#pragma once
#include <windows.h>
#include <new>
#include <owh/core/result.hpp>

namespace overwolf_hook {
struct Failure {
    owh_result result;
};
[[noreturn]] inline void fail(owh_status status, uint32_t stage = OWH_STAGE_NONE) {
    throw Failure{make_result(status, stage)};
}
[[noreturn]] inline void fail_win32(owh_status status, DWORD error,
                                    uint32_t stage = OWH_STAGE_NONE) {
    throw Failure{make_result(status, stage, OWH_ERROR_WIN32, error)};
}

template <typename F> owh_result boundary(F &&action) noexcept {
    try {
        return action();
    } catch (const Failure &error) {
        return error.result;
    } catch (const std::bad_alloc &) {
        return make_result(OWH_RESOURCE_EXHAUSTED);
    } catch (...) {
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_NONE, OWH_ERROR_WIN32,
                           ERROR_UNHANDLED_EXCEPTION);
    }
}
} // namespace overwolf_hook
