#include "support/session_fixture.hpp"
#include <owh/ipc/session_protocol.hpp>

#include <owh/core/services.hpp>
#include <owh/ipc/services.hpp>

#include <array>
#include <cstring>
#include <future>
#include <iostream>
#include <thread>

using namespace overwolf_hook;
struct InvalidNestedWire {
    struct Inner {
        void *address;
    } inner;
};
static_assert(!ipc::wire_safe<InvalidNestedWire>());
static_assert(!ipc::wire_safe<bool>());
static_assert(!ipc::wire_safe<std::string>());
namespace {
owh_result result(owh_status status) noexcept {
    return {status, OWH_STAGE_NONE, OWH_ERROR_NONE, 0, 0};
}

class SessionService final : public core::SessionService {
  public:
    SessionService(const core::PeerIdentity &identity, uint64_t id)
        : identity_(identity), id_(id), options_(core::resolve_options(OWH_CLIENT_OPTIONS_INIT)) {}

    core::HelloResponse hello(const core::HelloRequest &request) noexcept override {
        core::HelloResponse reply{};
        reply.result = result(OWH_INCOMPATIBLE);
        reply.abi_version = OWH_ABI_VERSION;
        reply.wire_protocol = ipc::protocol;
        reply.host_process_id = identity_.host_process_id;
        reply.host_process_started = identity_.host_process_started;
        reply.session_id = id_;
        reply.build_info = identity_.build_info;
        if (request.client_process_id != identity_.client_process_id ||
            request.client_process_started != identity_.client_process_started ||
            request.host_process_started != identity_.host_process_started ||
            request.windows_session_id != identity_.windows_session_id ||
            request.abi_version != OWH_ABI_VERSION ||
            std::memcmp(&request.build_info, &identity_.build_info, sizeof(owh_build_info)))
            return reply;
        options_ = request.options;
        reply.policy = request.options;
        greeted_ = true;
        reply.result = result(OWH_OK);
        return reply;
    }
    core::ResultResponse detach(const core::Empty &) noexcept override {
        if (!greeted_)
            return {result(OWH_AGENT_NOT_READY)};
        detached_ = true;
        return {result(OWH_OK)};
    }
    core::ResultResponse ping(const core::Empty &) noexcept override {
        return {greeted_ && !detached_ ? result(OWH_OK) : result(OWH_CLOSED)};
    }

  private:
    const core::PeerIdentity &identity_;
    uint64_t id_;
    core::SessionPolicy options_{};
    bool greeted_ = false;
    bool detached_ = false;
};

class OverlayService final : public core::OverlayService {
  public:
    core::OverlayCapabilitiesResponse capabilities(const core::Empty &) noexcept override {
        return {};
    }
    core::CreateSourceResponse
    create_pixel_buffer(const core::CreatePixelBufferSourceRequest &) noexcept override {
        return {result(OWH_UNSUPPORTED), {}};
    }
    core::ResultResponse close(const core::SourceRequest &) noexcept override {
        return {result(OWH_OK)};
    }
    core::ResultResponse bind_texture(const core::BindD3D11Request &) noexcept override {
        return {result(OWH_UNSUPPORTED)};
    }
    core::OverlayInfoResponse get_info(const core::SourceRequest &) noexcept override {
        return {result(OWH_NOT_FOUND), {}};
    }
    core::CreateSourceResponse
    create_shared_texture(const core::CreateSharedTextureSourceRequest &) noexcept override {
        return {result(OWH_UNSUPPORTED), {}};
    }
};

class EventService final : public core::EventService {
  public:
    core::EventCapabilitiesResponse capabilities(const core::Empty &) noexcept override {
        return {};
    }
    core::SubscribeEventsResponse
    subscribe(const core::SubscribeEventsRequest &) noexcept override {
        return {result(OWH_UNSUPPORTED), {}, {}};
    }
    core::EventStatisticsResponse statistics(const core::SourceRequest &) noexcept override {
        return {result(OWH_NOT_FOUND), {}};
    }
    core::ResultResponse close(const core::SourceRequest &) noexcept override {
        return {result(OWH_OK)};
    }
};

struct CloseConnections {
    ipc::MessageConnection &first;
    ipc::MessageConnection &second;
    ~CloseConnections() {
        first.close();
        second.close();
    }
};

struct ServedSession {
    SessionService session;
    OverlayService overlays;
    EventService events;
    ipc::ServiceRouter router;
    explicit ServedSession(owh_test::Session &fixture)
        : session(fixture.identity, fixture.session_id) {
        router.bind(session);
        router.bind(overlays);
        router.bind(events);
    }
};

void isolated_sessions() {
    owh_test::Session first(21), second(22);
    first.options.maximum_overlays = 3;
    first.options.pixel_buffer_budget_bytes = 4096;
    first.options.shared_texture_budget_bytes = 8192;
    second.options.maximum_overlays = 7;
    second.options.pixel_buffer_budget_bytes = 16384;
    second.options.shared_texture_budget_bytes = 32768;
    ServedSession a(first), b(second);
    auto a_result = std::async(std::launch::async, [&] {
        return ipc::serve(*first.server, first.identity, first.session_id, a.router);
    });
    auto b_result = std::async(std::launch::async, [&] {
        return ipc::serve(*second.server, second.identity, second.session_id, b.router);
    });
    CloseConnections cleanup{*first.client, *second.client};

    const auto hello_a = owh_test::exchange<core::Hello>(*first.client, first.identity.host_nonce,
                                                         0, 1, owh_test::greeting(first.identity, first.options));
    const auto hello_b = owh_test::exchange<core::Hello>(*second.client, second.identity.host_nonce,
                                                         0, 1, owh_test::greeting(second.identity, second.options));
    owh_test::require(hello_a.result.status == OWH_OK && hello_a.session_id == first.session_id,
                      "first connection gets its own session id");
    owh_test::require(hello_b.result.status == OWH_OK && hello_b.session_id == second.session_id,
                      "second connection attaches independently");
    owh_test::require(hello_a.policy.maximum_overlays == 3 &&
                          hello_b.policy.maximum_overlays == 7 &&
                          hello_a.policy.pixel_buffer_budget_bytes == 4096 &&
                          hello_b.policy.pixel_buffer_budget_bytes == 16384 &&
                          hello_a.policy.shared_texture_budget_bytes == 8192 &&
                          hello_b.policy.shared_texture_budget_bytes == 32768,
                      "accepted immutable policies are isolated without a feature readiness query");
    owh_test::require(owh_test::exchange<core::Detach>(*first.client, first.identity.host_nonce,
                                                       first.session_id, 3)
                              .result.status == OWH_OK,
                      "first session detaches");
    owh_test::require(owh_test::exchange<core::Detach>(*second.client, second.identity.host_nonce,
                                                       second.session_id, 3)
                              .result.status == OWH_OK,
                      "second session detaches");
    owh_test::require(a_result.get().status == OWH_OK && b_result.get().status == OWH_OK,
                      "both typed service sessions exit cleanly");
}

void one_deadline_covers_request_gate_and_reply() {
    owh_test::Session fixture;
    ipc::ServiceConnection connection(*fixture.client, fixture.identity.host_nonce);
    std::promise<void> first_received, second_starting;
    auto second_started = second_starting.get_future();
    auto server = std::async(std::launch::async, [&] {
        const auto receive = [&] {
            std::array<std::byte, ipc::message_capacity> request{};
            uint32_t bytes = 0;
            owh_test::require(fixture.server->receive(request, bytes, 2000) &&
                                  bytes == sizeof(ipc::MessageHeader),
                              "receive bounded ping");
            ipc::MessageHeader header{};
            std::memcpy(&header, request.data(), sizeof(header));
            return header;
        };
        const auto reply = [&](ipc::MessageHeader header) {
            header.kind |= ipc::response_bit;
            header.payload_bytes = sizeof(core::ResultResponse);
            std::array<std::byte, sizeof(header) + sizeof(core::ResultResponse)> packet{};
            const core::ResultResponse response{make_result(OWH_OK)};
            std::memcpy(packet.data(), &header, sizeof(header));
            std::memcpy(packet.data() + sizeof(header), &response, sizeof(response));
            return fixture.server->send(packet, 0);
        };
        const auto first = receive();
        first_received.set_value();
        second_started.wait();
        Sleep(180); // The second call spends part of its deadline at the gate.
        owh_test::require(reply(first), "release first RPC and its request gate");
        const auto second = receive();
        Sleep(240); // Fits 300 ms alone, but not after the gate wait.
        (void)reply(second);
    });
    auto first =
        std::async(std::launch::async, [&] { return connection.call<core::Ping>({}, 2000); });
    first_received.get_future().wait();
    second_starting.set_value();
    const auto second = connection.call<core::Ping>({}, 300);
    owh_test::require(first.get().result.status == OWH_OK, "first RPC completes normally");
    owh_test::require(second.result.status == OWH_TIMEOUT && !connection.alive(),
                      "one RPC deadline spans gate and receive and breaks a timed-out session");
    server.get();
}

void final_reply_survives_endpoint_close() {
    owh_test::Session fixture;
    const std::array<std::byte, 3> expected{std::byte{1}, std::byte{2}, std::byte{3}};
    owh_test::require(fixture.server->send(expected), "publish final response");
    fixture.server->close();
    std::array<std::byte, 3> actual{};
    uint32_t bytes = 0;
    owh_test::require(fixture.client->receive(actual, bytes, 0) && bytes == actual.size() &&
                          actual == expected,
                      "committed final response is drained after peer closes");
    owh_test::require(!fixture.client->receive(actual, bytes, 0) &&
                          GetLastError() == ERROR_BROKEN_PIPE,
                      "closed endpoint has no further responses");
}

void malformed_request_terminates_only_session() {
    owh_test::Session first(31), second(32);
    ServedSession a(first), b(second);
    auto a_result = std::async(std::launch::async, [&] {
        return ipc::serve(*first.server, first.identity, first.session_id, a.router);
    });
    auto b_result = std::async(std::launch::async, [&] {
        return ipc::serve(*second.server, second.identity, second.session_id, b.router);
    });
    CloseConnections cleanup{*first.client, *second.client};
    owh_test::exchange<core::Hello>(*first.client, first.identity.host_nonce, 0, 1,
                                    owh_test::greeting(first.identity, first.options));
    owh_test::exchange<core::Hello>(*second.client, second.identity.host_nonce, 0, 1,
                                    owh_test::greeting(second.identity, second.options));
    const ipc::MessageHeader malformed{ipc::magic,
                                       ipc::protocol,
                                       core::Ping::kind,
                                       sizeof(ipc::MessageHeader),
                                       1,
                                       2,
                                       first.identity.host_nonce,
                                       first.session_id};
    std::array<std::byte, sizeof(ipc::MessageHeader) + 1> packet{};
    std::memcpy(packet.data(), &malformed, sizeof(malformed));
    owh_test::require(first.client->send(packet), "send malformed request");
    owh_test::require(a_result.get().status == OWH_CORRUPT,
                      "malformed request terminates its connection");
    owh_test::require(owh_test::exchange<core::Detach>(*second.client, second.identity.host_nonce,
                                                       second.session_id, 2)
                              .result.status == OWH_OK,
                      "other client remains usable");
    owh_test::require(b_result.get().status == OWH_OK,
                      "unrelated typed session exits independently");
}
} // namespace

int main() {
    try {
        isolated_sessions();
        one_deadline_covers_request_gate_and_reply();
        final_reply_survives_endpoint_close();
        malformed_request_terminates_only_session();
        std::cout
            << "PASS typed service routing, connection isolation and malformed request handling\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
