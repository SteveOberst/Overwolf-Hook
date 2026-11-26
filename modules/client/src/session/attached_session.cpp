#include "attached_session.hpp"
#include "BuildInfo.hpp"
#include "target.hpp"
#include <cstring>
#include <overwolf_hook/types/agent.h>
#include <owh/core/service_contracts.hpp>
#include <owh/ipc/session_protocol.hpp>
#include <owh/ipc/session_transport.hpp>
#include <owh/platform/identity.hpp>
using namespace overwolf_hook;
using namespace overwolf_hook::client::detail;
using namespace overwolf_hook::core;
namespace {
bool compatible(const owh_agent_info &info) noexcept {
    return info.abi_version == OWH_ABI_VERSION && info.wire_version == ipc::protocol &&
           std::memcmp(&info.build_info, &build_info::value, sizeof(owh_build_info)) == 0;
}
} // namespace

namespace overwolf_hook::client {
owh_result AttachedSession::probe(const owh_client_options *input,
                                  owh_agent_info *output) noexcept {
    if (!output || output->size != sizeof(*output) || (input && !core::valid_options(*input)))
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
    *output = {};
    output->size = sizeof(*output);
    return boundary([&] {
        auto [status, target] = overwolf::select_process(input ? input->process_id : 0,
                                                    input ? input->process_started : 0);
        if (status.status != OWH_OK)
            return status;
        *output = ipc::AgentEndpoint::open(target.pid, target.started)->info();
        return ok;
    });
}
std::pair<owh_result, std::shared_ptr<AttachedSession>>
AttachedSession::attach(const owh_client_options *input) {
    const owh_client_options defaults = OWH_CLIENT_OPTIONS_INIT;
    if (!input)
        input = &defaults;
    std::shared_ptr<AttachedSession> connection;
    const auto outcome = boundary([&] {
        if (!core::valid_options(*input))
            return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
        const auto options = resolve_options(*input);
        auto [status, target] =
            overwolf::select_process(input->process_id, input->process_started);
        const bool restart = input->attach_mode == OWH_ATTACH_RESTART;
        if (restart) {
            if (status.status == OWH_OK) {
                status = host::stop(target.pid, target.started, 10000);
                if (status.status != OWH_OK)
                    return status;
            } else if (status.status != OWH_NOT_FOUND || input->process_id ||
                       input->process_started) {
                return status;
            }
            owh_overwolf_process launched{};
            status = host::start(10000, launched);
            if (status.status != OWH_OK)
                return status;
            auto selected =
                overwolf::select_process(launched.process_id, launched.process_started);
            status = selected.first;
            target = std::move(selected.second);
        }
        if (status.status != OWH_OK)
            return status;
        std::unique_ptr<ipc::AgentEndpoint> endpoint;
        const auto bootstrap = [&] {
            return bootstrap_agent(target, platform::process_start(GetCurrentProcess()),
                                   input->attach_mode);
        };
        if (restart) {
            const auto loaded = bootstrap();
            if (loaded.status != OWH_OK)
                return loaded;
            endpoint = ipc::AgentEndpoint::open(target.pid, target.started);
        } else {
            try {
                endpoint = ipc::AgentEndpoint::open(target.pid, target.started);
            } catch (const Failure &error) {
                if (error.result.status != OWH_NOT_FOUND)
                    return error.result;
                const auto loaded = bootstrap();
                if (loaded.status != OWH_OK)
                    return loaded;
                endpoint = ipc::AgentEndpoint::open(target.pid, target.started);
            }
        }
        if (!compatible(endpoint->info()))
            return make_result(OWH_INCOMPATIBLE, OWH_STAGE_HANDSHAKE);
        auto value = std::shared_ptr<AttachedSession>(new AttachedSession());
        value->agent_info_ = endpoint->info();
        value->pid_ = target.pid;
        value->started_ = target.started;
        value->transport_ = ipc::MessageConnection::create(target.pid, target.started);
        if (!endpoint->submit(value->transport_->descriptor()))
            return make_result(OWH_BUSY, OWH_STAGE_HANDSHAKE, OWH_ERROR_WIN32, GetLastError());

        HelloRequest request{};
        request.abi_version = OWH_ABI_VERSION;
        request.client_process_id = GetCurrentProcessId();
        request.windows_session_id = target.session;
        request.client_process_started = platform::process_start(GetCurrentProcess());
        request.host_process_started = target.started;
        request.wire_protocol = ipc::protocol;
        request.build_info = build_info::value;
        request.options = options;
        const auto queried = value->handshake(request);
        if (queried.status != OWH_OK)
            return queried;
        connection = std::move(value);
        return ok;
    });
    return {outcome, std::move(connection)};
}
owh_result AttachedSession::handshake(const core::HelloRequest &request) noexcept {
    return boundary([&] {
        calls_ =
            std::make_unique<ipc::ServiceConnection>(*transport_, transport_->descriptor().nonce);
        session_ = std::make_unique<ipc::SessionClient>(*calls_);
        const auto greeted = session_->hello(request);
        if (greeted.result.status != OWH_OK)
            return greeted.result;
        if (greeted.abi_version != OWH_ABI_VERSION || greeted.wire_protocol != ipc::protocol ||
            greeted.reserved || !greeted.session_id || greeted.host_process_id != pid_ ||
            greeted.host_process_started != started_ ||
            std::memcmp(&greeted.build_info, &request.build_info, sizeof(owh_build_info)) ||
            !core::valid_policy(greeted.policy) ||
            !core::same_policy(greeted.policy, request.options)) {
            invalidate();
            return make_result(OWH_INCOMPATIBLE, OWH_STAGE_HANDSHAKE);
        }
        id_ = greeted.session_id;
        calls_->establish(id_);
        overlays_ = std::make_unique<ipc::OverlayClient>(*calls_);
        events_ = std::make_unique<ipc::EventClient>(*calls_);
        policy_ = greeted.policy;
        return ok;
    });
}
owh_result AttachedSession::overlay_capabilities(owh_overlay_capabilities &output) noexcept {
    const auto reply = overlays_->capabilities({});
    if (reply.result.status != OWH_OK)
        return reply.result;
    const auto &c = reply.value;
    constexpr uint32_t mask = OWH_CAP_PIXEL_BUFFER | OWH_CAP_D3D11_SHARED_TEXTURE;
    const auto expected_available =
        (c.pixel_buffer_status.status == OWH_OK ? OWH_CAP_PIXEL_BUFFER : 0u) |
        ((c.shared_texture_status.status == OWH_OK || c.shared_texture_status.status == OWH_OPT_IN_REQUIRED)
             ? OWH_CAP_D3D11_SHARED_TEXTURE
             : 0u);
    const auto expected_enabled =
        expected_available &
        (OWH_CAP_PIXEL_BUFFER | (policy_.experimental_features & OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE
                                       ? OWH_CAP_D3D11_SHARED_TEXTURE
                                       : 0u));
    if (c.size != sizeof(c) || c.reserved || c.process_id != pid_ ||
        c.process_started != started_ || c.session_id != id_ || (c.available & ~mask) ||
        c.available != expected_available || c.enabled != expected_enabled ||
        !valid_result(c.pixel_buffer_status) || !valid_result(c.shared_texture_status) ||
        c.maximum_width != core::maximum_width || c.maximum_height != core::maximum_height ||
        c.minimum_slots != core::minimum_slots || c.maximum_slots != core::maximum_slots ||
        c.maximum_pixel_buffer_bytes != core::maximum_image_bytes ||
        c.pixel_buffer_budget_bytes != policy_.pixel_buffer_budget_bytes ||
        c.shared_texture_budget_bytes != policy_.shared_texture_budget_bytes ||
        c.maximum_overlays != policy_.maximum_overlays ||
        c.maximum_receipts_per_overlay != policy_.maximum_receipts_per_overlay) {
        invalidate();
        return make_result(OWH_CORRUPT, OWH_STAGE_CAPABILITY);
    }
    output = c;
    return ok;
}
owh_result AttachedSession::event_capabilities(owh_event_capabilities &output) noexcept {
    const auto reply = events_->capabilities({});
    if (reply.result.status != OWH_OK)
        return reply.result;
    if (reply.value.size != sizeof(reply.value) || reply.value.process_id != pid_ ||
        reply.value.process_started != started_ || reply.value.session_id != id_ ||
        !valid_result(reply.value.status)) {
        invalidate();
        return make_result(OWH_CORRUPT, OWH_STAGE_CAPABILITY);
    }
    output = reply.value;
    return ok;
}
owh_result AttachedSession::ping(uint32_t timeout) noexcept {
    if (timeout == INFINITE)
        return make_result(OWH_INVALID_ARGUMENT);
    return ipc::SessionClient(*calls_, timeout).ping({}).result;
}
owh_result AttachedSession::close() noexcept {
    if (!alive()) {
        invalidate();
        return ok;
    }
    const auto result = session_->detach({}).result;
    invalidate();
    return result;
}
bool AttachedSession::reserve_mapping(uint64_t bytes) noexcept {
    std::lock_guard lock(budget_mutex_);
    const auto maximum = policy_.pixel_buffer_budget_bytes;
    if (bytes > maximum || mapping_bytes_ > maximum - bytes)
        return false;
    mapping_bytes_ += bytes;
    return true;
}
void AttachedSession::release_mapping(uint64_t bytes) noexcept {
    std::lock_guard lock(budget_mutex_);
    mapping_bytes_ -= bytes;
}
} // namespace overwolf_hook::client
