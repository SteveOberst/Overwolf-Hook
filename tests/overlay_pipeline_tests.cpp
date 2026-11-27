#include "agent_root.hpp"
#include "agent_session.hpp"
#include "events/event_hub.hpp"
#include "support/session_fixture.hpp"
#include <overwolf_hook/abi/overlays.h>
#include <owh/ipc/services.hpp>

#include "overlays/overlay_domain.hpp"
#include <owh/ipc/pixel_stream.hpp>
#include <owh/overwolf/overlays/lifetime_admission.hpp>
#include <owh/overwolf/overlays/native_contract.hpp>
#include <owh/overwolf/overlays/overlay_executor.hpp>
#include <owh/overwolf/overlays/overlay_host.hpp>
#include <owh/testing/client_fixture.hpp>

#include <d3d11.h>
#include <dxgi.h>
#include <overwolf_hook/abi/events.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

using namespace overwolf_hook;
using namespace overwolf_hook::agent;
using namespace overwolf_hook::overwolf;
using Microsoft::WRL::ComPtr;

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

namespace {
void check(bool value, const char *label, int line) {
    if (!value) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, label);
        std::fflush(stderr);
        std::_Exit(2);
    }
    std::printf("PASS %s\n", label);
}
#define CHECK(x) check((x), #x, __LINE__)

bool await_pending(OverlayExecutor &executor, size_t expected) {
    const ULONGLONG deadline = GetTickCount64() + 3000;
    while (executor.pending_commands() < expected && GetTickCount64() < deadline)
        SwitchToThread();
    return executor.pending_commands() >= expected;
}

void executor_failure_cases() {
    OverlayExecutor commands;
    CHECK(commands.ensure_started({[] { return true; }, {}, {}, {}, {}}));
    bool command_failed = false;
    try {
        commands.invoke([&] {
            if (commands.running())
                throw std::runtime_error("owned command failure");
        });
    } catch (const std::runtime_error &) {
        command_failed = true;
    }
    bool later_command_ran = false;
    commands.invoke([&] { later_command_ran = true; });
    CHECK(command_failed && later_command_ran && commands.running());
    commands.shutdown();
    CHECK(commands.state() == OverlayExecutor::State::stopped);

    Handle callback_entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle callback_release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    CHECK(callback_entered && callback_release);
    std::atomic<bool> throw_from_source{false};
    OverlayExecutor failed;
    CHECK(failed.ensure_started({[] { return true; },
                                 {},
                                 [&] {
                                     if (!throw_from_source.load(std::memory_order_acquire))
                                         return;
                                     SetEvent(callback_entered.get());
                                     WaitForSingleObject(callback_release.get(), INFINITE);
                                     throw std::runtime_error("owned source callback failure");
                                 },
                                 {},
                                 {}}));
    throw_from_source.store(true, std::memory_order_release);
    failed.signal_sources();
    const bool entered = WaitForSingleObject(callback_entered.get(), 3000) == WAIT_OBJECT_0;
    std::atomic<bool> failed_command_ran{false};
    auto pending = std::async(std::launch::async, [&] {
        try {
            failed.invoke([&] { failed_command_ran.store(true, std::memory_order_release); });
            return false;
        } catch (...) {
            return true;
        }
    });
    const bool callback_command_queued = await_pending(failed, 1);
    SetEvent(callback_release.get());
    const bool rejected =
        pending.wait_for(std::chrono::seconds(3)) == std::future_status::ready && pending.get();
    failed.shutdown();
    CHECK(entered && callback_command_queued && rejected &&
          !failed_command_ran.load(std::memory_order_acquire) &&
          failed.state() == OverlayExecutor::State::failed);

    Handle shutdown_entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle shutdown_release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    CHECK(shutdown_entered && shutdown_release);
    std::atomic<bool> block_source{false};
    OverlayExecutor stopped;
    CHECK(stopped.ensure_started({[] { return true; },
                                  {},
                                  [&] {
                                      if (!block_source.load(std::memory_order_acquire))
                                          return;
                                      SetEvent(shutdown_entered.get());
                                      WaitForSingleObject(shutdown_release.get(), INFINITE);
                                  },
                                  {},
                                  {}}));
    block_source.store(true, std::memory_order_release);
    stopped.signal_sources();
    const bool shutdown_callback_entered =
        WaitForSingleObject(shutdown_entered.get(), 3000) == WAIT_OBJECT_0;
    auto queued = std::async(std::launch::async, [&] {
        try {
            stopped.invoke([] {});
            return false;
        } catch (...) {
            return true;
        }
    });
    const bool shutdown_command_queued = await_pending(stopped, 1);
    auto shutting_down = std::async(std::launch::async, [&] { stopped.shutdown(); });
    const ULONGLONG stopping_deadline = GetTickCount64() + 3000;
    while (stopped.state() == OverlayExecutor::State::running &&
           GetTickCount64() < stopping_deadline)
        SwitchToThread();
    const bool shutdown_requested = stopped.state() == OverlayExecutor::State::stopping;
    SetEvent(shutdown_release.get());
    shutting_down.get();
    const bool shutdown_rejected =
        queued.wait_for(std::chrono::seconds(3)) == std::future_status::ready && queued.get();
    CHECK(shutdown_callback_entered && shutdown_command_queued && shutdown_requested &&
          shutdown_rejected && stopped.state() == OverlayExecutor::State::stopped);
}

uint64_t process_started(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    CHECK(GetProcessTimes(process, &created, &exited, &kernel, &user));
    return (uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime;
}

// This is an owned, synthetic OWAgent-shaped object. Offsets match the reviewed
// profile so OverlayHost exercises its real admission, registry and witness
// code. The callbacks below are test-owned source functions, never target code.
struct FakeAgent {
    alignas(16) std::array<std::byte, 0x400> bytes{};
    std::array<uint64_t, 5> vtable{};
    uint64_t root_slot = 0;
    std::array<std::array<std::byte, 0x120>, 4> connectors{};
    std::array<uint64_t, 4> connector_ptrs{};
    CRITICAL_SECTION *registry = nullptr;
    std::atomic<uint32_t> pixel_buffer_paints{0};
    std::atomic<uint32_t> shared_texture_paints{0};
    std::atomic<bool> pixels_valid{false};
    std::atomic<bool> registry_held{false};
    std::atomic<bool> ignore_texture_token{false};
    std::atomic<bool> deny_refresh{false};
    std::atomic<bool> pause_next_paint{false};
    native_contract::rect32 last_damage{}; // Read only after the publication receipt completes.
    HANDLE paint_entered = nullptr;
    HANDLE paint_release = nullptr;

    bool registry_owned() const noexcept {
        return registry &&
               registry->OwningThread ==
                   reinterpret_cast<HANDLE>(static_cast<uintptr_t>(GetCurrentThreadId())) &&
               registry->RecursionCount != 0;
    }

    FakeAgent() {
        paint_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        paint_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        CHECK(paint_entered && paint_release);
        auto *root = bytes.data();
        *reinterpret_cast<uint64_t *>(root) = reinterpret_cast<uint64_t>(vtable.data());
        vtable[2] = reinterpret_cast<uint64_t>(&agent_init);
        vtable[4] = reinterpret_cast<uint64_t>(&agent_clear);
        *reinterpret_cast<uint64_t *>(root + 0x2b8) = 1;
        root_slot = reinterpret_cast<uint64_t>(root);
        registry = reinterpret_cast<CRITICAL_SECTION *>(root + 0x1e8 + 8);
        InitializeCriticalSection(registry);
        *reinterpret_cast<uint64_t *>(root + 0x88) =
            reinterpret_cast<uint64_t>(connector_ptrs.data());
        *reinterpret_cast<uint32_t *>(root + 0x150) = 0;
    }
    ~FakeAgent() {
        DeleteCriticalSection(registry);
        if (paint_entered)
            CloseHandle(paint_entered);
        if (paint_release)
            CloseHandle(paint_release);
    }

    static void __cdecl agent_init() {}
    static void __cdecl agent_clear() {}
    static void __cdecl lifecycle_run(uint32_t) {}
    static void __cdecl lifecycle_clear() {}
    static void __cdecl lifecycle_unhook() {}

    static FakeAgent *active;
    static bool __cdecl register_window(void *, void *hwnd) {
        auto *self = active;
        if (!self || !self->registry_owned())
            return false;
        const uint32_t count = *reinterpret_cast<uint32_t *>(self->bytes.data() + 0x150);
        if (count >= self->connector_ptrs.size())
            return false;
        auto &connector = self->connectors[count];
        *reinterpret_cast<uint64_t *>(connector.data() + 0x48) = reinterpret_cast<uint64_t>(hwnd);
        self->connector_ptrs[count] = reinterpret_cast<uint64_t>(connector.data());
        *reinterpret_cast<uint32_t *>(self->bytes.data() + 0x150) = count + 1;
        return true;
    }
    static void __cdecl refresh_windows(void *, bool, bool) {
        auto *self = active;
        if (!self || self->deny_refresh.load() || !self->registry_owned())
            return;
        uint32_t write = 0;
        const uint32_t count = *reinterpret_cast<uint32_t *>(self->bytes.data() + 0x150);
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t pointer = self->connector_ptrs[i];
            const HWND hwnd = reinterpret_cast<HWND>(*reinterpret_cast<const uint64_t *>(
                reinterpret_cast<const std::byte *>(pointer) + 0x48));
            if (IsWindow(hwnd))
                self->connector_ptrs[write++] = pointer;
        }
        for (uint32_t i = write; i < count; ++i)
            self->connector_ptrs[i] = 0;
        *reinterpret_cast<uint32_t *>(self->bytes.data() + 0x150) = write;
    }
    static bool __cdecl set_position(void *, int32_t, int32_t) { return true; }
    static uint64_t __cdecl paint(void *hwnd, const void *bgra, int32_t width, int32_t height,
                                  const native_contract::rect32 *damage, bool first_flag, bool second_flag,
                                  uint64_t *opaque_output) {
        auto *self = active;
        if (!self || !self->registry_owned() || !IsWindow(static_cast<HWND>(hwnd)))
            return 0;
        const auto *pixels = static_cast<const uint8_t *>(bgra);
        if (!damage || first_flag || second_flag)
            return 0;
        self->last_damage = *damage;
        const bool valid = width == 2 && height == 2 && pixels[0] == 0x5a && pixels[15] == 0x5a;
        self->pixels_valid.store(valid, std::memory_order_release);
        self->pixel_buffer_paints.fetch_add(1, std::memory_order_release);
        if (self->pause_next_paint.exchange(false, std::memory_order_acq_rel)) {
            SetEvent(self->paint_entered);
            (void)WaitForSingleObject(self->paint_release, 10000);
        }
        if (valid && opaque_output)
            *opaque_output = 0x0badc0ffeeull;
        return valid ? 0x0badc0ffeeull : 0;
    }
    static void __cdecl accelerated_paint(void *hwnd, void *token, const native_contract::rect32 *,
                                          bool) {
        auto *self = active;
        if (!self || !self->registry_owned() || !IsWindow(static_cast<HWND>(hwnd)))
            return;
        self->shared_texture_paints.fetch_add(1, std::memory_order_release);
        if (self->ignore_texture_token.load(std::memory_order_acquire))
            return;
        const uint32_t count = *reinterpret_cast<uint32_t *>(self->bytes.data() + 0x150);
        for (uint32_t i = 0; i < count; ++i) {
            auto *connector = reinterpret_cast<std::byte *>(self->connector_ptrs[i]);
            if (reinterpret_cast<HWND>(*reinterpret_cast<uint64_t *>(connector + 0x48)) == hwnd) {
                *reinterpret_cast<uint64_t *>(connector + 0x110) =
                    reinterpret_cast<uint64_t>(token);
                break;
            }
        }
    }
};
FakeAgent *FakeAgent::active = nullptr;

uint64_t address(auto fn) { return reinterpret_cast<uint64_t>(fn); }
overwolf_hook::overwolf::ResolvedProfile make_profile(FakeAgent &agent) {
    FakeAgent::active = &agent;
    overwolf_hook::overwolf::ResolvedProfile p{};
    p.common = p.pixel_buffer_source = p.shared_texture_source = p.lifecycle = true;
    p.agent_global_slot = reinterpret_cast<uint64_t>(&agent.root_slot);
    p.agent_init = address(&FakeAgent::agent_init);

    p.agent_clear = address(&FakeAgent::agent_clear);

    p.run_agent = address(&FakeAgent::lifecycle_run);

    p.clear_agent = address(&FakeAgent::lifecycle_clear);

    p.unhook_agent = address(&FakeAgent::lifecycle_unhook);

    p.source_paint = address(&FakeAgent::paint);
    p.accelerated_paint = address(&FakeAgent::accelerated_paint);
    p.register_window = address(&FakeAgent::register_window);
    p.refresh_windows = address(&FakeAgent::refresh_windows);
    p.set_position = address(&FakeAgent::set_position);
    return p;
}

struct Texture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> texture;
    owh_legacy_shared_texture binding{sizeof(binding), 0, 0, 0, 0};
    HRESULT create() {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
            D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());
        if (FAILED(hr))
            return hr;
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(device.As(&dxgi)) || FAILED(dxgi->GetAdapter(adapter.GetAddressOf())))
            return E_FAIL;
        DXGI_ADAPTER_DESC desc{};
        if (FAILED(adapter->GetDesc(&desc)))
            return E_FAIL;
        D3D11_TEXTURE2D_DESC shape{};
        shape.Width = 16;
        shape.Height = 8;
        shape.MipLevels = 1;
        shape.ArraySize = 1;
        shape.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        shape.SampleDesc.Count = 1;
        shape.Usage = D3D11_USAGE_DEFAULT;
        shape.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        shape.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        if (FAILED(device->CreateTexture2D(&shape, nullptr, texture.GetAddressOf())))
            return E_FAIL;
        ComPtr<IDXGIResource> resource;
        if (FAILED(texture.As(&resource)))
            return E_FAIL;
        HANDLE token = nullptr;
        if (FAILED(resource->GetSharedHandle(&token)))
            return E_FAIL;
        binding.legacy_shared_token = reinterpret_cast<uintptr_t>(token);
        binding.adapter_luid_low = desc.AdapterLuid.LowPart;
        binding.adapter_luid_high = desc.AdapterLuid.HighPart;
        return S_OK;
    }
};

struct TestServer {
    HANDLE process = nullptr;
    std::unique_ptr<ipc::MessageConnection> server_connection, client_connection;
    HANDLE served_event = nullptr;
    std::thread thread;
    uint64_t started = 0, session = 0x72756e74696d6501ull;
    core::PeerIdentity identity{};
    core::HelloRequest hello{};
    owh_overlay_capabilities caps{};
    ::overwolf_hook::overwolf::EventSource event_source;
    events::EventHub captures;
    std::shared_ptr<AgentSession> agent_session;
    std::atomic<uint32_t> served_status{UINT32_MAX};
    bool broken_expected = false;

    explicit TestServer(OverlayDomain &overlays, bool shared_texture_enabled = true) : captures(event_source) {
        // Tick-count resolution can give concurrent fixture sessions the same
        // identity; detaching one must never retire another session's sources.
        static std::atomic<uint64_t> next_session{1};
        session += next_session.fetch_add(1, std::memory_order_relaxed);
        started = process_started(GetCurrentProcess());
        CHECK(DuplicateHandle(
            GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &process,
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_DUP_HANDLE, FALSE, 0));
        client_connection = ipc::MessageConnection::create(GetCurrentProcessId(), started);
        CHECK(client_connection != nullptr);
        server_connection =
            ipc::MessageConnection::import(client_connection->descriptor(), process);
        CHECK(server_connection != nullptr);

        owh_client_options options = OWH_CLIENT_OPTIONS_INIT;
        options.experimental_features = shared_texture_enabled ? OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE : 0;
        options.maximum_overlays = 2;
        options.maximum_receipts_per_overlay = 8;
        options.pixel_buffer_budget_bytes = 1 << 20;
        options.shared_texture_budget_bytes = 1 << 20;
        const auto policy = core::resolve_options(options);
        identity.client_process_id = identity.host_process_id = GetCurrentProcessId();
        identity.client_process_started = identity.host_process_started = started;
        identity.windows_session_id = owh_test::windows_session();
        // The Hello transport channel is bound to this descriptor challenge.
        identity.host_nonce = client_connection->descriptor().nonce;
        identity.build_info = {"fixture", 42};
        hello = owh_test::greeting(identity, policy);
        served_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        CHECK(served_event != nullptr);
        thread = std::thread([&, this] {
            agent_session = std::make_shared<AgentSession>(overlays, captures, *server_connection,
                                                           identity, session);
            ipc::ServiceRouter router;
            agent_session->bind(router);
            const auto served = ipc::serve(*server_connection, identity, session, router);
            served_status.store(served.status, std::memory_order_release);
            agent_session.reset();
            for (unsigned attempt = 0; attempt != 100; ++attempt) {
                const auto overlay_status = overlays.retire(session);
                const auto event_status = captures.cleanup_owner(session);
                if (overlay_status.status == OWH_OK && event_status.status == OWH_OK)
                    break;
                Sleep(10);
            }
            SetEvent(served_event);
        });
    }
    ~TestServer() {
        if (thread.joinable())
            thread.join();
        if (!broken_expected)
            CHECK(served_status.load(std::memory_order_acquire) == OWH_OK);
        if (served_event)
            CloseHandle(served_event);
        if (process)
            CloseHandle(process);
    }
    owh_sdk *connect() {
        owh_sdk *host = nullptr;
        const auto connected = client::test_fixture::connect_sdk(
            std::move(client_connection), GetCurrentProcessId(), started, hello, &host);
        if (connected.status != OWH_OK)
            std::fprintf(stderr, "connect status=%d stage=%u native=%llu server=%u\n",
                         connected.status, connected.failure_stage,
                         static_cast<unsigned long long>(connected.native_code),
                         served_status.load(std::memory_order_acquire));
        CHECK(connected.status == OWH_OK);
        return host;
    }
    void break_endpoint() {
        CHECK(server_connection != nullptr);
        broken_expected = true;
        server_connection->close();
    }
    void join_broken() {
        CHECK(broken_expected);
        CHECK(WaitForSingleObject(served_event, 5000) == WAIT_OBJECT_0);
        if (thread.joinable())
            thread.join();
        CHECK(served_status.load(std::memory_order_acquire) == OWH_AGENT_LOST);
    }
};

void pixel_buffer_flow(owh_sdk *host, OverlayDomain &runtime, FakeAgent &agent) {
    owh_overlay_spec spec{sizeof(spec), 2, 2, 0, 0};
    owh_pixel_buffer_overlay_options queue{sizeof(queue), 2, OWH_QUEUE_FIFO};
    owh_overlay *overlay = nullptr;
    const auto first_create = owh_overlay_create_pixel_buffer(host, &spec, &queue, &overlay);
    if (first_create.status != OWH_OK)
        std::fprintf(stderr, "runtime flow create status=%d stage=%u native=%llu\n",
                     first_create.status, first_create.failure_stage,
                     static_cast<unsigned long long>(first_create.native_code));
    CHECK(first_create.status == OWH_OK);
    owh_overlay *second_overlay = nullptr;
    CHECK(owh_overlay_create_pixel_buffer(host, &spec, &queue, &second_overlay).status == OWH_OK);
    owh_frame *frame = nullptr;
    owh_pixel_buffer view{};
    view.size = sizeof(view);
    CHECK(owh_frame_acquire(overlay, &frame, &view).status == OWH_OK);
    std::memset(view.pixels, 0x5a, static_cast<size_t>(view.pixel_bytes));
    owh_submission *receipt = nullptr;
    CHECK(owh_frame_commit(frame, &receipt).status == OWH_OK);
    owh_wait_options wait{};
    wait.size = sizeof(wait);
    wait.timeout_ms = 5000;
    owh_submission_info submission{};
    submission.size = sizeof(submission);
    CHECK(owh_submission_wait(receipt, &wait, &submission).status == OWH_OK);
    CHECK(submission.state == OWH_SUBMISSION_PUBLISHED);
    CHECK(agent.pixel_buffer_paints.load(std::memory_order_acquire) == 1 &&
          agent.pixels_valid.load(std::memory_order_acquire));

    owh_frame *second_frame = nullptr;
    owh_pixel_buffer second_view{};
    second_view.size = sizeof(second_view);
    CHECK(owh_frame_acquire(second_overlay, &second_frame, &second_view).status == OWH_OK);
    std::memset(second_view.pixels, 0x5a, static_cast<size_t>(second_view.pixel_bytes));
    owh_submission *second_receipt = nullptr;
    CHECK(owh_frame_commit(second_frame, &second_receipt).status == OWH_OK);
    owh_submission_info second_submission{};
    second_submission.size = sizeof(second_submission);
    CHECK(owh_submission_wait(second_receipt, &wait, &second_submission).status == OWH_OK);
    CHECK(second_submission.state == OWH_SUBMISSION_PUBLISHED);
    CHECK(agent.pixel_buffer_paints.load(std::memory_order_acquire) == 2);

    // Exercise the real public submit -> shared stream -> source adapter path.
    // Invalid hints leave the pending stream untouched; direct frames also allow hints.
    {
        unsigned char image[16];
        std::memset(image, 0x5a, sizeof(image));
        const owh_pixel_region invalid_damage{1, 1, 2, 1};
        CHECK(owh_overlay_submit_pixels(overlay, image, sizeof(image), 0,
            &invalid_damage, nullptr, nullptr).status == OWH_INVALID_ARGUMENT);
        const owh_pixel_region damage{1, 0, 1, 1};
        owh_submission *changed = nullptr;
        CHECK(owh_overlay_submit_pixels(overlay, image, sizeof(image), 0,
            &damage, nullptr, &changed).status == OWH_OK);
        owh_submission_info changed_info = OWH_SUBMISSION_INFO_INIT;
        CHECK(owh_submission_wait(changed, &wait, &changed_info).status == OWH_OK);
        CHECK(changed_info.state == OWH_SUBMISSION_PUBLISHED);
        CHECK(agent.last_damage.left == 1 && agent.last_damage.top == 0 &&
              agent.last_damage.right == 2 && agent.last_damage.bottom == 1);
        owh_submission_close(changed);
        owh_frame *hinted = nullptr;
        owh_pixel_buffer hinted_pixels = OWH_PIXEL_BUFFER_INIT;
        CHECK(owh_frame_acquire(overlay, &hinted, &hinted_pixels).status == OWH_OK);
        CHECK(owh_frame_set_damage(hinted, &damage).status == OWH_OK);
        CHECK(owh_frame_set_damage(hinted, &invalid_damage).status == OWH_INVALID_ARGUMENT);
        CHECK(owh_frame_set_damage(hinted, nullptr).status == OWH_OK);
        owh_frame_discard(hinted);
    }

    // Two native waiters observe the same receipt independently. Cancelling
    // those waits must not cancel the publication already held by the Agent.
    owh_frame *waiting_frame = nullptr;
    owh_pixel_buffer waiting_view = OWH_PIXEL_BUFFER_INIT;
    CHECK(owh_frame_acquire(overlay, &waiting_frame, &waiting_view).status == OWH_OK);
    std::memset(waiting_view.pixels, 0x5a, static_cast<size_t>(waiting_view.pixel_bytes));
    ResetEvent(agent.paint_entered);
    ResetEvent(agent.paint_release);
    agent.pause_next_paint.store(true, std::memory_order_release);
    owh_submission *waiting_receipt = nullptr;
    CHECK(owh_frame_commit(waiting_frame, &waiting_receipt).status == OWH_OK);
    CHECK(WaitForSingleObject(agent.paint_entered, 3000) == WAIT_OBJECT_0);
    // Source painting may block, but handshake and read-only probes are not
    // actor work. A second session remains usable while the first owns a lease.
    {
        TestServer independent(runtime);
        auto *other = independent.connect();
        CHECK(owh_sdk_ping(other, 1000).status == OWH_OK);
        owh_event_capabilities events{};
        events.size = sizeof(events);
        CHECK(owh_event_query_capabilities(other, &events).status == OWH_OK);
        owh_overlay_capabilities caps{};
        caps.size = sizeof(caps);
        CHECK(owh_overlay_query_capabilities(other, &caps).status == OWH_OK);
        CHECK(owh_sdk_detach(other).status == OWH_OK);
        owh_sdk_close(other);
    }
    owh_cancel *cancellation = nullptr;
    CHECK(owh_cancel_create(&cancellation).status == OWH_OK);
    const auto observe = [&] {
        owh_wait_options options{sizeof(options), 5000, cancellation};
        owh_submission_info observation = OWH_SUBMISSION_INFO_INIT;
        return owh_submission_wait(waiting_receipt, &options, &observation).status;
    };
    auto first_waiter = std::async(std::launch::async, observe);
    owh_cancel_signal(cancellation);
    CHECK(first_waiter.get() == OWH_CANCELLED);
    owh_submission_info waiting_info = OWH_SUBMISSION_INFO_INIT;
    CHECK(owh_submission_poll(waiting_receipt, &waiting_info).status == OWH_OK &&
          waiting_info.state == OWH_SUBMISSION_PENDING);
    SetEvent(agent.paint_release);
    owh_wait_options finish_wait{sizeof(finish_wait), 5000, nullptr};
    CHECK(owh_submission_wait(waiting_receipt, &finish_wait, &waiting_info).status == OWH_OK &&
          waiting_info.state == OWH_SUBMISSION_PUBLISHED);
    CHECK(owh_submission_wait(waiting_receipt, &finish_wait, &waiting_info).status == OWH_OK);
    CHECK(owh_submission_poll(waiting_receipt, &waiting_info).status == OWH_OK &&
          waiting_info.state == OWH_SUBMISSION_PUBLISHED);
    owh_submission_close(waiting_receipt);
    owh_cancel_close(cancellation);

    // Keep source A continuously ready while source B publishes and while
    // control Info/Close commands share the same overlay executor.
    std::atomic<bool> stream_a{true};
    std::atomic<uint32_t> accepted_a{0};
    std::thread producer_a([&] {
        while (stream_a.load(std::memory_order_acquire)) {
            unsigned char image[16];
            std::memset(image, 0x5a, sizeof(image));
            const auto submitted = owh_overlay_submit_pixels(overlay, image, sizeof(image), 0, nullptr, nullptr, nullptr);
            if (submitted.status != OWH_OK) {
                std::fprintf(stderr, "continuous submit: %u stage %u\n", submitted.status, submitted.failure_stage);
                break;
            }
            accepted_a.fetch_add(1, std::memory_order_release);
        }
    });
    const ULONGLONG stream_deadline = GetTickCount64() + 3000;
    while (accepted_a.load(std::memory_order_acquire) < 10 && GetTickCount64() < stream_deadline)
        Sleep(1);
    CHECK(accepted_a.load(std::memory_order_acquire) >= 10);
    unsigned char image_b[16];
    std::memset(image_b, 0x5a, sizeof(image_b));
    owh_submission *receipt_b = nullptr;
    CHECK(owh_overlay_submit_pixels(second_overlay, image_b, sizeof(image_b), 0,
        nullptr, &wait, &receipt_b).status == OWH_OK);
    owh_submission_info receipt_b_info{};
    receipt_b_info.size = sizeof(receipt_b_info);
    CHECK(owh_submission_wait(receipt_b, &wait, &receipt_b_info).status == OWH_OK);
    CHECK(receipt_b_info.state == OWH_SUBMISSION_PUBLISHED);
    owh_overlay_info_data info_b{};
    info_b.size = sizeof(info_b);
    CHECK(owh_overlay_query(second_overlay, &info_b).status == OWH_OK);
    CHECK(info_b.state == OWH_OVERLAY_READY);
    CHECK(owh_overlay_close(second_overlay).status == OWH_OK);
    CHECK(stream_a.load(std::memory_order_acquire));
    stream_a.store(false, std::memory_order_release);
    producer_a.join();
    CHECK(accepted_a.load(std::memory_order_acquire) >= 10);
    owh_submission_close(receipt_b);

    // Close revokes write authority immediately but leaves the frame's mapping
    // pinned until its independent local wrapper is closed.
    owh_frame *pinned = nullptr;
    owh_pixel_buffer pinned_view{};
    pinned_view.size = sizeof(pinned_view);
    // The continuous producer can leave all slots queued. Wait for an actual
    // writable lease rather than assuming the executor has already drained them.
    owh_result acquired{};
    const auto acquire_deadline = GetTickCount64() + 3000;
    do {
        acquired = owh_frame_acquire(overlay, &pinned, &pinned_view);
        if (acquired.status != OWH_BUSY)
            break;
        SwitchToThread();
    } while (GetTickCount64() < acquire_deadline);
    CHECK(acquired.status == OWH_OK);
    std::memset(pinned_view.pixels, 0x5a, static_cast<size_t>(pinned_view.pixel_bytes));
    CHECK(owh_overlay_close(overlay).status == OWH_OK);
    // Close consumes the remote overlay immediately, but the outstanding frame
    // keeps its pixel mapping alive until its wrapper is consumed.
    CHECK(static_cast<std::byte *>(pinned_view.pixels)[0] == std::byte{0x5a});
    CHECK(owh_frame_commit(pinned, nullptr).status == OWH_CLOSED);
    CHECK(owh_sdk_detach(host).status == OWH_OK);
    owh_submission_info after_detach{};
    after_detach.size = sizeof(after_detach);
    CHECK(owh_submission_poll(receipt, &after_detach).status == OWH_OK);
    CHECK(after_detach.state == OWH_SUBMISSION_PUBLISHED);
    owh_submission_close(receipt);
    owh_submission_close(second_receipt);
    (void)runtime;
}

void terminal_completion_contention(OverlayDomain &runtime, FakeAgent &agent) {
    const core::ResourceRef ref{876, 1};
    auto [allocated, producer] =
        ipc::PixelProducer::create(ref, GetCurrentProcessId(), process_started(GetCurrentProcess()),
                                   2, 2, 2, OWH_QUEUE_FIFO, core::maximum_mapping_bytes);
    CHECK(allocated.status == OWH_OK && producer);
    const auto created = runtime.create_pixel_buffer(ref, {sizeof(core::Geometry), 2, 2, 0, 0},
                                               producer->descriptor(), GetCurrentProcess(),
                                               std::make_shared<const core::SessionPolicy>(
                                                   core::resolve_options(OWH_CLIENT_OPTIONS_INIT)));
    CHECK(created.first.status == OWH_OK);
    auto acquired = producer->try_acquire();
    const auto acquire_deadline = GetTickCount64() + 1500;
    while (acquired.result.status == OWH_BUSY && GetTickCount64() < acquire_deadline) {
        SwitchToThread();
        acquired = producer->try_acquire();
    }
    CHECK(acquired.result.status == OWH_OK);
    std::fill(acquired.frame.pixels.begin(), acquired.frame.pixels.end(), std::byte{});
    // Invalid fixture pixels make the foreign publish return a terminal error.
    ResetEvent(agent.paint_entered);
    ResetEvent(agent.paint_release);
    agent.pause_next_paint.store(true, std::memory_order_release);
    CHECK(producer->commit(acquired.frame).result.status == OWH_OK);
    CHECK(WaitForSingleObject(agent.paint_entered, 3000) == WAIT_OBJECT_0);
    {
        auto &header = const_cast<ipc::Header &>(producer->header());
        std::lock_guard lock(header.mutex);
        SetEvent(agent.paint_release);
        // The actor returns from paint, defers completion/loss because of this
        // mutex, then processes the query. No timing sleep is needed here.
        const auto info = runtime.info(ref);
        CHECK(info.first.status == OWH_OK && info.second.state == OWH_OVERLAY_LOST);
    }
    uint32_t count = 0;
    std::array<ipc::Completion, ipc::maximum_slots> completed{};
    const auto deadline = GetTickCount64() + 1500;
    do {
        completed = producer->collect_completed(count);
        if (count)
            break;
        Sleep(5);
    } while (GetTickCount64() < deadline);
    CHECK(count == 1 && !completed[0].published &&
          completed[0].result.failure_stage == OWH_STAGE_SOURCE_PUBLISH);
    CHECK(runtime.close(ref).status == OWH_OK);
    CHECK(producer->try_acquire().result.status != OWH_OK);
}

void shared_texture_flow(owh_sdk *host, FakeAgent &agent) {
    Texture source;
    const HRESULT hr = source.create();
    if (FAILED(hr)) {
        std::puts("SKIP owned D3D11 hardware source unavailable");
        CHECK(owh_sdk_detach(host).status == OWH_OK);
        return;
    }
    owh_overlay_spec spec{sizeof(spec), 16, 8, 0, 0};
    owh_overlay *overlay = nullptr;
    CHECK(owh_overlay_create_unbound_shared_texture(host, &spec, &overlay).status == OWH_OK);
    owh_overlay_info_data info{};
    info.size = sizeof(info);
    CHECK(owh_overlay_query(overlay, &info).status == OWH_OK);
    CHECK(info.state == OWH_OVERLAY_AWAITING_TEXTURE &&
          agent.shared_texture_paints.load(std::memory_order_acquire) == 0);
    CHECK(owh_overlay_close(overlay).status == OWH_OK);
    agent.ignore_texture_token.store(true, std::memory_order_release);
    agent.deny_refresh.store(true, std::memory_order_release);
    overlay = reinterpret_cast<owh_overlay *>(1);
    const auto rejected = owh_overlay_create_shared_texture(host, &spec, &source.binding, &overlay);
    CHECK(rejected.status == OWH_SYSTEM_ERROR && rejected.failure_stage == OWH_STAGE_SOURCE_PUBLISH);
    CHECK(overlay == nullptr); // factory consumes the failed source; no caller cleanup

    agent.deny_refresh.store(false, std::memory_order_release);
    agent.ignore_texture_token.store(false, std::memory_order_release);
    overlay = nullptr;
    const auto bound_create = owh_overlay_create_shared_texture(host, &spec, &source.binding, &overlay);
    CHECK(bound_create.status == OWH_OK);
    info = {};
    info.size = sizeof(info);
    CHECK(owh_overlay_query(overlay, &info).status == OWH_OK);
    CHECK(info.state == OWH_OVERLAY_READY && agent.shared_texture_paints.load(std::memory_order_acquire) >= 2);
    CHECK(owh_overlay_close(overlay).status == OWH_OK);
    CHECK(owh_sdk_detach(host).status == OWH_OK);
}

void broken_endpoint_flow(OverlayDomain &runtime, FakeAgent &agent) {
    owh_submission *terminal_receipt = nullptr;
    {
        TestServer server(runtime);
        owh_sdk *host = server.connect();
        owh_overlay_spec spec{sizeof(spec), 2, 2, 0, 0};
        owh_pixel_buffer_overlay_options queue{sizeof(queue), 2, OWH_QUEUE_FIFO};
        owh_overlay *overlay = nullptr;
        CHECK(owh_overlay_create_pixel_buffer(host, &spec, &queue, &overlay).status == OWH_OK);
        owh_frame *committed = nullptr;
        owh_pixel_buffer view{};
        view.size = sizeof(view);
        CHECK(owh_frame_acquire(overlay, &committed, &view).status == OWH_OK);
        std::memset(view.pixels, 0x5a, static_cast<size_t>(view.pixel_bytes));
        CHECK(owh_frame_commit(committed, &terminal_receipt).status == OWH_OK);
        owh_wait_options wait{};
        wait.size = sizeof(wait);
        wait.timeout_ms = 5000;
        owh_submission_info receipt_info{};
        receipt_info.size = sizeof(receipt_info);
        CHECK(owh_submission_wait(terminal_receipt, &wait, &receipt_info).status == OWH_OK);
        CHECK(receipt_info.state == OWH_SUBMISSION_PUBLISHED);

        owh_frame *in_flight = nullptr;
        owh_pixel_buffer inflight_view{};
        inflight_view.size = sizeof(inflight_view);
        CHECK(owh_frame_acquire(overlay, &in_flight, &inflight_view).status == OWH_OK);
        std::memset(inflight_view.pixels, 0x5a, static_cast<size_t>(inflight_view.pixel_bytes));
        owh_submission *lost_receipt = nullptr;
        ResetEvent(agent.paint_entered);
        ResetEvent(agent.paint_release);
        agent.pause_next_paint.store(true, std::memory_order_release);
        CHECK(owh_frame_commit(in_flight, &lost_receipt).status == OWH_OK);
        CHECK(WaitForSingleObject(agent.paint_entered, 5000) == WAIT_OBJECT_0);
        const ULONGLONG close_started = GetTickCount64();
        CHECK(owh_overlay_close(overlay).status == OWH_OK);
        CHECK(GetTickCount64() - close_started < 1000);
        // The server endpoint dies while its process handle remains signaled as
        // alive. Session retirement must clean native children and release its owner
        // gate; the local C handles then dispose without a cleanup response.
        server.break_endpoint();
        CHECK(WaitForSingleObject(server.process, 0) == WAIT_TIMEOUT);
        owh_overlay_capabilities unavailable{};
        unavailable.size = sizeof(unavailable);
        CHECK(owh_overlay_query_capabilities(host, &unavailable).status == OWH_AGENT_LOST);
        CHECK(owh_sdk_ping(host, 1000).status == OWH_AGENT_LOST);
        owh_submission_info lost_info{};
        lost_info.size = sizeof(lost_info);
        CHECK(owh_submission_poll(lost_receipt, &lost_info).status == OWH_OK);
        CHECK(lost_info.state == OWH_SUBMISSION_LOST);
        SetEvent(agent.paint_release);
        server.join_broken();
        CHECK(owh_sdk_detach(host).status == OWH_OK);
        owh_submission_info after_detach{};
        after_detach.size = sizeof(after_detach);
        CHECK(owh_submission_poll(terminal_receipt, &after_detach).status == OWH_OK);
        CHECK(after_detach.state == OWH_SUBMISSION_PUBLISHED);
        owh_submission_info after_loss{};
        after_loss.size = sizeof(after_loss);
        CHECK(owh_submission_poll(lost_receipt, &after_loss).status == OWH_OK);
        CHECK(after_loss.state == OWH_SUBMISSION_LOST);
        owh_submission_close(lost_receipt);
    }
    owh_submission_close(terminal_receipt);

    // A disconnected client does not block a later independent session.
    {
        TestServer server(runtime);
        auto *host = server.connect();
        owh_overlay_spec spec{sizeof(spec), 2, 2, 0, 0};
        owh_pixel_buffer_overlay_options queue{sizeof(queue), 2, OWH_QUEUE_FIFO};
        owh_overlay *overlay = nullptr;
        CHECK(owh_overlay_create_pixel_buffer(host, &spec, &queue, &overlay).status == OWH_OK);
        CHECK(owh_overlay_close(overlay).status == OWH_OK);
        CHECK(owh_sdk_detach(host).status == OWH_OK);
    }
    (void)agent;
}
} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    executor_failure_cases();
    FakeAgent agent;
    LifetimeAdmission admission;
    AdmissionFixture::initialize(admission, make_profile(agent));
    uint64_t root = 0, epoch = 0;
    CHECK(admission.try_refresh(root, epoch));
    // Lifecycle detours change the function-entry signatures in the loaded
    // image. Capability lookup must use the same admitted pre-hook profile as
    // source creation, without starting the actor or creating a source.
    {
        OverlayHost cached_host(admission);
        owh_overlay_capabilities caps{};
        caps.size = sizeof(caps);
        cached_host.fill_capabilities(caps, true);
        CHECK(caps.available == (OWH_CAP_PIXEL_BUFFER | OWH_CAP_D3D11_SHARED_TEXTURE));
        CHECK(caps.enabled == caps.available);
        CHECK(caps.pixel_buffer_status.status == OWH_OK && caps.shared_texture_status.status == OWH_OK);
        caps = {};
        cached_host.fill_capabilities(caps, false);
        CHECK(caps.available == (OWH_CAP_PIXEL_BUFFER | OWH_CAP_D3D11_SHARED_TEXTURE));
        CHECK(caps.enabled == OWH_CAP_PIXEL_BUFFER);
        CHECK(caps.shared_texture_status.status == OWH_OPT_IN_REQUIRED);
        CHECK(agent.pixel_buffer_paints.load() == 0 && agent.shared_texture_paints.load() == 0);
    }
    // The process root stays inert even if its eventual window class cannot be
    // registered. The executor reports startup failure and contains teardown.
    const auto blocked_class = L"OWH.NativeOverlay." + std::to_wstring(GetCurrentProcessId());
    WNDCLASSEXW blocker{};
    blocker.cbSize = sizeof(blocker);
    blocker.lpfnWndProc = DefWindowProcW;
    blocker.hInstance = GetModuleHandleW(nullptr);
    blocker.lpszClassName = blocked_class.c_str();
    CHECK(RegisterClassExW(&blocker));
    bool inert_constructed = false;
    try {
        overwolf_hook::agent::AgentRoot inert_services(admission);
        inert_constructed = true;
    } catch (...) {
    }
    CHECK(inert_constructed); // a conflicting window class does not activate
                              // services
    DWORD handles_before = 0, handles_after = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
    {
        OverlayExecutor failed_executor;
        CHECK(!failed_executor.ensure_started({[] { return false; }, {}, {}, {}, {}}));
        CHECK(failed_executor.state() == OverlayExecutor::State::failed);
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
    CHECK(handles_after == handles_before);
    CHECK(UnregisterClassW(blocked_class.c_str(), blocker.hInstance));
    // Destroy an owned domain with a real registered native source still open.
    // Teardown must unregister it before releasing the shared allocation/host.
    {
        OverlayHost teardown_host(admission);
        const auto before = *reinterpret_cast<uint32_t *>(agent.bytes.data() + 0x150);
        auto [allocated, producer] = ipc::PixelProducer::create(
            {987, 1}, GetCurrentProcessId(), process_started(GetCurrentProcess()), 2, 2, 2,
            OWH_QUEUE_FIFO, core::maximum_mapping_bytes);
        CHECK(allocated.status == OWH_OK && producer);
        {
            OverlayDomain teardown_domain(teardown_host);
            const auto created =
                teardown_domain.create_pixel_buffer({987, 1}, {sizeof(core::Geometry), 2, 2, 0, 0},
                                              producer->descriptor(), GetCurrentProcess(),
                                              std::make_shared<const core::SessionPolicy>(
                                                  core::resolve_options(OWH_CLIENT_OPTIONS_INIT)));
            CHECK(created.first.status == OWH_OK && teardown_domain.child_count(987) == 1);
            CHECK(*reinterpret_cast<uint32_t *>(agent.bytes.data() + 0x150) == before + 1);
            teardown_host.wake();
        }
        CHECK(*reinterpret_cast<uint32_t *>(agent.bytes.data() + 0x150) == before);
    }
    overwolf_hook::agent::AgentRoot services(admission);
    auto &runtime = services.overlays();
    terminal_completion_contention(runtime, agent);
    agent.pixel_buffer_paints.store(0, std::memory_order_release);
    {
        TestServer server(runtime);
        auto *host = server.connect();
        pixel_buffer_flow(host, runtime, agent);
    }
    {
        TestServer server(runtime, false);
        auto *host = server.connect();
        owh_overlay_spec spec{sizeof(spec), 16, 8, 0, 0};
        owh_overlay *overlay = reinterpret_cast<owh_overlay *>(uintptr_t{1});
        const auto result = owh_overlay_create_unbound_shared_texture(host, &spec, &overlay);
        CHECK(result.status == OWH_OPT_IN_REQUIRED && overlay == nullptr);
        CHECK(agent.shared_texture_paints.load(std::memory_order_acquire) == 0);
        CHECK(owh_sdk_detach(host).status == OWH_OK);
    }
    {
        TestServer server(runtime);
        auto *host = server.connect();
        shared_texture_flow(host, agent);
    }
    broken_endpoint_flow(runtime, agent);
    std::puts("PASS actual host C ABI -> control protocol -> Agent OverlayDomain -> "
              "fake native source");
    return 0;
}
