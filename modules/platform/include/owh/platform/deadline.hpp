#pragma once

#include <windows.h>
#include <cstdint>

namespace overwolf_hook {
/** One monotonic time budget shared by every stage of a blocking operation. */
class Deadline final {
  public:
    explicit Deadline(uint32_t timeout_ms) noexcept
        : infinite_(timeout_ms == INFINITE), end_(infinite_ ? 0 : GetTickCount64() + timeout_ms),
          nonblocking_(timeout_ms == 0) {}

    DWORD remaining_ms() const noexcept {
        if (infinite_)
            return INFINITE;
        const auto now = GetTickCount64();
        return now >= end_ ? 0 : static_cast<DWORD>(end_ - now);
    }
    bool expired() const noexcept { return !infinite_ && remaining_ms() == 0; }
    bool nonblocking() const noexcept { return nonblocking_; }

  private:
    bool infinite_;
    ULONGLONG end_;
    bool nonblocking_;
};
} // namespace overwolf_hook
