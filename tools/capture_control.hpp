#pragma once

#include <windows.h>
#include <charconv>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <algorithm>

namespace overwolf_hook::controller {
inline uint64_t number(std::wstring_view value, const char* description) {
    if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring_view::npos)
        throw std::runtime_error(description);
    std::string digits;
    for (const auto digit : value) digits.push_back(static_cast<char>(digit)); // ASCII validated above.
    uint64_t output = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), output);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) throw std::runtime_error(description);
    return output;
}

enum class StopReason { None, Requested, Deadline, TargetExited };

/** Own timer and stop signal. Provider notifications never participate. */
class CaptureControl {
public:
    CaptureControl(uint64_t seconds, HANDLE request, HANDLE target, std::filesystem::path stop_file = {})
        : request_(request), target_(target), stop_file_(std::move(stop_file)) {
        const auto now = GetTickCount64();
        if (seconds > (std::numeric_limits<ULONGLONG>::max() - now) / 1000)
            throw std::runtime_error("Capture duration is too large");
        deadline_ = seconds ? now + seconds * 1000 : 0;
    }
    StopReason reason() const {
        if (target_) {
            const auto waited = WaitForSingleObject(target_, 0);
            if (waited == WAIT_OBJECT_0) return StopReason::TargetExited;
            if (waited != WAIT_TIMEOUT) throw std::runtime_error("Cannot check target lifetime");
        }
        if (request_) {
            const auto waited = WaitForSingleObject(request_, 0);
            if (waited == WAIT_OBJECT_0) return StopReason::Requested;
            if (waited != WAIT_TIMEOUT) throw std::runtime_error("Cannot check controller stop signal");
        }
        if (!stop_file_.empty()) {
            std::error_code error;
            const auto exists = std::filesystem::exists(stop_file_, error);
            if (error) throw std::runtime_error("Cannot inspect the capture stop file: " + error.message());
            if (exists) return StopReason::Requested;
        }
        return deadline_ && GetTickCount64() >= deadline_ ? StopReason::Deadline : StopReason::None;
    }
    /** Sleep on kernel signals/directory changes until control changes. finished
     * ends monitoring when the receiver has drained; no event-reception timer loop.
     */
    StopReason wait(HANDLE finished) const {
        struct Changes {
            HANDLE value = INVALID_HANDLE_VALUE;
            ~Changes() { if (value != INVALID_HANDLE_VALUE) FindCloseChangeNotification(value); }
        } changes;
        if (!stop_file_.empty()) {
            const auto parent = std::filesystem::absolute(stop_file_).parent_path();
            changes.value = FindFirstChangeNotificationW(parent.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
            if (changes.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot watch capture stop-file directory");
        }
        HANDLE handles[4]{finished}; DWORD count = 1;
        if (target_) handles[count++] = target_;
        if (request_) handles[count++] = request_;
        if (changes.value != INVALID_HANDLE_VALUE) handles[count++] = changes.value;
        for (;;) {
            if (const auto current = reason(); current != StopReason::None) return current;
            const auto now = GetTickCount64();
            const DWORD timeout = deadline_ ? static_cast<DWORD>(std::min<ULONGLONG>(
                deadline_ > now ? deadline_ - now : 0, INFINITE - 1)) : INFINITE;
            const auto result = WaitForMultipleObjects(count, handles, FALSE, timeout);
            if (result == WAIT_OBJECT_0) return StopReason::None;
            if (result == WAIT_FAILED) throw std::runtime_error("Cannot wait for capture control");
            if (changes.value != INVALID_HANDLE_VALUE && result == WAIT_OBJECT_0 + count - 1 &&
                !FindNextChangeNotification(changes.value)) throw std::runtime_error("Cannot rearm stop-file notification");
        }
    }
private:
    ULONGLONG deadline_ = 0;
    HANDLE request_, target_;
    std::filesystem::path stop_file_;
};
}
