#include <owh/ipc/session_protocol.hpp>
#pragma once

#include <owh/core/service_contracts.hpp>
#include <owh/ipc/session_transport.hpp>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace owh_test {
using overwolf_hook::core::HelloRequest;
using overwolf_hook::core::PeerIdentity;
using overwolf_hook::ipc::MessageConnection;
using overwolf_hook::ipc::MessageHeader;

inline void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

inline uint64_t process_started(HANDLE process = GetCurrentProcess()) {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE,
            "read process creation time");
    return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

inline uint32_t windows_session(uint32_t pid = GetCurrentProcessId()) {
    DWORD session = 0;
    require(ProcessIdToSessionId(pid, &session) != FALSE, "read Windows session id");
    return session;
}

struct Session {
    HANDLE owner = nullptr;
    uint64_t started = process_started();
    uint64_t session_id = 1;
    PeerIdentity identity{};
    overwolf_hook::core::SessionPolicy options = overwolf_hook::core::resolve_options(OWH_CLIENT_OPTIONS_INIT);
    std::unique_ptr<MessageConnection> client;
    std::unique_ptr<MessageConnection> server;

    explicit Session(uint64_t id = 1) : session_id(id) {
        owner = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                            FALSE, GetCurrentProcessId());
        require(owner != nullptr, "open fixture process");
        client = MessageConnection::create(GetCurrentProcessId(), started);
        require(client != nullptr, "create shared-memory client connection");
        server = MessageConnection::import(client->descriptor(), owner);
        require(server != nullptr, "import shared-memory server connection");



        identity.host_process_id = identity.client_process_id = GetCurrentProcessId();
        identity.host_process_started = identity.client_process_started = started;
        identity.windows_session_id = windows_session();
        identity.host_nonce = client->descriptor().nonce;
        identity.build_info = {"fixture", 42};
    }
    ~Session() {
        if (owner)
            CloseHandle(owner);
    }
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
};

inline HelloRequest greeting(const PeerIdentity &id, const overwolf_hook::core::SessionPolicy &options =
    overwolf_hook::core::resolve_options(OWH_CLIENT_OPTIONS_INIT)) {
    HelloRequest request{};
    request.abi_version = OWH_ABI_VERSION;
    request.client_process_id = id.client_process_id;
    request.windows_session_id = id.windows_session_id;
    request.client_process_started = id.client_process_started;
    request.host_process_started = id.host_process_started;
    request.build_info = id.build_info;
    request.wire_protocol = overwolf_hook::ipc::protocol;
    request.options = options;
    return request;
}

template <class Method>
inline typename Method::Response exchange(MessageConnection &connection, uint64_t nonce,
                                          uint64_t session_id, uint64_t request_id,
                                          const typename Method::Request &request = {}) {
    using namespace overwolf_hook::core;
    using namespace overwolf_hook::ipc;
    const MessageHeader header{magic,
                               protocol,
                               Method::kind,
                               sizeof(MessageHeader),
                               wire_size<typename Method::Request>,
                               request_id,
                               nonce,
                               session_id};
    std::vector<std::byte> packet(sizeof(MessageHeader) + header.payload_bytes);
    std::memcpy(packet.data(), &header, sizeof(header));
    if (header.payload_bytes)
        std::memcpy(packet.data() + sizeof(header), &request, header.payload_bytes);
    require(connection.send(packet), "send typed request");
    std::vector<std::byte> response(overwolf_hook::ipc::message_capacity);
    uint32_t received = 0;
    require(connection.receive(response, received, 5000), "receive typed response");
    require(received >= sizeof(MessageHeader), "response header present");
    MessageHeader reply{};
    std::memcpy(&reply, response.data(), sizeof(reply));
    require(reply.kind == (Method::kind | response_bit) && reply.request_id == request_id &&
                reply.host_nonce == nonce && (!session_id || reply.session_id == session_id) &&
                received == sizeof(MessageHeader) + reply.payload_bytes,
            "response is correlated");
    typename Method::Response value{};
    if (reply.payload_bytes == sizeof(value))
        std::memcpy(&value, response.data() + sizeof(MessageHeader), sizeof(value));
    else if (reply.payload_bytes == sizeof(owh_result))
        std::memcpy(&value, response.data() + sizeof(MessageHeader), sizeof(owh_result));
    else
        throw std::runtime_error("typed response has invalid size");
    return value;
}
} // namespace owh_test
