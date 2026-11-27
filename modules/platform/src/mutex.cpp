#include <owh/platform/mutex.hpp>
#include <owh/platform/wait.hpp>

namespace overwolf_hook {
Lock::Lock(HANDLE mutex, DWORD timeout) : mutex_(mutex) {
    const auto result = wait_one(mutex, timeout);
    owned_ = result.kind == WaitKind::signaled || result.kind == WaitKind::abandoned;
    abandoned_ = result.kind == WaitKind::abandoned;
    if (result.kind == WaitKind::timeout)
        fail(OWH_BUSY);
    if (!owned_)
        fail_win32(OWH_SYSTEM_ERROR, result.error);
}
Lock::Lock(HANDLE mutex, DWORD timeout, HANDLE cancel) : mutex_(mutex) {
    HANDLE handles[]{cancel, mutex};
    const auto result = wait_any(handles, 2, timeout);
    owned_ = result.index == 1 &&
             (result.kind == WaitKind::signaled || result.kind == WaitKind::abandoned);
    abandoned_ = result.index == 1 && result.kind == WaitKind::abandoned;
    if (result.kind == WaitKind::signaled && result.index == 0)
        fail(OWH_CANCELLED);
    if (result.kind == WaitKind::timeout)
        fail(OWH_TIMEOUT);
    if (!owned_)
        fail_win32(OWH_SYSTEM_ERROR, result.error);
}

} // namespace overwolf_hook
