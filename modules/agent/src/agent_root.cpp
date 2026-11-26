#include "agent_root.hpp"

#include <owh/overwolf/overlays/lifetime_admission.hpp>

#include <mutex>
#include <new>
#include <owh/platform/result.hpp>

namespace overwolf_hook::agent {

AgentRoot::AgentRoot(overwolf::LifetimeAdmission &admission)
    : overlay_host_(admission), overlays_(overlay_host_), events_(event_source_) {}

owh_result AgentRoot::start_server() noexcept {
    return boundary([&] {
        std::lock_guard lock(server_mutex_);
        if (!server_)
            server_ = std::make_unique<AgentServer>(overlays_, events_);
        return ok;
    });
}

AgentRoot *process_agent_root(overwolf::LifetimeAdmission &admission) noexcept {
    static std::mutex mutex;
    static AgentRoot *services = nullptr;
    std::lock_guard lock(mutex);
    if (!services) {
        try {
            services = new AgentRoot(admission);
        } catch (...) {
            return nullptr;
        }
    }
    return services;
}

} // namespace overwolf_hook::agent
