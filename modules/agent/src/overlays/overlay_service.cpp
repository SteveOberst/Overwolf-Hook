#include "overlay_service.hpp"
#include <owh/ipc/session_transport.hpp>
#include <owh/platform/result.hpp>
#include <stdexcept>
namespace overwolf_hook::agent {
OverlayServiceImpl::OverlayServiceImpl(OverlayDomain &domain, ipc::MessageConnection &connection,
                                       SessionContext &context)
    : domain_(domain), context_(context) {
    HANDLE peer = nullptr;
    if (!connection.peer() ||
        !DuplicateHandle(GetCurrentProcess(), connection.peer(), GetCurrentProcess(), &peer, 0,
                         FALSE, DUPLICATE_SAME_ACCESS))
        throw std::runtime_error("cannot retain verified peer for pixel import");
    peer_.reset(peer);
}

core::CreateSourceResponse
OverlayServiceImpl::create_pixel_buffer(const core::CreatePixelBufferSourceRequest &request) noexcept {
    const auto &q = request.queue;
    if (request.ref.session_id != context_.session_id || !request.ref.resource_id ||
        request.geometry.size != sizeof(core::Geometry) ||
        !core::valid_geometry(request.geometry) || q.ref != request.ref ||
        q.producer_pid != context_.identity.client_process_id ||
        q.producer_started != context_.identity.client_process_started ||
        q.consumer_pid != context_.identity.host_process_id || q.width != request.geometry.width ||
        q.height != request.geometry.height ||
        !core::valid_queue({sizeof(core::PixelBufferOverlayOptions), q.slot_count, q.queue_policy}))
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), {}};
    if (!context_.admit(request.ref))
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), {}};
    core::CreateSourceResponse response{};
    const auto status = boundary([&] {
        auto [result, ref] = domain_.create_pixel_buffer(request.ref, request.geometry, q, peer_.get(), context_.policy);
        response = {result, ref};
        return result;
    });
    if (status.status != OWH_OK)
        response.result = status;
    return response;
}

core::CreateSourceResponse
OverlayServiceImpl::create_shared_texture(const core::CreateSharedTextureSourceRequest &request) noexcept {
    if (request.ref.session_id != context_.session_id || !request.ref.resource_id ||
        request.geometry.size != sizeof(core::Geometry) || !core::valid_geometry(request.geometry))
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), {}};
    if (!(context_.policy->experimental_features & OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE))
        return {make_result(OWH_OPT_IN_REQUIRED, OWH_STAGE_CAPABILITY), {}};
    if (!context_.admit(request.ref))
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), {}};
    core::CreateSourceResponse response{};
    const auto status = boundary([&] {
        auto [result, ref] = domain_.create_shared_texture(request.ref, request.geometry, context_.policy);
        response = {result, ref};
        return result;
    });
    if (status.status != OWH_OK)
        response.result = status;
    return response;
}

core::ResultResponse OverlayServiceImpl::close(const core::SourceRequest &request) noexcept {
    if (request.ref.session_id != context_.session_id || !request.ref.resource_id)
        return {make_result(OWH_STALE_GENERATION, OWH_STAGE_AGENT_LIFETIME)};
    return {domain_.close(request.ref)};
}
core::ResultResponse
OverlayServiceImpl::bind_texture(const core::BindD3D11Request &request) noexcept {
    if (request.ref.session_id != context_.session_id || !request.ref.resource_id)
        return {make_result(OWH_STALE_GENERATION, OWH_STAGE_AGENT_LIFETIME)};
    if (!(context_.policy->experimental_features & OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE))
        return {make_result(OWH_OPT_IN_REQUIRED, OWH_STAGE_CAPABILITY)};
    const auto &t = request.texture;
    if (t.size != sizeof(t) || t.reserved)
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT)};
    return {domain_.bind(request.ref, t)};
}
core::OverlayInfoResponse
OverlayServiceImpl::get_info(const core::SourceRequest &request) noexcept {
    if (request.ref.session_id != context_.session_id || !request.ref.resource_id)
        return {make_result(OWH_STALE_GENERATION, OWH_STAGE_AGENT_LIFETIME), {}};
    auto [result, info] = domain_.info(request.ref);
    return {result, info};
}
core::OverlayCapabilitiesResponse OverlayServiceImpl::capabilities(const core::Empty &) noexcept {
    core::OverlayCapabilitiesResponse out{};
    out.value = domain_.capabilities(*context_.policy, context_.identity.host_process_id,
                                     context_.identity.host_process_started, context_.session_id);
    out.result = make_result(OWH_OK);
    return out;
}


} // namespace overwolf_hook::agent
