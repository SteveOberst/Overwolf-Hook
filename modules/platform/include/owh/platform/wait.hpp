#pragma once

#include <windows.h>

namespace overwolf_hook {
enum class WaitKind : unsigned char { signaled, timeout, abandoned, failed };
struct WaitResult final {
    WaitKind kind;
    DWORD index;
    DWORD error;
};

inline WaitResult wait_one(HANDLE handle, DWORD timeout) noexcept {
    const DWORD value = WaitForSingleObject(handle, timeout);
    if (value == WAIT_OBJECT_0)
        return {WaitKind::signaled, 0, 0};
    if (value == WAIT_ABANDONED)
        return {WaitKind::abandoned, 0, 0};
    if (value == WAIT_TIMEOUT)
        return {WaitKind::timeout, 0, 0};
    return {WaitKind::failed, 0, GetLastError()};
}

inline WaitResult wait_any(const HANDLE *handles, DWORD count, DWORD timeout) noexcept {
    const DWORD value = WaitForMultipleObjects(count, handles, FALSE, timeout);
    if (value >= WAIT_OBJECT_0 && value < WAIT_OBJECT_0 + count)
        return {WaitKind::signaled, value - WAIT_OBJECT_0, 0};
    if (value >= WAIT_ABANDONED_0 && value < WAIT_ABANDONED_0 + count)
        return {WaitKind::abandoned, value - WAIT_ABANDONED_0, 0};
    if (value == WAIT_TIMEOUT)
        return {WaitKind::timeout, 0, 0};
    return {WaitKind::failed, 0, GetLastError()};
}
} // namespace overwolf_hook
