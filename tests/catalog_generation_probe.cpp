// Compile this fixture with a temporary copy of the core catalog where
// events.def contains GeneratedProbe. The CMake target must put that generated
// include tree before the production include roots.
#include <array>
#include <cstddef>
#include <cstring>
#include <owh/core/services.hpp>
#include <owh/ipc/services.hpp>
#include <span>

using namespace overwolf_hook;

static_assert(core::GeneratedProbe::kind == 97);
static_assert(requires(ipc::EventClient &client, const core::Empty &request) {
    client.generated_probe(request);
});

class GeneratedEventService final : public core::EventService {
  public:
#define OWH_METHOD(Symbol, Name, Id, RequestType, ResponseType)                                    \
    core::ResponseType Name(const core::RequestType &) noexcept override {                         \
        core::ResponseType response{};                                                             \
        response.result.status = OWH_OK;                                                           \
        response.result.native_code = 0xCAFE;                                                      \
        return response;                                                                           \
    }
#include <owh/core/events.def>
#undef OWH_METHOD
};

int main() {
    GeneratedEventService service;
    ipc::ServiceRouter router;
    router.bind(service);

    std::array<std::byte, sizeof(core::ResultResponse)> response{};
    uint32_t response_bytes = 0;
    if (!router.dispatch(core::GeneratedProbe::kind, std::span<const std::byte>{}, response,
                         response_bytes))
        return 1;
    if (response_bytes != sizeof(core::ResultResponse))
        return 2;
    core::ResultResponse reply{};
    std::memcpy(&reply, response.data(), sizeof(reply));
    return reply.result.status == OWH_OK && reply.result.native_code == 0xCAFE ? 0 : 3;
}
