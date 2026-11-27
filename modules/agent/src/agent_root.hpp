#pragma once

#include "agent_server.hpp"
#include "events/event_hub.hpp"
#include "overlays/overlay_domain.hpp"
#include <mutex>
#include <owh/overwolf/event_source.hpp>
#include <owh/overwolf/overlays/overlay_host.hpp>

namespace overwolf_hook::agent {

/** Process-lifetime owner for inert Agent services and independent client
 * admission. Constructing this root allocates local state only; HELLO and the
 * first source operation determine whether capture hooks or the overlay
 * executor activate.
 */
class AgentRoot final {
  public:
    explicit AgentRoot(overwolf::LifetimeAdmission &admission);
    AgentRoot(const AgentRoot &) = delete;
    AgentRoot &operator=(const AgentRoot &) = delete;

    OverlayDomain &overlays() noexcept { return overlays_; }
    events::EventHub &events() noexcept { return events_; }
    owh_result start_server() noexcept;

  private:
    overwolf::OverlayHost overlay_host_;
    OverlayDomain overlays_;
    ::overwolf_hook::overwolf::EventSource event_source_;
    events::EventHub events_;
    std::mutex server_mutex_;
    std::unique_ptr<AgentServer> server_;
};

AgentRoot *process_agent_root(overwolf::LifetimeAdmission &admission) noexcept;

} // namespace overwolf_hook::agent
