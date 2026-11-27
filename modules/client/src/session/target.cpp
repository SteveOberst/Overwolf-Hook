#include <windows.h>
#include "session/target.hpp"
#include <owh/platform/identity.hpp>
#include <owh/platform/result.hpp>
#include <tlhelp32.h>
#include <vector>
using namespace overwolf_hook::client::detail;

namespace overwolf_hook::client::detail {
Module module_in_process(uint32_t pid, std::wstring_view wanted) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};
    MODULEENTRY32W row{};
    row.dwSize = sizeof(row);
    Module found;
    if (Module32FirstW(snapshot, &row))
        do {
            if (_wcsicmp(row.szModule, std::wstring(wanted).c_str()) == 0) {
                found.base = reinterpret_cast<uintptr_t>(row.modBaseAddr);
                found.path = row.szExePath;
                break;
            }
        } while (Module32NextW(snapshot, &row));
    CloseHandle(snapshot);
    return found;
}

} // namespace overwolf_hook::client::detail
