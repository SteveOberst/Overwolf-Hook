#include "BuildInfo.hpp"
#include "agent_root.hpp"
#include <cstring>
#include <cwchar>
#include <mutex>
#include <owh/core/types.hpp>
#include <owh/ipc/session_protocol.hpp>
#include <owh/ipc/shared_region.hpp>
#include <owh/overwolf/overlays/lifetime_admission.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/identity.hpp>

namespace {
using namespace overwolf_hook;
using overwolf_hook::ipc::BootstrapArgs;
bool copy_bootstrap(const void *source, BootstrapArgs &destination) noexcept {
    __try {
        std::memcpy(&destination, source, sizeof(destination));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool main_process() noexcept {
    wchar_t path[32768]{};
    const auto count = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!count || count == std::size(path))
        return false;
    const auto base = std::wcsrchr(path, L'\\');
    return _wcsicmp(base ? base + 1 : path, L"Overwolf.exe") == 0;
}
} // namespace
extern "C" __declspec(dllexport) const owh_build_info owh_agent_build_info =
    overwolf_hook::build_info::value;
/** One-shot, idempotent bootstrap. This thread starts the resident server and
 * returns; it does not own a client session or run a client's command loop. */
extern "C" __declspec(dllexport) DWORD WINAPI owh_agent_bootstrap(void *remote_args) noexcept {
    BootstrapArgs args{};
    if (!remote_args || !copy_bootstrap(remote_args, args))
        return ERROR_INVALID_ADDRESS;
    if (args.magic != ipc::magic || args.size != sizeof(args) ||
        args.abi_version != OWH_ABI_VERSION || args.reserved ||
        (args.attach_mode != OWH_ATTACH_RESTART && args.attach_mode != OWH_ATTACH_HOT) ||
        args.host_process_id != GetCurrentProcessId() || !args.client_process_id ||
        !args.client_process_started ||
        args.host_process_started != platform::process_start(GetCurrentProcess()) ||
        !main_process() ||
        std::memcmp(&args.build_info, &build_info::value, sizeof(owh_build_info)))
        return ERROR_INVALID_DATA;
    Handle client(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                              args.client_process_id));
    DWORD own_session = 0, client_session = 0;
    if (!client || platform::process_start(client.get()) != args.client_process_started ||
        !platform::same_user(GetCurrentProcess(), client.get()) ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &own_session) ||
        !ProcessIdToSessionId(args.client_process_id, &client_session) ||
        own_session != client_session || own_session != args.windows_session_id)
        return ERROR_ACCESS_DENIED;
    auto &admission = overwolf_hook::overwolf::process_lifetime_admission();
    // One resident initialization decision. A later HOT client must not reopen
    // admission after a failed observed Init, or weaken a controlled startup.
    static std::mutex startup_mutex;
    static uint32_t startup_mode = UINT32_MAX;
    static DWORD startup_status = ERROR_NOT_READY;
    std::lock_guard startup_lock(startup_mutex);
    if (startup_mode == UINT32_MAX) {
        startup_mode = args.attach_mode;
        if (startup_mode == OWH_ATTACH_HOT) {
            admission.allow_hot_attach();
            startup_status = ERROR_SUCCESS;
        } else if (admission.prepare_controlled_start()) {
            startup_status = ERROR_SUCCESS;
        }
    }
    if (startup_status != ERROR_SUCCESS ||
        (args.attach_mode == OWH_ATTACH_RESTART && startup_mode != OWH_ATTACH_RESTART))
        return ERROR_NOT_READY;
    overwolf_hook::overwolf::ResolvedProfile profile{};
    // Feature readiness must not prevent resident identity/version discovery.
    (void)admission.resolve_profile(GetModuleHandleW(L"OWAgent.dll"), profile);
    auto *root = overwolf_hook::agent::process_agent_root(admission);
    if (!root)
        return ERROR_NOT_ENOUGH_MEMORY;
    const auto status = root->start_server();
    return status.status == OWH_OK ? ERROR_SUCCESS : ERROR_GEN_FAILURE;
}
