#include <owh/overwolf/host_process.hpp>
#include <owh/platform/identity.hpp>
#include <owh/platform/result.hpp>
#include <tlhelp32.h>
#include <filesystem>
#include <vector>

namespace overwolf_hook::overwolf {

namespace {
bool x64_process(HANDLE process) noexcept {
    using IsWow64Process2Fn = BOOL(WINAPI *)(HANDLE, USHORT *, USHORT *);
    auto fn = reinterpret_cast<IsWow64Process2Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    if (!fn)
        return false;
    USHORT process_machine = 0, native_machine = 0;
    return fn(process, &process_machine, &native_machine) &&
           process_machine == IMAGE_FILE_MACHINE_UNKNOWN &&
           native_machine == IMAGE_FILE_MACHINE_AMD64;
}

bool read_image(HANDLE process, std::wstring &path) {
    std::vector<wchar_t> buffer(32768);
    DWORD length = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &length))
        return false;
    path.assign(buffer.data(), length);
    const auto base = path.find_last_of(L"\\/");
    return _wcsicmp(path.c_str() + (base == std::wstring::npos ? 0 : base + 1), L"Overwolf.exe") ==
           0;
}

bool main_package(const std::wstring &image) {
    // The installation-root Overwolf.exe is a bootstrapper. The main executable
    // lives with the application core and native server. Inspect package files,
    // not loaded modules, so this also works before early startup hooks are armed.
    const auto directory = std::filesystem::path(image).parent_path();
    std::error_code error;
    return std::filesystem::is_regular_file(directory / L"OverWolf.Client.Core.dll", error) &&
           std::filesystem::is_regular_file(directory / L"OWServer.dll", error);
}

} // namespace

std::pair<owh_result, ProcessIdentity> select_process(uint32_t requested_pid,
    uint64_t expected_started, ProcessAccess access) {
    DWORD current_session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &current_session))
        return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_TARGET_DISCOVERY, OWH_ERROR_WIN32,
                            GetLastError()),
                ProcessIdentity{}};
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_TARGET_DISCOVERY, OWH_ERROR_WIN32,
                            GetLastError()),
                ProcessIdentity{}};
    std::vector<DWORD> candidates;
    PROCESSENTRY32W row{};
    row.dwSize = sizeof(row);
    if (Process32FirstW(snapshot, &row))
        do {
            if ((!requested_pid || requested_pid == row.th32ProcessID) &&
                _wcsicmp(row.szExeFile, L"Overwolf.exe") == 0)
                candidates.push_back(row.th32ProcessID);
        } while (Process32NextW(snapshot, &row));
    CloseHandle(snapshot);
    if (requested_pid && candidates.empty())
        return {make_result(OWH_NOT_FOUND, OWH_STAGE_TARGET_DISCOVERY), ProcessIdentity{}};
    std::vector<ProcessIdentity> matches;
    for (const auto pid : candidates) {
        HANDLE inspection =
            OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                        FALSE, pid);
        if (!inspection) {
            const auto error = GetLastError();
            if (!requested_pid && error == ERROR_INVALID_PARAMETER)
                continue; // A candidate may exit while its launcher hands off.
            return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_TARGET_DISCOVERY, OWH_ERROR_WIN32,
                                error), ProcessIdentity{}};
        }
        ProcessIdentity observed;
        observed.pid = pid;
        DWORD error = 0;
        observed.started = platform::process_start(inspection);
        if (!observed.started) {
            error = GetLastError();
            CloseHandle(inspection);
            return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_TARGET_DISCOVERY, OWH_ERROR_WIN32, error),
                    ProcessIdentity{}};
        }
        if (!ProcessIdToSessionId(pid, &observed.session) ||
            observed.session != current_session || !x64_process(inspection) ||
            !read_image(inspection, observed.image) ||
            !platform::same_user(GetCurrentProcess(), inspection)) {
            CloseHandle(inspection);
            if (requested_pid)
                return {make_result(OWH_INCOMPATIBLE, OWH_STAGE_TARGET_DISCOVERY), ProcessIdentity{}};
            continue;
        }
        if (!main_package(observed.image)) {
            CloseHandle(inspection);
            continue;
        }
        observed.process.reset(inspection);
        matches.push_back(std::move(observed));
    }
    if (matches.size() != 1)
        return {make_result(matches.empty() ? OWH_NOT_FOUND : OWH_INVALID_ARGUMENT,
                            OWH_STAGE_TARGET_DISCOVERY), ProcessIdentity{}};
    auto observed = std::move(matches.front());
    if (expected_started && observed.started != expected_started)
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_AGENT_LIFETIME), ProcessIdentity{}};
    if (access == ProcessAccess::inspect) {
        return {make_result(OWH_OK), std::move(observed)};
    }

    ProcessIdentity identity;
    identity.pid = observed.pid;
    DWORD rights = PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION |
                   PROCESS_VM_READ | SYNCHRONIZE;
    if (access == ProcessAccess::inject)
        rights |= PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE;
    identity.process.reset(OpenProcess(rights, FALSE, identity.pid));
    if (!identity.process.get())
        return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32, GetLastError()),
                ProcessIdentity{}};
    identity.started = platform::process_start(identity.process.get());
    identity.session = observed.session;
    if (!identity.started || identity.started != observed.started ||
        !platform::same_user(GetCurrentProcess(), identity.process.get()) ||
        !x64_process(identity.process.get()) || !read_image(identity.process.get(), identity.image))
        return {make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME), ProcessIdentity{}};
    return {make_result(OWH_OK), std::move(identity)};
}
} // namespace overwolf_hook::overwolf
