#pragma once
#include <array>
#include <cstring>
#include <mutex>
#include <owh/core/services.hpp>
#include <owh/ipc/session_protocol.hpp>
#include <owh/ipc/session_transport.hpp>
#include <owh/ipc/wire_contract.hpp>
#include <owh/platform/result.hpp>
#include <span>

namespace overwolf_hook::ipc {
/** One connection-wide request gate. Feature proxies know no wire framing. */
class ServiceConnection final {
  public:
    ServiceConnection(MessageConnection &connection, uint64_t nonce, uint64_t session = 0)
        : connection_(connection), nonce_(nonce), session_(session) {}
    template <class Method>
    typename Method::Response call(const typename Method::Request &request = {},
                                   uint32_t timeout_ms = 5000) noexcept {
        static_assert(wire_safe<typename Method::Request>() &&
                          wire_safe<typename Method::Response>(),
                      "Service values must contain only fixed-width values and nested PODs");
        static_assert(core::wire_size<typename Method::Request> <= ipc::maximum_payload &&
                      sizeof(typename Method::Response) <= ipc::maximum_payload);
        typename Method::Response reply{};
        auto result = exchange(Method::kind, &request, core::wire_size<typename Method::Request>,
                               &reply, sizeof(reply), timeout_ms);
        if (result.status != OWH_OK)
            reply.result = result;
        return reply;
    }
    void establish(uint64_t session) noexcept { session_ = session; }
    bool alive() const noexcept { return connection_.alive(); }
    void close() noexcept { connection_.close(); }

  private:
    owh_result exchange(uint16_t method, const void *, uint32_t, void *, uint32_t,
                        uint32_t) noexcept;
    MessageConnection &connection_;
    uint64_t nonce_, session_, sequence_ = 1;
    std::timed_mutex gate_;
};

#define OWH_METHOD(Symbol, Name, Id, RequestType, ResponseType)                                    \
    core::ResponseType Name(const core::RequestType &request) noexcept override {                  \
        return connection_.call<core::Symbol>(request, timeout_ms_);                               \
    }
class OverlayClient final : public core::OverlayService {
  public:
    explicit OverlayClient(ServiceConnection &connection, uint32_t timeout_ms = 5000)
        : connection_(connection), timeout_ms_(timeout_ms) {}
#include <owh/core/overlays.def>
  private:
    ServiceConnection &connection_;
    uint32_t timeout_ms_;
};
class EventClient final : public core::EventService {
  public:
    explicit EventClient(ServiceConnection &connection, uint32_t timeout_ms = 5000)
        : connection_(connection), timeout_ms_(timeout_ms) {}
#include <owh/core/events.def>
  private:
    ServiceConnection &connection_;
    uint32_t timeout_ms_;
};
class SessionClient final : public core::SessionService {
  public:
    explicit SessionClient(ServiceConnection &connection, uint32_t timeout_ms = 5000)
        : connection_(connection), timeout_ms_(timeout_ms) {}
#include <owh/core/session.def>
  private:
    ServiceConnection &connection_;
    uint32_t timeout_ms_;
};
#undef OWH_METHOD

/** Closed service catalog. Binding a service populates typed handlers; no
 * feature switch, codec table, or response-size list is maintained separately. */
class ServiceRouter final {
  public:
    void bind(core::SessionService &service) noexcept {
#define OWH_METHOD(Symbol, Name, Id, RequestType, ResponseType)                                    \
    add<core::Symbol, core::SessionService, &core::SessionService::Name>(service);
#include <owh/core/session.def>
#undef OWH_METHOD
    }
    void bind(core::OverlayService &service) noexcept {
#define OWH_METHOD(Symbol, Name, Id, RequestType, ResponseType)                                    \
    add<core::Symbol, core::OverlayService, &core::OverlayService::Name>(service);
#include <owh/core/overlays.def>
#undef OWH_METHOD
    }
    void bind(core::EventService &service) noexcept {
#define OWH_METHOD(Symbol, Name, Id, RequestType, ResponseType)                                    \
    add<core::Symbol, core::EventService, &core::EventService::Name>(service);
#include <owh/core/events.def>
#undef OWH_METHOD
    }
    bool dispatch(uint16_t method, std::span<const std::byte> request, std::span<std::byte> reply,
                  uint32_t &bytes) const noexcept;

  private:
    struct Entry {
        void *service = nullptr;
        bool (*invoke)(void *, std::span<const std::byte>, std::span<std::byte>,
                       uint32_t &) noexcept = nullptr;
    };
    template <class Method, class Service, auto Invoke> void add(Service &service) noexcept {
        static_assert(wire_safe<typename Method::Request>() &&
                      wire_safe<typename Method::Response>());
        entries_[Method::kind] = {
            &service, [](void *object, std::span<const std::byte> input,
                         std::span<std::byte> output, uint32_t &bytes) noexcept {
                if (input.size() != core::wire_size<typename Method::Request> ||
                    output.size() < sizeof(typename Method::Response))
                    return false;
                typename Method::Request request{};
                if constexpr (core::wire_size<typename Method::Request>)
                    std::memcpy(&request, input.data(), sizeof(request));
                const auto reply = (static_cast<Service *>(object)->*Invoke)(request);
                std::memcpy(output.data(), &reply, sizeof(reply));
                bytes = sizeof(reply);
                return true;
            }};
    }
    static constexpr uint16_t method_ids[]{
#define OWH_METHOD(Symbol, Name, Id, RequestType, ResponseType) Id,
#include <owh/core/events.def>
#include <owh/core/overlays.def>
#include <owh/core/session.def>
#undef OWH_METHOD
    };
    static constexpr size_t count = [] {
        size_t maximum = 0;
        for (auto id : method_ids)
            if (id > maximum)
                maximum = id;
        return maximum + 1;
    }();
    static_assert(
        [] {
            for (size_t i = 0; i < std::size(method_ids); ++i) {
                if (!method_ids[i] || method_ids[i] & ipc::response_bit)
                    return false;
                for (size_t j = 0; j < i; ++j)
                    if (method_ids[i] == method_ids[j])
                        return false;
            }
            return true;
        }(),
        "Service method IDs must be unique request IDs");
    std::array<Entry, count> entries_{};
};

owh_result serve(MessageConnection &, const core::PeerIdentity &, uint64_t session,
                 ServiceRouter &) noexcept;
} // namespace overwolf_hook::ipc
