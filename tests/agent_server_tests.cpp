#include "agent_server.hpp"
#include "events/event_hub.hpp"
#include "overlays/overlay_domain.hpp"
#include <BuildInfo.hpp>
#include <overwolf_hook/abi/events.h>
#include <overwolf_hook/abi/overlays.h>
#include <owh/ipc/pixel_stream.hpp>
#include <owh/ipc/services.hpp>
#include <owh/ipc/session_transport.hpp>
#include <owh/overwolf/event_source.hpp>
#include <owh/overwolf/overlays/lifetime_admission.hpp>
#include <owh/overwolf/overlays/native_contract.hpp>
#include <owh/overwolf/overlays/overlay_host.hpp>
#include <owh/overwolf/signatures/resolver.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/identity.hpp>
#include <owh/platform/result.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <cstring>

namespace overwolf_hook::overwolf {
struct AdmissionFixture {
    static void initialize(LifetimeAdmission &admission,
                           const overwolf_hook::overwolf::ResolvedProfile &profile) {
        admission.allow_hot_attach();
        admission.profile_ = profile;
        admission.installed_.store(true, std::memory_order_release);
    }
};
} // namespace overwolf_hook::overwolf

using namespace overwolf_hook;

namespace {
using namespace overwolf_hook::overwolf;
using namespace overwolf_hook::ipc;
using overwolf_hook::overwolf::native_contract::rect32;

struct FakeOverwolf {
    alignas(16) std::array<std::byte, 0x400> bytes{};
    std::array<uint64_t, 5> vtable{};
    uint64_t root_slot = 0;
    std::array<std::byte, 0x120> connector{};
    uint64_t connector_pointer = 0;
    CRITICAL_SECTION *registry = nullptr;
    Handle cleanup_entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle cleanup_release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> hold_cleanup{true};
    static inline FakeOverwolf *active = nullptr;

    FakeOverwolf() {
        assert(cleanup_entered && cleanup_release);
        auto *root = bytes.data();
        *reinterpret_cast<uint64_t *>(root) = reinterpret_cast<uint64_t>(vtable.data());
        vtable[2] = reinterpret_cast<uint64_t>(&agent_init);
        vtable[4] = reinterpret_cast<uint64_t>(&agent_clear);
        *reinterpret_cast<uint64_t *>(root + 0x2b8) = 1;
        root_slot = reinterpret_cast<uint64_t>(root);
        registry = reinterpret_cast<CRITICAL_SECTION *>(root + 0x1e8 + 8);
        InitializeCriticalSection(registry);
        *reinterpret_cast<uint64_t *>(root + 0x88) = reinterpret_cast<uint64_t>(&connector_pointer);
        *reinterpret_cast<uint32_t *>(root + 0x150) = 0;
        active = this;
    }
    ~FakeOverwolf() {
        active = nullptr;
        DeleteCriticalSection(registry);
    }

    bool registry_owned() const noexcept {
        return registry &&
               registry->OwningThread ==
                   reinterpret_cast<HANDLE>(static_cast<uintptr_t>(GetCurrentThreadId())) &&
               registry->RecursionCount != 0;
    }
    static void __cdecl agent_init() {}
    static void __cdecl agent_clear() {}
    static void __cdecl lifecycle_run(uint32_t) {}
    static void __cdecl lifecycle_clear() {}
    static void __cdecl lifecycle_unhook() {}
    static bool __cdecl register_window(void *, void *raw_window) {
        auto *self = active;
        if (!self || !self->registry_owned())
            return false;
        *reinterpret_cast<uint64_t *>(self->connector.data() + 0x48) =
            reinterpret_cast<uint64_t>(raw_window);
        self->connector_pointer = reinterpret_cast<uint64_t>(self->connector.data());
        *reinterpret_cast<uint32_t *>(self->bytes.data() + 0x150) = 1;
        return true;
    }
    static void __cdecl refresh_windows(void *, bool, bool) {
        auto *self = active;
        if (!self || !self->registry_owned())
            return;
        if (self->hold_cleanup.exchange(false, std::memory_order_acq_rel)) {
            SetEvent(self->cleanup_entered.get());
            (void)WaitForSingleObject(self->cleanup_release.get(), 10000);
        }
        const HWND window = reinterpret_cast<HWND>(
            *reinterpret_cast<const uint64_t *>(self->connector.data() + 0x48));
        if (!IsWindow(window)) {
            self->connector_pointer = 0;
            *reinterpret_cast<uint32_t *>(self->bytes.data() + 0x150) = 0;
        }
    }
    static bool __cdecl set_position(void *, int32_t, int32_t) { return true; }
    static uint64_t __cdecl paint(void *window, const void *, int32_t, int32_t, const rect32 *,
                                  bool, bool, uint64_t *output) {
        auto *self = active;
        if (!self || !self->registry_owned() || !IsWindow(static_cast<HWND>(window)))
            return 0;
        if (output)
            *output = 1;
        return 1;
    }

    overwolf_hook::overwolf::ResolvedProfile profile() const noexcept {
        overwolf_hook::overwolf::ResolvedProfile p{};
        p.common = p.pixel_buffer_source = p.lifecycle = true;
        p.agent_global_slot = reinterpret_cast<uint64_t>(&root_slot);
        p.agent_init = reinterpret_cast<uint64_t>(&agent_init);

        p.agent_clear = reinterpret_cast<uint64_t>(&agent_clear);

        p.run_agent = reinterpret_cast<uint64_t>(&lifecycle_run);

        p.clear_agent = reinterpret_cast<uint64_t>(&lifecycle_clear);

        p.unhook_agent = reinterpret_cast<uint64_t>(&lifecycle_unhook);

        p.source_paint = reinterpret_cast<uint64_t>(&paint);
        p.register_window = reinterpret_cast<uint64_t>(&register_window);
        p.refresh_windows = reinterpret_cast<uint64_t>(&refresh_windows);
        p.set_position = reinterpret_cast<uint64_t>(&set_position);
        return p;
    }
};

uint64_t current_process_started() {
    FILETIME created{}, exited{}, kernel{}, user{};
    assert(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user));
    return (uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
}

bool wait_until(const auto &predicate, DWORD timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (!predicate() && GetTickCount64() < deadline)
        SwitchToThread();
    return predicate();
}

void root_retires_resources_during_async_cleanup(agent::OverlayDomain &overlays) {
    auto endpoint = ipc::AgentEndpoint::open(GetCurrentProcessId(), current_process_started());
    auto transport =
        ipc::MessageConnection::create(GetCurrentProcessId(), current_process_started());
    assert(endpoint && transport && endpoint->submit(transport->descriptor()));

    ipc::ServiceConnection calls(*transport, transport->descriptor().nonce);
    DWORD windows_session = 0;
    assert(ProcessIdToSessionId(GetCurrentProcessId(), &windows_session));
    const auto options = core::resolve_options(OWH_CLIENT_OPTIONS_INIT);
    core::HelloRequest hello{};
    hello.abi_version = OWH_ABI_VERSION;
    hello.client_process_id = GetCurrentProcessId();
    hello.windows_session_id = windows_session;
    hello.client_process_started = current_process_started();
    hello.host_process_started = current_process_started();
    hello.wire_protocol = ipc::protocol;
    hello.build_info = build_info::value;
    hello.options = options;
    // A rejected handshake terminates that connection. Test it on a separate
    // peer rather than attempting to reuse an incompatible session.
    {
        auto incompatible = ipc::MessageConnection::create(GetCurrentProcessId(),
                                                          current_process_started());
        assert(incompatible && endpoint->submit(incompatible->descriptor()));
        ipc::ServiceConnection rejected(*incompatible, incompatible->descriptor().nonce);
        auto mismatched_build = hello;
        mismatched_build.build_info.build_number ^= 1;
        assert(rejected.call<core::Hello>(mismatched_build).result.status == OWH_INCOMPATIBLE);
    }
    const auto greeted = calls.call<core::Hello>(hello);
    assert(greeted.result.status == OWH_OK && greeted.session_id);
    calls.establish(greeted.session_id);
    assert(calls.call<core::GetOverlayCapabilities>().result.status == OWH_OK);

    const core::ResourceRef ref{greeted.session_id, 1};
    const auto started = current_process_started();
    const auto queue = core::resolve_queue(2, OWH_QUEUE_FIFO);
    auto [producer_result, producer] = PixelProducer::create(
        ref, GetCurrentProcessId(), started, 2, 2, queue.slot_count, queue.policy, 1 << 20);
    assert(producer_result.status == OWH_OK && producer);
    core::Geometry geometry{};
    geometry.size = sizeof(geometry);
    geometry.width = 2;
    geometry.height = 2;
    // The service itself must reject a raw shared-texture request without session opt-in;
    // this bypasses the public Client's earlier convenience check.
    const auto denied_texture = calls.call<core::CreateSharedTextureSource>({ref, geometry});
    assert(denied_texture.result.status == OWH_OPT_IN_REQUIRED);
    const auto created =
        calls.call<core::CreatePixelBufferSource>({ref, geometry, producer->descriptor()});
    assert(created.result.status == OWH_OK && created.ref == ref);
    assert(overlays.child_count(ref.session_id) == 1);

    owh_sdk *second_client = nullptr;
    assert(owh_sdk_open(&second_client).status == OWH_OK && second_client);
    owh_client_options target = OWH_CLIENT_OPTIONS_INIT;
    target.attach_mode = OWH_ATTACH_HOT;
    target.process_id = GetCurrentProcessId();
    target.process_started = started;
    assert(owh_sdk_attach(second_client, &target).status == OWH_OK);
    assert(owh_sdk_ping(second_client, 1000).status == OWH_OK);

    // A dead control peer drives the real AgentServer worker-exit path. Keep
    // native source removal blocked until the root has had time to schedule and
    // retry resource retirement by session ID; the runtime record must remain owning meanwhile.
    transport->close();
    auto *fake = FakeOverwolf::active;
    assert(fake && WaitForSingleObject(fake->cleanup_entered.get(), 5000) == WAIT_OBJECT_0);
    assert(overlays.child_count(ref.session_id) == 1);
    assert(owh_sdk_ping(second_client, 1000).status == OWH_OK);

    // A new HELLO and either domain probe do not queue behind native cleanup.
    owh_sdk *third_client = nullptr;
    assert(owh_sdk_open(&third_client).status == OWH_OK);
    assert(owh_sdk_attach(third_client, &target).status == OWH_OK);
    owh_event_capabilities events{};
    events.size = sizeof(events);
    assert(owh_event_query_capabilities(third_client, &events).status == OWH_OK);
    owh_overlay_capabilities overlay_caps{};
    overlay_caps.size = sizeof(overlay_caps);
    assert(owh_overlay_query_capabilities(third_client, &overlay_caps).status == OWH_OK);
    assert(owh_sdk_ping(third_client, 1000).status == OWH_OK);
    assert(owh_sdk_detach(third_client).status == OWH_OK);
    owh_sdk_close(third_client);

    SetEvent(fake->cleanup_release.get());
    assert(wait_until([&] { return overlays.child_count(ref.session_id) == 0; }, 5000));
    assert(owh_sdk_ping(second_client, 1000).status == OWH_OK);
    assert(owh_sdk_detach(second_client).status == OWH_OK);
    owh_sdk_close(second_client);
}
} // namespace

int main() {
    owh_sdk *sdk = nullptr;
    assert(owh_sdk_open(&sdk).status == OWH_OK && sdk);
    owh_client_options target = OWH_CLIENT_OPTIONS_INIT;
    target.attach_mode = OWH_ATTACH_HOT;
    target.process_id = GetCurrentProcessId();
    target.process_started = platform::process_start(GetCurrentProcess());
    owh_agent_info query{};
    query.size = sizeof(query);
    assert(owh_sdk_probe_agent(&target, &query).status == OWH_NOT_FOUND);

    {
        owh_agent_info other{};
        other.size = sizeof(other);
        other.abi_version = 999;
        other.wire_version = 998;
        other.server_state = OWH_AGENT_READY;
        other.instance_id = platform::random_id();
        other.process_id = target.process_id;
        other.process_started = target.process_started;
        auto endpoint = ipc::AgentEndpoint::create(other);
        query = {};
        query.size = sizeof(query);
        assert(owh_sdk_probe_agent(&target, &query).status == OWH_OK &&
               query.abi_version == 999);
        assert(owh_sdk_attach(sdk, &target).status == OWH_INCOMPATIBLE);
    }
    {
        owh_agent_info info{};
        info.size = sizeof(info);
        info.abi_version = OWH_ABI_VERSION;
        info.wire_version = ipc::protocol;
        info.server_state = OWH_AGENT_READY;
        info.process_id = target.process_id;
        info.process_started = target.process_started;
        info.instance_id = platform::random_id();
        auto endpoint = ipc::AgentEndpoint::create(info);
        auto connection = ipc::MessageConnection::create(info.process_id, info.process_started);
        endpoint->set_admission_available(false);
        assert(!endpoint->submit(connection->descriptor(), 0));
        assert(GetLastError() == ERROR_BUSY);
        endpoint->set_admission_available(true);
        assert(endpoint->submit(connection->descriptor(), 0));
        ipc::ConnectionDescriptor submitted{};
        assert(endpoint->accept(submitted));
        assert(submitted.nonce == connection->descriptor().nonce);
    }

    overwolf::LifetimeAdmission admission;
    FakeOverwolf fake;
    overwolf::AdmissionFixture::initialize(admission, fake.profile());
    overwolf::OverlayHost overlay_host(admission);
    agent::OverlayDomain overlays(overlay_host);
    ::overwolf_hook::overwolf::EventSource event_source;
    agent::events::EventHub events(event_source);
    agent::AgentServer server(overlays, events);
    root_retires_resources_during_async_cleanup(overlays);
    query = {};
    query.size = sizeof(query);
    assert(owh_sdk_probe_agent(&target, &query).status == OWH_OK &&
           query.abi_version == OWH_ABI_VERSION);
    const auto instance = query.instance_id;

    std::array<owh_sdk *, ipc::admission_capacity> clients{};
    for (auto &client : clients) {
        assert(owh_sdk_open(&client).status == OWH_OK && client);
        assert(owh_sdk_attach(client, &target).status == OWH_OK);
        assert(owh_sdk_ping(client, 1000).status == OWH_OK);
    }
    auto endpoint = ipc::AgentEndpoint::open(target.process_id, target.process_started);
    auto rejected = ipc::MessageConnection::create(target.process_id, target.process_started);
    assert(!endpoint->submit(rejected->descriptor(), 0));
    assert(GetLastError() == ERROR_BUSY);

    for (auto *client : clients) {
        assert(owh_sdk_detach(client).status == OWH_OK);
        owh_sdk_close(client);
    }
    query = {};
    query.size = sizeof(query);
    assert(owh_sdk_probe_agent(&target, &query).status == OWH_OK &&
           query.instance_id == instance);
    owh_sdk_close(sdk);
}
