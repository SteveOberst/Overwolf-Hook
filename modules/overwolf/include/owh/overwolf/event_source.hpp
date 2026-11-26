#pragma once

#include <owh/core/types.hpp>
#include <owh/platform/result.hpp>

#include <cstdint>
#include <memory>

namespace overwolf_hook::overwolf {

struct EventSourceMetrics {
    bool enabled = false;
    uint32_t warnings = 0;
};

/** Activates a process-resident decoded-event dispatcher hook and emits copied PODs.
 * shutdown retires only this callback; detour/trampoline storage is never removed
 * or rebound to another owner. Real-hook fixtures use independent processes. */
class EventSource final {
  public:
    using Callback = void (*)(void *, const core::CapturedEvent &) noexcept;

    EventSource();
    ~EventSource();
    EventSource(const EventSource &) = delete;
    EventSource &operator=(const EventSource &) = delete;

    owh_result capability() const noexcept;
    /** Resolve/install the permanent hook and bind its owner. Export stays
     * disabled until set_enabled(true), after the receiver is ready. */
    owh_result prepare(Callback callback, void *context) noexcept;
    void set_enabled(bool enabled) noexcept;
    EventSourceMetrics metrics() const noexcept;
    void shutdown() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace overwolf_hook::overwolf
