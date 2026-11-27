#include "BuildInfo.hpp"
#include "session/bootstrap_payload.hpp"
#include "session/target.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <owh/core/types.hpp>
#include <owh/ipc/session_protocol.hpp>
#include <owh/ipc/session_transport.hpp>
#include <owh/platform/identity.hpp>
#include <owh/platform/mutex.hpp>
#include <owh/platform/pe_image.hpp>
using namespace overwolf_hook::client::detail;

namespace overwolf_hook::client::detail {
bool compatible_build_info(const owh_build_info &value) noexcept {
    return std::memcmp(&value, &overwolf_hook::build_info::value, sizeof(value)) == 0;
}

static uintptr_t remote_function(uint32_t pid, FARPROC local_function) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!local_function ||
        !VirtualQuery(reinterpret_cast<void *>(local_function), &memory, sizeof(memory)))
        return 0;
    wchar_t path[32768]{};
    const DWORD chars = GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase), path,
                                           static_cast<DWORD>(std::size(path)));
    if (!chars || chars >= std::size(path))
        return 0;
    const auto module = module_in_process(
        pid, std::filesystem::path(std::wstring(path, chars)).filename().wstring());
    return module.base ? module.base + reinterpret_cast<uintptr_t>(local_function) -
                             reinterpret_cast<uintptr_t>(memory.AllocationBase)
                       : 0;
}

static bool rva_offset(const AgentPayload &image, const IMAGE_NT_HEADERS64 &nt, DWORD rva,
                       size_t length, size_t &offset) {
    return platform::pe::rva_offset(image.bytes, nt, image.sections_offset, rva, length, offset);
}

static bool payload_export(const AgentPayload &image, const IMAGE_NT_HEADERS64 &nt,
                           const char *wanted, DWORD &function_rva) {
    return platform::pe::export_rva(image.bytes, platform::pe::Layout::raw_file, nt,
                                    image.sections_offset, wanted, function_rva);
}

bool parse_agent_payload(const std::wstring &path, AgentPayload &payload) {
    std::ifstream input(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!input)
        return false;
    const auto length = input.tellg();
    if (length < static_cast<std::streamoff>(sizeof(IMAGE_DOS_HEADER) + sizeof(DWORD) +
                                             sizeof(IMAGE_FILE_HEADER) +
                                             sizeof(IMAGE_OPTIONAL_HEADER64)) ||
        length > 256 * 1024 * 1024)
        return false;
    payload.bytes.resize(static_cast<size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(payload.bytes.data()), length))
        return false;
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, payload.bytes.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        static_cast<size_t>(dos.e_lfanew) > payload.bytes.size() - sizeof(DWORD) -
                                                sizeof(IMAGE_FILE_HEADER) -
                                                sizeof(IMAGE_OPTIONAL_HEADER64))
        return false;
    DWORD signature = 0;
    std::memcpy(&signature, payload.bytes.data() + dos.e_lfanew, sizeof(signature));
    if (signature != IMAGE_NT_SIGNATURE)
        return false;
    IMAGE_NT_HEADERS64 nt{};
    std::memcpy(&nt, payload.bytes.data() + dos.e_lfanew, sizeof(nt));
    if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || !nt.FileHeader.NumberOfSections ||
        nt.FileHeader.NumberOfSections > 96 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT ||
        !nt.OptionalHeader.SizeOfImage)
        return false;
    if (!nt.FileHeader.NumberOfSections ||
        !platform::pe::section_table(payload.bytes, static_cast<size_t>(dos.e_lfanew), nt,
                                     payload.sections_offset))
        return false;
    payload.image_size = nt.OptionalHeader.SizeOfImage;
    if (!payload_export(payload, nt, "owh_agent_bootstrap", payload.bootstrap_rva) ||
        !payload_export(payload, nt, "owh_agent_build_info", payload.build_info_rva))
        return false;
    size_t build_info_offset = 0;
    if (!rva_offset(payload, nt, payload.build_info_rva, sizeof(payload.build_info),
                    build_info_offset))
        return false;
    std::memcpy(&payload.build_info, payload.bytes.data() + build_info_offset,
                sizeof(payload.build_info));
    if (!compatible_build_info(payload.build_info))
        return false;
    return true;
}

static bool remote_agent_matches(HANDLE process, const Module &module,
                                 const AgentPayload &payload) {
    if (!module.base || module.path.empty() || !payload.bootstrap_rva || !payload.image_size ||
        payload.bootstrap_rva >= payload.image_size ||
        payload.build_info_rva > payload.image_size ||
        sizeof(payload.build_info) > payload.image_size - payload.build_info_rva)
        return false;
    const auto bootstrap = module.base + payload.bootstrap_rva;
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQueryEx(process, reinterpret_cast<const void *>(bootstrap), &memory,
                        sizeof(memory)) ||
        memory.AllocationBase != reinterpret_cast<void *>(module.base) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(memory.Protect &
          (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
        !payload.bootstrap_rva)
        return false;
    const auto build_info_address = module.base + payload.build_info_rva;
    MEMORY_BASIC_INFORMATION info_memory{};
    if (!VirtualQueryEx(process, reinterpret_cast<const void *>(build_info_address), &info_memory,
                        sizeof(info_memory)) ||
        info_memory.AllocationBase != reinterpret_cast<void *>(module.base) ||
        info_memory.State != MEM_COMMIT || (info_memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    const auto region_base = reinterpret_cast<uintptr_t>(info_memory.BaseAddress);
    const auto region_end = region_base + info_memory.RegionSize;
    if (region_end < region_base || build_info_address < region_base ||
        build_info_address > region_end || sizeof(owh_build_info) > region_end - build_info_address)
        return false;
    owh_build_info resident{};
    SIZE_T bytes_read = 0;
    return ReadProcessMemory(process, reinterpret_cast<const void *>(build_info_address), &resident,
                             sizeof(resident), &bytes_read) &&
           bytes_read == sizeof(resident) && compatible_build_info(resident);
}

std::wstring client_module_directory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&client_module_directory), &module))
        return {};
    std::vector<wchar_t> path(32768);
    const DWORD chars = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!chars || chars >= path.size())
        return {};
    return std::filesystem::path(std::wstring(path.data(), chars)).parent_path().wstring();
}

namespace {
bool wait_remote(HANDLE thread, HANDLE process, DWORD &exit_code) noexcept {
    HANDLE waits[]{thread, process};
    const auto wait = WaitForMultipleObjects(2, waits, FALSE, 5000);
    if (wait != WAIT_OBJECT_0) {
        SetLastError(wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_PROCESS_ABORTED);
        return false;
    }
    return GetExitCodeThread(thread, &exit_code) != FALSE;
}
} // namespace
owh_result bootstrap_agent(overwolf::ProcessIdentity &target, uint64_t client_started,
                           uint32_t attach_mode) {
    const auto mutex_name =
        overwolf_hook::ipc::agent_endpoint_name(target.pid, target.started) + ".bootstrap";
    overwolf_hook::Handle startup(CreateMutexA(nullptr, FALSE, mutex_name.c_str()));
    if (!startup)
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32, GetLastError());
    overwolf_hook::Lock lock(startup.get(), 5000);
    // HOT may reuse a compatible resident server. RESTART must establish its
    // own early-start boundary rather than accepting an earlier HOT bootstrap.
    if (attach_mode == OWH_ATTACH_HOT)
        try {
            auto endpoint = overwolf_hook::ipc::AgentEndpoint::open(target.pid, target.started);
            return make_result(OWH_OK);
        } catch (const overwolf_hook::Failure &error) {
            if (error.result.status != OWH_NOT_FOUND)
                return error.result;
        }
    auto [selection, active] = overwolf::select_process(target.pid, target.started, overwolf::ProcessAccess::inject);
    if (selection.status != OWH_OK)
        return selection;
    const auto path = client_module_directory() + L"\\owh_agent.dll";
    AgentPayload payload;
    if (!parse_agent_payload(path, payload))
        return make_result(OWH_INCOMPATIBLE, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32,
                           ERROR_INVALID_IMAGE_HASH);
    auto module = module_in_process(active.pid, L"owh_agent.dll");
    if (!module.base) {
        const auto load = remote_function(
            active.pid, GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        if (!load)
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_BOOTSTRAP);
        const size_t bytes = (path.size() + 1) * sizeof(wchar_t);
        void *remote_path = VirtualAllocEx(active.process.get(), nullptr, bytes,
                                           MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote_path)
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32,
                               GetLastError());
        if (!WriteProcessMemory(active.process.get(), remote_path, path.c_str(), bytes, nullptr)) {
            const auto error = GetLastError();
            VirtualFreeEx(active.process.get(), remote_path, 0, MEM_RELEASE);
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32, error);
        }
        overwolf_hook::Handle thread(CreateRemoteThread(
            active.process.get(), nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(load),
            remote_path, 0, nullptr));
        if (!thread) {
            const auto error = GetLastError();
            VirtualFreeEx(active.process.get(), remote_path, 0, MEM_RELEASE);
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32, error);
        }
        DWORD outcome = 0;
        if (!wait_remote(thread.get(), active.process.get(), outcome)) {
            // Still-running remote code may use its argument. The small allocation
            // belongs to the target until process exit; never free it under it.
            return make_result(OWH_TIMEOUT, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32, GetLastError());
        }
        VirtualFreeEx(active.process.get(), remote_path, 0, MEM_RELEASE);
        module = module_in_process(active.pid, L"owh_agent.dll");
    }
    AgentPayload resident_payload;
    if (module.path.empty() || !parse_agent_payload(module.path, resident_payload) ||
        !remote_agent_matches(active.process.get(), module, resident_payload))
        return make_result(OWH_INCOMPATIBLE, OWH_STAGE_HANDSHAKE, OWH_ERROR_WIN32,
                           ERROR_INVALID_IMAGE_HASH);
    ipc::BootstrapArgs args{};
    args.magic = ipc::magic;
    args.size = sizeof(args);
    args.abi_version = OWH_ABI_VERSION;
    args.attach_mode = attach_mode;
    args.host_process_id = target.pid;
    args.host_process_started = target.started;
    args.client_process_id = GetCurrentProcessId();
    args.client_process_started = client_started;
    args.windows_session_id = target.session;
    args.build_info = overwolf_hook::build_info::value;
    void *remote_args = VirtualAllocEx(active.process.get(), nullptr, sizeof(args),
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_args)
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32, GetLastError());
    if (!WriteProcessMemory(active.process.get(), remote_args, &args, sizeof(args), nullptr)) {
        const auto error = GetLastError();
        VirtualFreeEx(active.process.get(), remote_args, 0, MEM_RELEASE);
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32, error);
    }
    overwolf_hook::Handle thread(CreateRemoteThread(
        active.process.get(), nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(module.base + resident_payload.bootstrap_rva),
        remote_args, 0, nullptr));
    if (!thread) {
        const auto error = GetLastError();
        VirtualFreeEx(active.process.get(), remote_args, 0, MEM_RELEASE);
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32, error);
    }
    DWORD outcome = 0;
    if (!wait_remote(thread.get(), active.process.get(), outcome))
        return make_result(OWH_TIMEOUT, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32, GetLastError());
    VirtualFreeEx(active.process.get(), remote_args, 0, MEM_RELEASE);
    return outcome == ERROR_SUCCESS
               ? make_result(OWH_OK)
               : make_result(OWH_SYSTEM_ERROR, OWH_STAGE_BOOTSTRAP, OWH_ERROR_WIN32, outcome);
}
} // namespace overwolf_hook::client::detail
