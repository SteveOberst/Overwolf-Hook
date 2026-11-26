#include <windows.h>
#include "capture_control.hpp"
#include "capture_recording.hpp"
#include <algorithm>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <overwolf_hook/overwolf_hook.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {
HANDLE console_stop = nullptr;
HANDLE console_done = nullptr;
BOOL WINAPI console_handler(DWORD signal) {
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT && signal != CTRL_CLOSE_EVENT &&
        signal != CTRL_LOGOFF_EVENT && signal != CTRL_SHUTDOWN_EVENT)
        return FALSE;
    SetEvent(console_stop);
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT)
        WaitForSingleObject(console_done, 4000);
    return TRUE;
}
[[noreturn]] void error(const char *what) {
    throw std::runtime_error(std::string(what) + " (Win32 " + std::to_string(GetLastError()) + ")");
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
struct ConsoleControl {
    Handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ConsoleControl() {
        if (!stop.value || !done.value)
            error("Cannot create controller stop signal");
        console_stop = stop.value;
        console_done = done.value;
        if (!SetConsoleCtrlHandler(console_handler, TRUE))
            error("Cannot register console stop handler");
    }
    ~ConsoleControl() {
        SetEvent(done.value);
        SetConsoleCtrlHandler(console_handler, FALSE);
    }
};
void print_result(const char *operation, const owh_result &result) {
    std::cerr << operation << " failed: status=" << result.status
              << " stage=" << result.failure_stage << " domain=" << result.error_domain
              << " native=0x" << std::hex << result.native_code << std::dec << '\n';
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        const bool streaming = argc >= 2 && std::wstring_view(argv[1]) == L"stream";
        const int base = streaming ? 2 : 1;
        if ((!streaming && argc < 4) || (streaming && argc < 3)) {
            std::wcerr
                << L"Usage: owh_controller <Overwolf PID> <seconds|0=until-stop> <new capture "
                   L"file> [all|source-channel] [--stop-file path] [--ready-file path]\n"
                << L"       owh_controller stream <Overwolf PID> [all|source-channel] [--stop-file "
                   L"path] [--ready-file path]\n";
            return 2;
        }
        using namespace overwolf_hook::controller;
        const auto pid_value = number(argv[base], "PID must be a nonzero uint32");
        if (!pid_value || pid_value > UINT32_MAX)
            throw std::runtime_error("PID must be a nonzero uint32");
        const DWORD pid = static_cast<DWORD>(pid_value);
        uint64_t seconds = 0;
        int option_index = base + 1;
        if (!streaming) {
            seconds = number(argv[base + 1], "Duration must be whole seconds, or 0 until stop");
            option_index = base + 3;
        }
        uint32_t source_channel = OWH_EVENTS_ALL_CHANNELS;
        if (option_index < argc && std::wstring_view(argv[option_index]) != L"--stop-file" &&
            std::wstring_view(argv[option_index]) != L"--ready-file") {
            if (std::wstring_view(argv[option_index]) != L"all") {
                const auto value = number(argv[option_index],
                                          "Source must be 'all' or a nonzero uint32 channel ID");
                if (!value || value > UINT32_MAX)
                    throw std::runtime_error("Source channel must fit uint32");
                source_channel = static_cast<uint32_t>(value);
            }
            ++option_index;
        }
        std::filesystem::path stop_file, ready_file;
        for (int i = option_index; i < argc; i += 2) {
            const std::wstring_view option = argv[i];
            auto &path = option == L"--stop-file" ? stop_file : ready_file;
            if ((option != L"--stop-file" && option != L"--ready-file") || i + 1 >= argc)
                throw std::runtime_error("Expected --stop-file or --ready-file followed by a path");
            if (!path.empty())
                throw std::runtime_error("Duplicate controller option");
            path = std::filesystem::absolute(argv[i + 1]);
            if (std::filesystem::exists(path))
                throw std::runtime_error("Control file must not already exist");
        }
        ConsoleControl console;
        (void)CaptureControl(seconds, console.stop.value, nullptr, stop_file);
        owh_client_options host_options = OWH_CLIENT_OPTIONS_INIT;
        host_options.process_id = pid;
        host_options.attach_mode = OWH_ATTACH_HOT;
        auto sdk = overwolf_hook::OverwolfHook::open();
        try {
            sdk.attach(&host_options);
        } catch (const overwolf_hook::NativeError &failure) {
            print_result("Host attach", failure.result());
            return 1;
        }
        const auto caps = sdk.events().capabilities();
        if (caps.status.status != OWH_OK) {
            print_result("Event capability", caps.status);
            return 1;
        }
        const owh_event_options options{sizeof(owh_event_options), source_channel};
        auto capture = sdk.events().subscribe(&options);
        auto info = capture.info();
        const auto cleanup = [&] {
            capture.close();
            sdk.detach();
        };
        try {
            Handle output;
            const bool record_output = !streaming;
            if (record_output) {
                output.value = CreateFileW(argv[base + 2], GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (output.value == INVALID_HANDLE_VALUE)
                    error("Output must be a new writable file");
            }
            std::cout << "capture active; source="
                      << (source_channel ? std::to_string(source_channel) : "all") << "; duration="
                      << (streaming
                              ? "until-stop"
                              : (seconds ? std::to_string(seconds) + " seconds" : "until-stop"))
                      << "; recording=" << (record_output ? "on" : "off") << "; host_pid=" << pid
                      << "; started_qpc=" << info.started_qpc
                      << "; qpc_frequency=" << info.qpc_frequency
                      << "; started_filetime_utc=" << info.started_utc_filetime << '\n'
                      << std::flush;
            if (info.warnings & OWH_SUBSCRIPTION_WARNING_UNREVIEWED_CONSUMER)
                std::cerr << "warning: consumer hash differs from the reviewed reference; "
                             "structural signatures passed\n";
            if (info.warnings & OWH_SUBSCRIPTION_WARNING_HASH_UNAVAILABLE)
                std::cerr
                    << "warning: consumer hash is unavailable; structural signatures passed\n";
            if (!ready_file.empty()) {
                Handle ready{CreateFileW(ready_file.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                         nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
                if (ready.value == INVALID_HANDLE_VALUE)
                    error("Cannot create controller readiness file");
            }

            CaptureControl control(seconds, console.stop.value, nullptr, stop_file);
            uint64_t received = 0, lost = 0;
            bool target_exited = false;
            std::exception_ptr stop_failure;
            Handle monitor_done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            if (!monitor_done.value)
                error("Cannot create capture monitor signal");
            owh_cancel *raw_cancel = nullptr;
            overwolf_hook::check(owh_cancel_create(&raw_cancel));
            std::unique_ptr<owh_cancel, decltype(&owh_cancel_close)> cancellation(raw_cancel, owh_cancel_close);
            const owh_wait_options wait{sizeof(wait), OWH_WAIT_INFINITE, raw_cancel};
            std::thread monitor([&] {
                try {
                    const auto reason = control.wait(monitor_done.value);
                    if (reason == StopReason::None)
                        return;
                    target_exited = reason == StopReason::TargetExited;
                    if (target_exited)
                        return;
                    owh_cancel_signal(raw_cancel);
                } catch (...) {
                    stop_failure = std::current_exception();
                    owh_cancel_signal(raw_cancel);
                }
            });
            std::exception_ptr read_failure;
            try {
                for (;;) {
                    owh_event frame{};
                    try {
                        auto next = capture.receive(&wait);
                        if (!next) continue;
                        frame = *next;
                    } catch (const overwolf_hook::NativeError &error) {
                        if (error.result().status == OWH_CLOSED ||
                            error.result().status == OWH_CANCELLED)
                            break;
                        throw;
                    }
                    if (record_output && !write_capture_record(output.value, frame))
                        error("Raw capture write failed");
                    ++received;
                    lost += frame.lost_before;
                    if (received <= 5) {
                        const auto *end = std::find(frame.raw, frame.raw + 255, 0);
                        std::string name(reinterpret_cast<const char *>(frame.raw),
                                         static_cast<size_t>(end - frame.raw));
                        for (auto &c : name)
                            if (c < 32 || c > 126)
                                c = '?';
                        std::cout << "event " << frame.sequence
                                  << " source=" << frame.source_channel << ": " << name << '\n';
                    }
                }
            } catch (...) {
                read_failure = std::current_exception();
            }
            SetEvent(monitor_done.value);
            if (monitor.joinable())
                monitor.join();
            if (read_failure)
                std::rethrow_exception(read_failure);
            if (target_exited) {
                cleanup();
                std::cout << "received=" << received << " bridge_overwrites=" << lost
                          << " target_exited=1 diagnostics=unavailable\n";
                return 0;
            }
            if (stop_failure)
                std::rethrow_exception(stop_failure);
            {
                try {
                    info = capture.info();
                    capture.close();
                } catch (const overwolf_hook::NativeError &error) {
                    if (error.result().status != OWH_AGENT_LOST)
                        throw;
                    cleanup();
                    std::cout << "received=" << received << " bridge_overwrites=" << lost
                              << " target_exited=1 diagnostics=unavailable\n";
                    return 0;
                }
            }
            if (record_output && !FlushFileBuffers(output.value))
                error("Cannot flush capture file");
            cleanup();
            std::cout << "received=" << received << " bridge_overwrites=" << lost
                      << " enabled=" << info.hook_enabled << '\n';
            return 0;
        } catch (...) {
            cleanup();
            throw;
        }
    } catch (const std::exception &problem) {
        std::cerr << problem.what() << '\n';
        return 1;
    }
}
