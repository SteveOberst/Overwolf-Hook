#include <algorithm>
#include <chrono>
#include <owh/ipc/services.hpp>
#include <owh/ipc/session_protocol.hpp>
#include <owh/platform/deadline.hpp>

namespace overwolf_hook::ipc {
namespace {
owh_result transport_error(DWORD error) noexcept {
    return make_result(error == ERROR_TIMEOUT ? OWH_TIMEOUT : OWH_AGENT_LOST, OWH_STAGE_CONTROL_IO,
                       OWH_ERROR_WIN32, error);
}
} // namespace
owh_result ServiceConnection::exchange(uint16_t method, const void *request, uint32_t request_bytes,
                                       void *response, uint32_t response_bytes,
                                       uint32_t timeout) noexcept {
    const Deadline deadline(timeout);
    std::unique_lock lock(gate_, std::defer_lock);
    if (!lock.try_lock_for(std::chrono::milliseconds(timeout)))
        return make_result(OWH_TIMEOUT, OWH_STAGE_CONTROL_IO);
    if (!connection_.alive())
        return make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME);
    if (request_bytes > ipc::maximum_payload || response_bytes > ipc::maximum_payload || !sequence_)
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_CONTROL_IO);
    std::array<std::byte, message_capacity> packet{};
    const ipc::MessageHeader header{
        ipc::magic,    ipc::protocol, method, sizeof(ipc::MessageHeader),
        request_bytes, sequence_++,   nonce_, session_};
    std::memcpy(packet.data(), &header, sizeof(header));
    if (request_bytes)
        std::memcpy(packet.data() + sizeof(header), request, request_bytes);
    if (!connection_.send({packet.data(), sizeof(header) + request_bytes},
                          deadline.remaining_ms())) {
        const auto error = GetLastError();
        connection_.close();
        return transport_error(error);
    }
    uint32_t bytes = 0;
    if (!connection_.receive(packet, bytes, deadline.remaining_ms())) {
        const auto error = GetLastError();
        connection_.close();
        return transport_error(error);
    }
    ipc::MessageHeader received{};
    if (bytes >= sizeof(received))
        std::memcpy(&received, packet.data(), sizeof(received));
    if (received.magic != ipc::magic || received.protocol != ipc::protocol ||
        received.kind != (method | ipc::response_bit) ||
        received.header_bytes != sizeof(received) ||
        received.payload_bytes != bytes - std::min<uint32_t>(bytes, sizeof(received)) ||
        received.request_id != header.request_id || received.host_nonce != nonce_ ||
        (session_ && received.session_id != session_) ||
        received.payload_bytes < sizeof(owh_result)) {
        connection_.close();
        return make_result(OWH_CORRUPT, OWH_STAGE_CONTROL_IO);
    }
    owh_result outcome{};
    std::memcpy(&outcome, packet.data() + sizeof(received), sizeof(outcome));
    if (received.payload_bytes != response_bytes) {
        if (received.payload_bytes == sizeof(owh_result) && outcome.status != OWH_OK)
            return outcome;
        connection_.close();
        return make_result(OWH_CORRUPT, OWH_STAGE_CONTROL_IO);
    }
    std::memcpy(response, packet.data() + sizeof(received), response_bytes);
    return ok;
}

bool ServiceRouter::dispatch(uint16_t method, std::span<const std::byte> request,
                             std::span<std::byte> reply, uint32_t &bytes) const noexcept {
    bytes = 0;
    if (method < entries_.size() && entries_[method].invoke)
        return entries_[method].invoke(entries_[method].service, request, reply, bytes);
    const auto result = make_result(OWH_UNSUPPORTED, OWH_STAGE_CONTROL_IO);
    if (reply.size() < sizeof(result))
        return false;
    std::memcpy(reply.data(), &result, sizeof(result));
    bytes = sizeof(result);
    return true;
}

owh_result serve(MessageConnection &connection, const core::PeerIdentity &identity,
                 uint64_t session, ServiceRouter &router) noexcept {
    uint64_t last_sequence = 0;
    bool greeted = false;
    std::array<std::byte, message_capacity> packet{}, response{};
    for (;;) {
        uint32_t bytes = 0;
        if (!connection.receive(packet, bytes))
            return make_result(OWH_AGENT_LOST, OWH_STAGE_CONTROL_IO);
        ipc::MessageHeader request{};
        if (bytes < sizeof(request))
            return make_result(OWH_CORRUPT, OWH_STAGE_CONTROL_IO);
        std::memcpy(&request, packet.data(), sizeof(request));
        if (request.magic != ipc::magic || request.protocol != ipc::protocol ||
            request.header_bytes != sizeof(request) ||
            request.payload_bytes > ipc::maximum_payload ||
            bytes != sizeof(request) + request.payload_bytes ||
            request.request_id <= last_sequence || request.host_nonce != identity.host_nonce ||
            request.session_id != (greeted ? session : 0) ||
            (!greeted && request.kind != core::Hello::kind))
            return make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE);
        last_sequence = request.request_id;
        uint32_t payload = 0;
        if (!router.dispatch(request.kind, {packet.data() + sizeof(request), request.payload_bytes},
                             {response.data() + sizeof(ipc::MessageHeader), ipc::maximum_payload},
                             payload))
            return make_result(OWH_CORRUPT, OWH_STAGE_CONTROL_IO);
        const ipc::MessageHeader header{ipc::magic,
                                        ipc::protocol,
                                        static_cast<uint16_t>(request.kind | ipc::response_bit),
                                        sizeof(ipc::MessageHeader),
                                        payload,
                                        request.request_id,
                                        identity.host_nonce,
                                        session};
        std::memcpy(response.data(), &header, sizeof(header));
        if (!connection.send({response.data(), sizeof(header) + payload}))
            return make_result(OWH_AGENT_LOST, OWH_STAGE_CONTROL_IO);
        owh_result outcome{};
        std::memcpy(&outcome, response.data() + sizeof(header), sizeof(outcome));
        if (!greeted) {
            if (outcome.status != OWH_OK)
                return outcome;
            greeted = true;
        } else if (request.kind == core::Detach::kind && outcome.status == OWH_OK)
            return outcome;
    }
}
} // namespace overwolf_hook::ipc
