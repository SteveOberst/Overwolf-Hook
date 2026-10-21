#include "../tools/capture_control.hpp"
#include <iostream>

namespace {
void require(bool test) { if (!test) throw std::runtime_error("controller assertion failed"); }
struct Handle { HANDLE value; ~Handle() { if (value) CloseHandle(value); } };
}
int main() {
    try {
        using namespace overwolf_hook::controller;
        Handle request{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        Handle target{CreateEventW(nullptr, TRUE, FALSE, nullptr)}; // Controlled lifetime signal.
        require(request.value && target.value);
        CaptureControl indefinite(0, request.value, target.value);
        require(indefinite.reason() == StopReason::None);
        CaptureControl long_duration(86400, request.value, target.value);
        require(long_duration.reason() == StopReason::None); // No former 120-second cap.
        SetEvent(request.value);
        require(indefinite.reason() == StopReason::Requested && long_duration.reason() == StopReason::Requested);
        ResetEvent(request.value); SetEvent(target.value);
        require(indefinite.reason() == StopReason::TargetExited);
        ResetEvent(target.value);
        CaptureControl short_duration(1, request.value, target.value);
        Sleep(1050); require(short_duration.reason() == StopReason::Deadline);
        wchar_t temporary[MAX_PATH]; require(GetTempPathW(MAX_PATH, temporary));
        auto file = std::filesystem::path(temporary) / (L"owh-stop-test-" + std::to_wstring(GetCurrentProcessId()));
        require(!std::filesystem::exists(file));
        CaptureControl stop_file(0, request.value, target.value, file);
        require(stop_file.reason() == StopReason::None);
        {
            Handle created{CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
            require(created.value != INVALID_HANDLE_VALUE);
        }
        const auto reason = stop_file.reason();
        require(DeleteFileW(file.c_str())); require(reason == StopReason::Requested);
        for (const auto* bad : {L"", L"-1", L"12junk", L"18446744073709551616"}) {
            bool rejected = false;
            try { number(bad, "bad number"); } catch (const std::runtime_error&) { rejected = true; }
            require(rejected);
        }
        bool rejected = false;
        try { CaptureControl overflow(UINT64_MAX, request.value, target.value); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected);
        std::cout << "PASS indefinite, long duration, explicit stop, deadline, target death, stop file, invalid numbers\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
