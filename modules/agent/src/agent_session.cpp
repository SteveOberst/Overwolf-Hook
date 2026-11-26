#include "agent_session.hpp"
#include <cstring>
#include <owh/ipc/services.hpp>
#include <owh/platform/result.hpp>
namespace overwolf_hook::agent {
SessionServiceImpl::SessionServiceImpl(SessionContext &context) : context_(context) {}

core::HelloResponse SessionServiceImpl::hello(const core::HelloRequest &request) noexcept {
    core::HelloResponse response{};
    response.result = make_result(OWH_INCOMPATIBLE, OWH_STAGE_HANDSHAKE);
    response.abi_version = OWH_ABI_VERSION;
    response.wire_protocol = ipc::protocol;
    response.host_process_id = context_.identity.host_process_id;
    response.host_process_started = context_.identity.host_process_started;
    response.session_id = context_.session_id;
    response.build_info = context_.identity.build_info;
    if (context_.policy) {
        response.result = make_result(OWH_CLOSED, OWH_STAGE_HANDSHAKE);
        return response;
    }
    if (request.abi_version != OWH_ABI_VERSION || request.reserved ||
        request.wire_protocol != ipc::protocol ||
        request.client_process_id != context_.identity.client_process_id ||
        request.client_process_started != context_.identity.client_process_started ||
        request.host_process_started != context_.identity.host_process_started ||
        request.windows_session_id != context_.identity.windows_session_id ||
        std::memcmp(&request.build_info, &context_.identity.build_info, sizeof(owh_build_info)))
        return response;
    if (!core::valid_policy(request.options)) {
        response.result = make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
        return response;
    }
    response.result = boundary([&] {
        context_.policy = std::make_shared<const core::SessionPolicy>(request.options);
        return make_result(OWH_OK);
    });
    if (response.result.status != OWH_OK) return response;
    response.policy = *context_.policy;
    response.result = make_result(OWH_OK);
    return response;
}

core::ResultResponse SessionServiceImpl::detach(const core::Empty &) noexcept {
    if (!context_.policy)
        return {make_result(OWH_AGENT_NOT_READY, OWH_STAGE_HANDSHAKE)};
    context_.policy.reset();
    return {make_result(OWH_OK)};
}
core::ResultResponse SessionServiceImpl::ping(const core::Empty &) noexcept {
    return {context_.policy != nullptr ? make_result(OWH_OK) : make_result(OWH_CLOSED)};
}
AgentSession::AgentSession(OverlayDomain &overlays, events::EventHub &event_hub,
                           ipc::MessageConnection &connection, const core::PeerIdentity &identity,
                           uint64_t session)
    : context_(identity, session), overlays_(overlays, connection, context_),
      events_(event_hub, context_), session_(context_) {}
void AgentSession::bind(ipc::ServiceRouter &router) {
    router.bind(session_);
    router.bind(overlays_);
    router.bind(events_);
}

} // namespace overwolf_hook::agent
