#pragma once
#include "../session_context.hpp"
#include "event_hub.hpp"
#include <owh/core/services.hpp>
namespace overwolf_hook::agent {
class EventServiceImpl final : public core::EventService {
  public:
    EventServiceImpl(events::EventHub &, SessionContext &);
    core::SubscribeEventsResponse subscribe(const core::SubscribeEventsRequest &) noexcept override;
    core::EventStatisticsResponse statistics(const core::SourceRequest &) noexcept override;
    core::ResultResponse close(const core::SourceRequest &) noexcept override;
    core::EventCapabilitiesResponse capabilities(const core::Empty &) noexcept override;

  private:
    events::EventHub &backend_;
    SessionContext &context_;
};

} // namespace overwolf_hook::agent
