#include "event_service.hpp"
#include <cstring>
#include <owh/platform/result.hpp>
namespace overwolf_hook::agent {
EventServiceImpl::EventServiceImpl(events::EventHub &backend, SessionContext &context)
    : backend_(backend), context_(context) {}

core::SubscribeEventsResponse
EventServiceImpl::subscribe(const core::SubscribeEventsRequest &request) noexcept {
    if (request.ref.session_id != context_.session_id || !request.ref.resource_id ||
        request.reserved)
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), {}, {}};
    if (!context_.admit(request.ref))
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), {}, {}};
    auto [status, stream] = backend_.subscribe(request.ref);
    if (status.status != OWH_OK) return {status};
    core::SubscribeEventsResponse response{};
    response.result = make_result(OWH_OK); response.ref = request.ref;
    std::memcpy(response.channel, stream.channel.c_str(), stream.channel.size() + 1);
    response.start_sequence = stream.position.next; response.capture_loss = stream.position.capture_loss;
    response.wake_handle = stream.wake_handle; response.closed_handle = stream.closed_handle;
    return response;
}
core::EventStatisticsResponse
EventServiceImpl::statistics(const core::SourceRequest &request) noexcept {
    if (request.ref.session_id != context_.session_id || !request.ref.resource_id)
        return {make_result(OWH_STALE_GENERATION, OWH_STAGE_AGENT_LIFETIME), {}};
    owh_subscription_info info{};
    const auto status = backend_.query({request.ref.session_id, request.ref.resource_id}, info);
    return {status, info};
}
core::ResultResponse EventServiceImpl::close(const core::SourceRequest &request) noexcept {
    if (request.ref.session_id != context_.session_id || !request.ref.resource_id)
        return {make_result(OWH_STALE_GENERATION, OWH_STAGE_AGENT_LIFETIME)};
    const auto status = backend_.close({request.ref.session_id, request.ref.resource_id});
    return {status};
}
core::EventCapabilitiesResponse EventServiceImpl::capabilities(const core::Empty &) noexcept {
    core::EventCapabilitiesResponse out{};
    out.value.size = sizeof(out.value);
    out.value.process_id = context_.identity.host_process_id;
    out.value.process_started = context_.identity.host_process_started;
    out.value.session_id = context_.session_id;
    out.value.status = backend_.capability();
    out.result = make_result(OWH_OK);
    return out;
}


} // namespace overwolf_hook::agent
