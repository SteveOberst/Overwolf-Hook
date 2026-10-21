#include <overwolf_hook/probe.h>
#include "capture_control.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
HANDLE console_stop = nullptr;
HANDLE console_done = nullptr;
BOOL WINAPI console_handler(DWORD signal) {
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT && signal != CTRL_CLOSE_EVENT &&
        signal != CTRL_LOGOFF_EVENT && signal != CTRL_SHUTDOWN_EVENT) return FALSE;
    SetEvent(console_stop);
    // Console-close otherwise terminates us as soon as this handler returns.
    // The owner-process watcher remains the fallback if Windows' deadline wins.
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT) WaitForSingleObject(console_done, 4000);
    return TRUE;
}
[[noreturn]] void error(const char* what) {
    throw std::runtime_error(std::string(what) + " (Win32 " + std::to_string(GetLastError()) + ")");
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Module { uintptr_t base; std::wstring path; };
Module module(DWORD pid, const std::wstring& name, bool exact_path = false) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)};
    if (snapshot.value == INVALID_HANDLE_VALUE) error("Module snapshot failed");
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.value, &entry)) error("Module enumeration failed");
    do {
        if (_wcsicmp(exact_path ? entry.szExePath : entry.szModule, name.c_str()) == 0)
            return {reinterpret_cast<uintptr_t>(entry.modBaseAddr), entry.szExePath};
    } while (Module32NextW(snapshot.value, &entry));
    return {};
}
uint64_t created(HANDLE process) {
    FILETIME a{}, b{}, c{}, d{};
    if (!GetProcessTimes(process, &a, &b, &c, &d)) error("Process identity unavailable");
    return (uint64_t{a.dwHighDateTime} << 32) | a.dwLowDateTime;
}
struct RemoteBuffer {
    HANDLE process;
    void* address;
    RemoteBuffer(HANDLE process, const void* data, size_t size) : process(process) {
        address = VirtualAllocEx(process, nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!address) error("Remote argument allocation failed");
        SIZE_T written;
        if (data && (!WriteProcessMemory(process, address, data, size, &written) || written != size)) {
            VirtualFreeEx(process, address, 0, MEM_RELEASE); error("Remote argument write failed");
        }
    }
    ~RemoteBuffer() { if (address) VirtualFreeEx(process, address, 0, MEM_RELEASE); }
};
DWORD call(HANDLE process, uintptr_t entry, RemoteBuffer* argument = nullptr) {
    Handle thread{CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(entry),
        argument ? argument->address : nullptr, 0, nullptr)};
    if (!thread.value) error("Explicit remote call failed");
    if (WaitForSingleObject(thread.value, 15000) != WAIT_OBJECT_0) {
        // Never terminate a host thread or free an argument it may still use.
        if (argument) argument->address = nullptr;
        throw std::runtime_error("Remote call did not finish; argument retained, owner-exit shutdown applies");
    }
    DWORD result;
    if (!GetExitCodeThread(thread.value, &result)) error("Remote result unavailable");
    return result;
}
uintptr_t remote_system_function(DWORD pid, const char* name) {
    const auto local = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
    if (!local) error("System function unavailable");
    HMODULE containing = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(local), &containing)) error("Forwarded system function module unavailable");
    wchar_t path[32768];
    if (!GetModuleFileNameW(containing, path, 32768)) error("System module path unavailable");
    const auto remote = module(pid, std::filesystem::path(path).filename().wstring());
    if (!remote.base) error("Target system module unavailable");
    return remote.base + reinterpret_cast<uintptr_t>(local) - reinterpret_cast<uintptr_t>(containing);
}
struct Session {
    HANDLE process;
    uintptr_t stop = 0;
    bool started = false;
    ~Session() {
        if (started && stop && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
            try { call(process, stop); }
            catch (const std::exception& problem) { std::cerr << "Stop: " << problem.what() << '\n'; }
        }
    }
};
struct ConsoleControl {
    Handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ConsoleControl() {
        if (!stop.value || !done.value) error("Cannot create controller stop signal");
        console_stop = stop.value; console_done = done.value;
        if (!SetConsoleCtrlHandler(console_handler, TRUE)) error("Cannot register console stop handler");
    }
    ~ConsoleControl() {
        SetEvent(done.value);
        SetConsoleCtrlHandler(console_handler, FALSE);
    }
};
void write(HANDLE file, const void* bytes, DWORD count) {
    DWORD written = 0;
    if (!WriteFile(file, bytes, count, &written, nullptr) || written != count) error("Raw capture write failed");
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        // No generic injection interface: explicitly selected Overwolf host only.
        if (argc != 5 && argc != 6 && argc != 8) {
            std::cerr << "Usage: owh_probe <Overwolf PID> <capture DLL absolute path> <seconds|0=until-stop> <new capture file> [all|source-channel] [--stop-file <path>]\n";
            return 2;
        }
        using namespace overwolf_hook::controller;
        uint32_t source_channel = OWH_ALL_CHANNELS;
        if (argc >= 6 && std::wstring_view(argv[5]) != L"all") {
            const auto value = number(argv[5], "Source must be 'all' or a nonzero uint32 channel ID (not a game ID)");
            if (!value || value > UINT32_MAX)
                throw std::runtime_error("Source channel outside 1..4294967295; use 'all' for every event channel");
            source_channel = static_cast<uint32_t>(value);
        }
        std::filesystem::path stop_file;
        if (argc == 8) {
            if (std::wstring_view(argv[6]) != L"--stop-file") throw std::runtime_error("Expected --stop-file <path>");
            stop_file = std::filesystem::absolute(argv[7]);
            if (std::filesystem::exists(stop_file)) throw std::runtime_error("Stop file must not already exist");
        }
        const auto selected_pid = number(argv[1], "PID must be a nonzero uint32");
        if (!selected_pid || selected_pid > UINT32_MAX) throw std::runtime_error("PID must be a nonzero uint32");
        const DWORD pid = static_cast<DWORD>(selected_pid);
        const auto dll = std::filesystem::canonical(argv[2]).wstring();
        const auto seconds = number(argv[3], "Duration must be whole seconds, or 0 until stop");
        ConsoleControl console;
        // Validate overflow before any remote operation; no arbitrary duration cap.
        (void)CaptureControl(seconds, console.stop.value, nullptr, stop_file);
        Handle process{OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid)};
        if (!process.value) error("Cannot open selected Overwolf process");
        wchar_t image[32768]; DWORD length = 32768;
        if (!QueryFullProcessImageNameW(process.value, 0, image, &length)) error("Cannot identify target");
        if (_wcsicmp(std::filesystem::path(image).filename().c_str(), L"Overwolf.exe") != 0)
            throw std::runtime_error("Target must be Overwolf.exe, never a game process");
        USHORT machine, native;
        if (!IsWow64Process2(process.value, &machine, &native) || machine != IMAGE_FILE_MACHINE_UNKNOWN ||
            native != IMAGE_FILE_MACHINE_AMD64) throw std::runtime_error("Target must be native Windows x64");
        if (!module(pid, L"OWGameEventsConsumer.dll").base) throw std::runtime_error("Target has no loaded consumer");
        Handle output{CreateFileW(argv[4], GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (output.value == INVALID_HANDLE_VALUE) error("Output must be a new writable file");
        struct Library { HMODULE value; ~Library() { if (value) FreeLibrary(value); } } local{LoadLibraryW(dll.c_str())};
        if (!local.value) error("Cannot load local capture exports");
        // LoadLibraryW may be forwarded into KernelBase: resolve its actual module
        // and rebase that module in the target, never assume identical ASLR bases.
        auto remote = module(pid, dll, true);
        if (!remote.base) {
            RemoteBuffer path(process.value, dll.c_str(), (dll.size() + 1) * sizeof(wchar_t));
            call(process.value, remote_system_function(pid, "LoadLibraryW"), &path);
            remote = module(pid, dll, true);
            if (!remote.base) throw std::runtime_error("Capture DLL did not load (check dependencies/target policy)");
        }
        const auto entry = [&](const char* name) {
            const auto symbol = GetProcAddress(local.value, name);
            if (!symbol) error("Capture export unavailable");
            return remote.base + reinterpret_cast<uintptr_t>(symbol) - reinterpret_cast<uintptr_t>(local.value);
        };
        Session session{process.value, entry("owh_probe_stop")};
        owh_probe_options options{sizeof(options), OWH_ABI_VERSION, GetCurrentProcessId(), source_channel,
            created(GetCurrentProcess()), {}};
        const auto name = "overwolf-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
        std::memcpy(options.channel, name.c_str(), name.size() + 1);
        RemoteBuffer configuration(process.value, &options, sizeof(options));
        session.started = true;
        const DWORD started = call(process.value, entry("owh_probe_start"), &configuration);
        if (started == OWH_ALREADY_EXISTS) session.started = false;
        if (started != OWH_OK) throw std::runtime_error("Probe start failed: status " + std::to_string(started));
        struct Reader { owh_reader* value = nullptr; ~Reader() { owh_reader_close(value); } } reader;
        // The newly created channel contains this capture session only. Include
        // records published between hook enable and this reader opening.
        if (owh_reader_open(1, options.channel, OWH_RETAINED, &reader.value).status != OWH_OK)
            throw std::runtime_error("Capture channel unavailable after start");
        LARGE_INTEGER qpc{}, frequency{};
        FILETIME utc{};
        QueryPerformanceCounter(&qpc); QueryPerformanceFrequency(&frequency); GetSystemTimePreciseAsFileTime(&utc);
        const uint64_t filetime = (uint64_t{utc.dwHighDateTime} << 32) | utc.dwLowDateTime;
        std::cout << "hook enabled; channel=" << options.channel
            << "; source=" << (source_channel ? std::to_string(source_channel) : "all")
            << "; capture=" << (seconds ? std::to_string(seconds) + " seconds" : "until-stop")
            << "; host_pid=" << pid << "; host_started=" << created(process.value)
            << "; started_qpc=" << qpc.QuadPart << "; qpc_frequency=" << frequency.QuadPart
            << "; started_filetime_utc=" << filetime << '\n' << std::flush;
        CaptureControl capture_control(seconds, console.stop.value, process.value, stop_file);
        uint64_t received = 0, lost = 0;
        bool stopped = false;
        bool target_exited = false;
        ULONGLONG drain_deadline = 0;
        DWORD stop_result = OWH_OK;
        for (;;) {
            const auto reason = capture_control.reason();
            if (!stopped && reason != StopReason::None) {
                target_exited = reason == StopReason::TargetExited;
                stop_result = target_exited ? OWH_CLOSED : call(process.value, session.stop);
                session.started = !target_exited && stop_result != OWH_OK;
                if (target_exited) stop_result = OWH_OK;
                if (stop_result != OWH_OK)
                    throw std::runtime_error("Capture did not stop: " + std::to_string(stop_result));
                stopped = true;
                drain_deadline = GetTickCount64() + 10000;
            }
            owh_probe_frame frame{}; owh_record record{};
            const auto result = owh_reader_next(reader.value, &frame, sizeof(frame), &record);
            if (result.status == OWH_NO_DATA || result.status == OWH_BUSY) {
                if (stopped && GetTickCount64() >= drain_deadline) throw std::runtime_error("Capture drain timed out");
                Sleep(1); continue;
            }
            if (result.status == OWH_CLOSED) break;
            if (result.status != OWH_OK || record.size != sizeof(frame))
                throw std::runtime_error("Invalid capture read: " + std::to_string(result.status));
            write(output.value, &record, sizeof(record)); write(output.value, &frame, sizeof(frame));
            ++received; lost += record.lost_before;
            if (received <= 5) {
                const auto* end = std::find(frame.raw, frame.raw + 255, 0);
                std::string event(reinterpret_cast<const char*>(frame.raw), static_cast<size_t>(end - frame.raw));
                for (auto& c : event) if (c < 32 || c > 126) c = '?';
                std::cout << "event " << record.sequence << " source=" << frame.source_channel
                    << ": " << event << '\n' << std::flush;
            }
        }
        if (!FlushFileBuffers(output.value)) error("Cannot flush capture file");
        // The host may exit after CLOSED is read but before diagnostics are queried.
        if (capture_control.reason() == StopReason::TargetExited) target_exited = true;
        if (target_exited) {
            session.started = false;
            std::cout << "received=" << received << " bridge_overwrites=" << lost
                << " target_exited=1 diagnostics=unavailable\n";
            return 0;
        }
        if (!stopped) {
            stop_result = call(process.value, session.stop);
            session.started = stop_result != OWH_OK;
        }
        owh_probe_statistics statistics{};
        RemoteBuffer stats(process.value, nullptr, sizeof(statistics));
        if (call(process.value, entry("owh_probe_query"), &stats) != OWH_OK)
            throw std::runtime_error("Capture diagnostics unavailable");
        SIZE_T read = 0;
        if (!ReadProcessMemory(process.value, stats.address, &statistics, sizeof(statistics), &read) || read != sizeof(statistics))
            error("Cannot read capture diagnostics");
        std::cout << "received=" << received << " bridge_overwrites=" << lost << " calls=" << statistics.calls
            << " seen=" << statistics.seen << " forwarded=" << statistics.forwarded << " rejected=" << statistics.rejected
            << " filtered=" << statistics.filtered << " invalid=" << statistics.invalid << " active=" << statistics.active_calls
            << " enabled=" << statistics.hook_enabled << " stop=" << stop_result << '\n';
        return stop_result == OWH_OK && !statistics.hook_enabled && !statistics.active_calls ? 0 : 1;
    } catch (const std::exception& problem) { std::cerr << problem.what() << '\n'; return 1; }
}
