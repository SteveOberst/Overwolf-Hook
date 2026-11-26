#pragma once
#include <memory>
#include <overwolf_hook/types/common.h>

namespace overwolf_hook::agent {
class OverlayDomain;
namespace events {
class EventHub;
}
/** Resident multi-client endpoint. Construction starts no Overwolf feature. */
class AgentServer final {
  public:
    AgentServer(OverlayDomain &, events::EventHub &);
    ~AgentServer();
    AgentServer(const AgentServer &) = delete;
    AgentServer &operator=(const AgentServer &) = delete;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace overwolf_hook::agent
