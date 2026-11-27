#pragma once
#include "events/event_service.hpp"
#include "overlays/overlay_service.hpp"
#include "session_context.hpp"
namespace overwolf_hook::ipc {
class ServiceRouter;
}
namespace overwolf_hook::agent {
class SessionServiceImpl final : public core::SessionService {
  public:
    explicit SessionServiceImpl(SessionContext &);
    core::HelloResponse hello(const core::HelloRequest &) noexcept override;
    core::ResultResponse detach(const core::Empty &) noexcept override;
    core::ResultResponse ping(const core::Empty &) noexcept override;

  private:
    SessionContext &context_;
};

class AgentSession final {
  public:
    AgentSession(OverlayDomain &, events::EventHub &, ipc::MessageConnection &,
                 const core::PeerIdentity &, uint64_t);
    AgentSession(const AgentSession &) = delete;
    AgentSession &operator=(const AgentSession &) = delete;
    void bind(ipc::ServiceRouter &);

  private:
    SessionContext context_;
    OverlayServiceImpl overlays_;
    EventServiceImpl events_;
    SessionServiceImpl session_;
};

} // namespace overwolf_hook::agent
