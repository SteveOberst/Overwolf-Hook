#pragma once
#include <memory>
#include <overwolf_hook/types/events.h>
#include <owh/core/types.hpp>
#include <owh/ipc/event_log.hpp>

namespace overwolf_hook::client {
class AttachedSession;
/** One consuming live stream, pinned to its original attached session. */
class EventSubscription final {
  public:
    static std::pair<owh_result, std::unique_ptr<EventSubscription>>
    create(std::shared_ptr<AttachedSession>, const owh_event_options *) noexcept;
    ~EventSubscription();
    owh_result statistics(owh_subscription_info &) noexcept;
    owh_result poll(owh_event &) noexcept;
    owh_result receive(uint32_t timeout_ms, HANDLE cancellation, owh_event &) noexcept;
    owh_result close() noexcept;
  private:
    std::shared_ptr<AttachedSession> connection_;
    core::ResourceRef ref_{};
    std::unique_ptr<ipc::EventCursor> cursor_;
};
} // namespace overwolf_hook::client
