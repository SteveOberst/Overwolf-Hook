#pragma once
#include <owh/core/types.hpp>
#include <owh/ipc/event_log.hpp>
#include <owh/overwolf/event_source.hpp>
#include <memory>
#include <string>

namespace overwolf_hook::agent::events {
using SubscriptionId = core::ResourceRef;
struct SubscriptionStream {
    std::string channel;
    ipc::EventPosition position;
    uint64_t wake_handle = 0, closed_handle = 0;
};
/** One bounded live publication ring, owned by the resident Agent. */
class EventHub final {
  public:
    explicit EventHub(overwolf::EventSource &);
    ~EventHub();
    EventHub(const EventHub &) = delete;
    EventHub &operator=(const EventHub &) = delete;
    owh_result capability() const noexcept;
    std::pair<owh_result, SubscriptionStream> subscribe(SubscriptionId) noexcept;
    owh_result query(SubscriptionId, owh_subscription_info &) noexcept;
    owh_result close(SubscriptionId) noexcept;
    bool has_children(uint64_t session_id) const noexcept;
    owh_result cleanup_owner(uint64_t session_id) noexcept;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace overwolf_hook::agent::events
