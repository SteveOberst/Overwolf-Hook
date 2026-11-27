#pragma once
#include <owh/core/types.hpp>
#include <memory>
namespace overwolf_hook::agent {
struct SessionContext {
    core::PeerIdentity identity;
    uint64_t session_id = 0;
    std::shared_ptr<const core::SessionPolicy> policy;
    uint64_t last_resource_id = 0;
    SessionContext(const core::PeerIdentity &, uint64_t);
    bool admit(core::ResourceRef) noexcept;
};

inline SessionContext::SessionContext(const core::PeerIdentity &args, uint64_t id)
    : identity(args), session_id(id) {}

inline bool SessionContext::admit(core::ResourceRef ref) noexcept {
    if (!policy || ref.session_id != session_id || !ref.resource_id)
        return false;
    // ipc::serve dispatches one request at a time per session connection.
    if (ref.resource_id <= last_resource_id)
        return false;
    last_resource_id = ref.resource_id;
    return true;
}

} // namespace overwolf_hook::agent
