#include "process.hpp"
#include <owh/platform/deadline.hpp>
#include <owh/platform/result.hpp>
#include <shellapi.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace overwolf_hook::client::host {
void describe(const overwolf::ProcessIdentity &identity, owh_overwolf_process &output) {
    owh_overwolf_process info{};
    info.size = sizeof(info);
    info.process_id = identity.pid;
    info.process_started = identity.started;
    copy_utf8(info.executable, sizeof(info.executable), identity.image);
    copy_utf8(info.version, sizeof(info.version), file_version(identity.image));
    output = info;
}
owh_result process(uint32_t pid, uint64_t started, owh_overwolf_process &output) noexcept {
    return boundary([&] {
        auto [status, identity] = overwolf::select_process(pid, started);
        if (status.status == OWH_OK)
            describe(identity, output);
        return status;
    });
}
owh_result start(uint32_t timeout, owh_overwolf_process &output) noexcept {
    return boundary([&] {
        if (!timeout || timeout == INFINITE)
            return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
        const Deadline deadline(timeout);
        const auto running = process(0, 0, output);
        if (running.status != OWH_NOT_FOUND)
            return running;
        owh_overwolf_installation installed{};
        const auto found = installation(installed);
        if (found.status != OWH_OK)
            return found;
        const auto path = std::filesystem::path(std::u8string(
            installed.launcher, installed.launcher + std::strlen(installed.launcher)));
        const auto file = path.wstring(), directory = path.parent_path().wstring();
        SHELLEXECUTEINFOW launch{sizeof(launch)};
        launch.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        launch.lpVerb = L"open";
        launch.lpFile = file.c_str();
        launch.lpDirectory = directory.c_str();
        launch.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&launch))
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_OVERWOLF_START, OWH_ERROR_WIN32,
                               GetLastError());
        Handle launched(launch.hProcess);
        // The launcher may exit or delegate to another process. Neither its PID
        // nor its parent relationship is used as proof of main-process identity.
        for (;;) {
            const auto result = process(0, 0, output);
            // The launcher can briefly overlap another Overwolf.exe during
            // handoff. After our launch, wait for one unambiguous main process
            // instead of treating that transitional overlap as a selection.
            if (result.status != OWH_NOT_FOUND && result.status != OWH_INVALID_ARGUMENT)
                return result;
            if (deadline.expired())
                return make_result(OWH_TIMEOUT, OWH_STAGE_OVERWOLF_START);
            Sleep(std::min<DWORD>(50, deadline.remaining_ms()));
        }
    });
}

namespace {
std::wstring shutdown_event_name() {
#if defined(OWH_CLIENT_OVERLAY_TEST_FIXTURE)
    return L"Local\\owh.shutdown-test." + std::to_wstring(GetCurrentProcessId());
#else
    return L"Global\\OverwolfClientKill";
#endif
}

#if !defined(OWH_CLIENT_OVERLAY_TEST_FIXTURE)
bool installed_image(const std::wstring &image, const char *installed_directory) {
    auto directory = std::filesystem::path(std::u8string(
        installed_directory, installed_directory + std::strlen(installed_directory))).lexically_normal();
    auto executable = std::filesystem::path(image).lexically_normal();
    directory.make_preferred();
    executable.make_preferred();
    if (!directory.is_absolute())
        return false;
    auto component = executable.begin();
    for (const auto &part : directory) {
        if (part.empty())
            continue;
        if (component == executable.end() || _wcsicmp(part.c_str(), component->c_str()) != 0)
            return false;
        ++component;
    }
    return component != executable.end();
}
#endif
} // namespace

owh_result stop(uint32_t pid, uint64_t started, uint32_t timeout) noexcept {
    return boundary([&] {
        if (!pid || !started || !timeout || timeout == INFINITE)
            return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
        const Deadline deadline(timeout);
        auto [status, identity] = overwolf::select_process(pid, started);
        if (status.status != OWH_OK)
            return status;
#if !defined(OWH_CLIENT_OVERLAY_TEST_FIXTURE)
        // The application event addresses its global singleton, not a PID. Only
        // the sole installed main process may receive this cooperative request.
        owh_overwolf_installation installed{};
        const auto found = installation(installed);
        if (found.status != OWH_OK)
            return found.status == OWH_NOT_FOUND ? make_result(OWH_UNSUPPORTED, OWH_STAGE_OVERWOLF_STOP)
                                                 : found;
        if (!installed_image(identity.image, installed.directory))
            return make_result(OWH_INCOMPATIBLE, OWH_STAGE_OVERWOLF_STOP);
        auto [unique_status, unique] = overwolf::select_process(0, started);
        if (unique_status.status != OWH_OK)
            return unique_status;
        if (unique.pid != identity.pid)
            return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_OVERWOLF_STOP);
#endif
        const auto event_name = shutdown_event_name();
        Handle request(OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name.c_str()));
        if (!request) {
            const auto error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND ? make_result(OWH_UNSUPPORTED, OWH_STAGE_OVERWOLF_STOP)
                                                : make_result(OWH_SYSTEM_ERROR, OWH_STAGE_OVERWOLF_STOP,
                                                              OWH_ERROR_WIN32, error);
        }
        if (deadline.expired())
            return make_result(OWH_TIMEOUT, OWH_STAGE_OVERWOLF_STOP);
        if (!SetEvent(request.get()))
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_OVERWOLF_STOP, OWH_ERROR_WIN32, GetLastError());
        request.reset();
        // Acceptance of the request is not acknowledgement of completed teardown.
        const auto waited = WaitForSingleObject(identity.process.get(), deadline.remaining_ms());
        return waited == WAIT_OBJECT_0 ? make_result(OWH_OK)
             : waited == WAIT_TIMEOUT ? make_result(OWH_TIMEOUT, OWH_STAGE_OVERWOLF_STOP)
             : make_result(OWH_SYSTEM_ERROR, OWH_STAGE_OVERWOLF_STOP, OWH_ERROR_WIN32,
                           GetLastError());
    });
}
} // namespace overwolf_hook::client::host
