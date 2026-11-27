#include "support/session_fixture.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <future>
#include <overwolf_hook/abi/events.h>
#include <overwolf_hook/abi/overlays.h>
#include <owh/core/services.hpp>
#include <owh/ipc/event_log.hpp>
#include <owh/ipc/pixel_stream.hpp>
#include <owh/ipc/services.hpp>
#include <owh/ipc/session_protocol.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/result.hpp>
#include <owh/testing/client_fixture.hpp>
#include <string>
#include <thread>
#undef assert
#define assert(test)                                                                               \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            std::fprintf(stderr, "check failed line %d: %s\n", __LINE__, #test);                   \
            ExitProcess(2);                                                                        \
        }                                                                                          \
    } while (false)
using namespace overwolf_hook;
using namespace overwolf_hook::client;
using namespace overwolf_hook::ipc;
namespace {
uint64_t started() {
    FILETIME a{}, b{}, c{}, d{};
    assert(GetProcessTimes(GetCurrentProcess(), &a, &b, &c, &d));
    return (uint64_t(a.dwHighDateTime) << 32) | a.dwLowDateTime;
}
struct Publisher final : core::OverlayService {
    core::PeerIdentity identity{};
    uint64_t session_id = 0;
    std::atomic<bool> ready{true};
    uint32_t corrupt_capability = 0;
    core::OverlayCapabilitiesResponse capabilities(const core::Empty &) noexcept override {
        core::OverlayCapabilitiesResponse reply{};
        reply.result = make_result(OWH_OK);
        auto &c = reply.value;
        c.size = sizeof(c);
        c.process_id = identity.host_process_id;
        c.process_started = identity.host_process_started;
        c.session_id = session_id;
        c.maximum_overlays = options.maximum_overlays;
        c.maximum_receipts_per_overlay = options.maximum_receipts_per_overlay;
        c.pixel_buffer_budget_bytes = options.pixel_buffer_budget_bytes;
        c.shared_texture_budget_bytes = options.shared_texture_budget_bytes;
        c.maximum_width = core::maximum_width;
        c.maximum_height = core::maximum_height;
        c.minimum_slots = core::minimum_slots;
        c.maximum_slots = core::maximum_slots;
        c.maximum_pixel_buffer_bytes = core::maximum_image_bytes;
        c.pixel_buffer_status = make_result(ready ? OWH_OK : OWH_INCOMPATIBLE);
        c.shared_texture_status = make_result(OWH_INCOMPATIBLE);
        c.available = c.enabled = ready ? OWH_CAP_PIXEL_BUFFER : 0;
        switch (corrupt_capability) {
        case 1:
            ++c.process_id;
            break;
        case 2:
            ++c.process_started;
            break;
        case 3:
            ++c.session_id;
            break;
        case 4:
            c.reserved = 1;
            break;
        case 5:
            c.available |= 8;
            break;
        case 6:
            ++c.maximum_width;
            break;
        case 7:
            ++c.pixel_buffer_budget_bytes;
            break;
        case 8:
            c.pixel_buffer_status.status = 999;
            break;
        }
        return reply;
    }
    ipc::MessageConnection *disconnect_before_reply = nullptr;
    ipc::MessageConnection *connection = nullptr;
    core::SessionPolicy options = core::resolve_options(OWH_CLIENT_OPTIONS_INIT);
    bool imported_before_disconnect = false;
    std::unique_ptr<PixelConsumer> pixels;
    ResourceRef ref{};
    std::thread worker;
    Handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle resume{CreateEventW(nullptr, TRUE, TRUE, nullptr)};
    bool closing = false, malformed_info = false;
    std::atomic<uint32_t> published{0};
    ~Publisher() {
        SetEvent(stop.get());
        if (worker.joinable())
            worker.join();
    }
    core::CreateSourceResponse
    create_pixel_buffer(const core::CreatePixelBufferSourceRequest &request) noexcept override {
        if (!ready)
            return {make_result(OWH_INCOMPATIBLE), {}};
        ref = request.ref;
        auto [import_result, view] = PixelConsumer::import(
            request.queue, connection->peer(), request.ref, request.geometry.width,
            request.geometry.height, request.queue.slot_count, request.queue.queue_policy,
            options.pixel_buffer_budget_bytes);
        if (import_result.status != OWH_OK)
            return {import_result, {}};
        pixels = std::move(view);
        if (disconnect_before_reply) {
            imported_before_disconnect = true;
            disconnect_before_reply->close();
            return {make_result(OWH_OK), ref};
        }
        ResetEvent(stop.get());
        closing = false;
        worker = std::thread([this] {
            HANDLE waits[]{stop.get(), pixels->wake_event()};
            for (;;) {
                const auto wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wait != WAIT_OBJECT_0 + 1)
                    return;
                HANDLE admission[]{stop.get(), resume.get()};
                if (WaitForMultipleObjects(2, admission, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
                    return;
                for (;;) {
                    auto [r, read] = pixels->try_take_ready();
                    if (r.status == OWH_NO_DATA)
                        break;
                    if (r.status == OWH_BUSY) {
                        // try_take_ready is deliberately nonblocking. A producer
                        // still releasing the shared mutex is not stream death.
                        SwitchToThread();
                        continue;
                    }
                    if (r.status != OWH_OK)
                        return;
                    assert(read->pixels()[0] == std::byte{0x5a});
                    ++published;
                    read->complete(make_result(OWH_OK));
                }
            }
        });
        return {make_result(OWH_OK), ref};
    }
    core::OverlayInfoResponse get_info(const core::SourceRequest &request) noexcept override {
        const ResourceRef r = request.ref;
        if (r != ref)
            return {make_result(OWH_STALE_GENERATION), {}};
        owh_overlay_info_data i{};
        i.size = malformed_info ? 0 : sizeof(i);
        i.state = closing ? OWH_OVERLAY_CLOSING : OWH_OVERLAY_READY;
        i.id = r.resource_id;
        i.kind = OWH_OVERLAY_PIXEL_BUFFER;
        i.width = i.height = 2;
        return {make_result(OWH_OK), i};
    }
    core::ResultResponse close(const core::SourceRequest &request) noexcept override {
        const ResourceRef r = request.ref;
        if (!pixels)
            return {make_result(OWH_OK)};
        if (r != ref)
            return {make_result(OWH_STALE_GENERATION)};
        closing = true;
        SetEvent(stop.get());
        if (worker.joinable())
            worker.join();
        const auto done = pixels->settle_for_close();
        if (done.status == OWH_OK)
            pixels.reset();
        // Logical close is accepted; the fixture retains a busy consumer.
        return {done.status == OWH_BUSY ? make_result(OWH_OK) : done};
    }
    core::ResultResponse bind_texture(const core::BindD3D11Request &) noexcept override {
        return {make_result(OWH_UNSUPPORTED)};
    }
    core::CreateSourceResponse
    create_shared_texture(const core::CreateSharedTextureSourceRequest &request) noexcept override {
        return {make_result(OWH_UNSUPPORTED), request.ref};
    }
};
struct Captures final : core::EventService {
    core::EventCapabilitiesResponse capabilities(const core::Empty &) noexcept override {
        return {};
    }
    std::unique_ptr<ipc::EventLog> channel;
    Handle wake{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Handle closed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ResourceRef ref{};
    owh_subscription_info info{};
    bool malformed_info = false;
    std::atomic<uint32_t> names{0};
    core::SubscribeEventsResponse
    subscribe(const core::SubscribeEventsRequest &request) noexcept override {
        try {
            ref = request.ref;
            const auto name = "public-event-" + std::to_string(GetCurrentProcessId()) + "-" +
                              std::to_string(names.fetch_add(1));
            channel = ipc::EventLog::create(name.c_str(), 8);
            if (!channel)
                return {make_result(OWH_RESOURCE_EXHAUSTED), {}, {}};
            core::CapturedEvent record{};
            record.captured_qpc = 12345;
            record.source_channel = request.source_channel;
            record.raw_size = sizeof(record.raw);
            record.raw[0] = 0x42;
            const auto published = channel->publish(record);
            if (published.status != OWH_OK)
                return {published, {}, {}};
            info = {};
            info.size = sizeof(info);
            info.state = OWH_SUBSCRIPTION_ACTIVE;
            info.hook_enabled = 1;
            core::SubscribeEventsResponse response{make_result(OWH_OK), request.ref, {}};
            std::memcpy(response.channel, name.c_str(), name.size() + 1);
            response.start_sequence = 1;
            response.wake_handle = reinterpret_cast<uint64_t>(wake.get());
            response.closed_handle = reinterpret_cast<uint64_t>(closed.get());
            return response;
        } catch (...) {
            return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_ALLOCATION), {}, {}};
        }
    }
    core::EventStatisticsResponse statistics(const core::SourceRequest &request) noexcept override {
        if (request.ref != ref || !channel)
            return {make_result(OWH_STALE_GENERATION), {}};
        auto copy = info;
        if (malformed_info)
            copy.size = 0;
        return {make_result(OWH_OK), copy};
    }
    core::ResultResponse close(const core::SourceRequest &) noexcept override {
        if (!channel)
            return {make_result(OWH_OK)};
        const auto result = channel->close(100);
        if (result.status == OWH_OK)
            channel.reset();
        return {result};
    }
};
struct SessionService final : core::SessionService {
    core::PeerIdentity identity;
    uint64_t session;
    bool greeted = false;
    SessionService(const core::PeerIdentity &value, uint64_t id) : identity(value), session(id) {}
    core::HelloResponse hello(const core::HelloRequest &request) noexcept override {
        core::HelloResponse response{};
        response.result = make_result(OWH_INCOMPATIBLE);
        response.abi_version = OWH_ABI_VERSION;
        response.wire_protocol = ipc::protocol;
        response.host_process_id = identity.host_process_id;
        response.host_process_started = identity.host_process_started;
        response.session_id = session;
        response.policy = request.options;
        response.build_info = identity.build_info;
        if (request.client_process_id == identity.client_process_id &&
            request.client_process_started == identity.client_process_started &&
            request.host_process_started == identity.host_process_started &&
            request.windows_session_id == identity.windows_session_id &&
            request.abi_version == OWH_ABI_VERSION &&
            !std::memcmp(&request.build_info, &identity.build_info, sizeof(owh_build_info))) {
            greeted = true;
            response.result = make_result(OWH_OK);
        }
        return response;
    }
    core::ResultResponse detach(const core::Empty &) noexcept override {
        return {make_result(OWH_OK)};
    }
    core::ResultResponse ping(const core::Empty &) noexcept override {
        return {make_result(OWH_OK)};
    }
};
struct DropContext {
    core::PeerIdentity identity;
    uint64_t session;
    ipc::ConnectionDescriptor descriptor;
};
int drop_child(HANDLE context_pipe) {
    DropContext context{};
    DWORD bytes = 0;
    assert(ReadFile(context_pipe, &context, sizeof(context), &bytes, nullptr) &&
           bytes == sizeof(context));
    CloseHandle(context_pipe);
    Handle producer(
        OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                    context.identity.client_process_id));
    assert(producer);
    auto connection = ipc::MessageConnection::import(context.descriptor, producer.get());
    assert(connection);
    Publisher publisher;
    publisher.identity = context.identity;
    publisher.session_id = context.session;
    publisher.disconnect_before_reply = connection.get();
    publisher.connection = connection.get();
    DWORD before = 0, after = 0;
    SessionService session(context.identity, context.session);
    Captures captures;
    assert(GetProcessHandleCount(GetCurrentProcess(), &before));
    ipc::ServiceRouter router;
    router.bind(session);
    router.bind(publisher);
    router.bind(captures);
    const auto result = ipc::serve(*connection, context.identity, context.session, router);
    if (result.status != OWH_AGENT_LOST)
        return 10;
    if (!publisher.imported_before_disconnect)
        return 11;
    // The router only dispatches calls. Its owner performs session retirement,
    // just as AgentServer does after a control connection disappears.
    if (publisher.pixels && publisher.pixels->settle_for_close().status != OWH_OK)
        return 12;
    publisher.pixels.reset();
    if (!GetProcessHandleCount(GetCurrentProcess(), &after))
        return 13;
    if (after != before)
        return 14;
    return 0;
}
void dropped_create_reply() {
    SECURITY_ATTRIBUTES inherited{sizeof(inherited), nullptr, TRUE};
    HANDLE input = nullptr, output = nullptr;
    assert(CreatePipe(&input, &output, &inherited, sizeof(DropContext)));
    Handle child_input(input), parent_output(output);
    assert(SetHandleInformation(parent_output.get(), HANDLE_FLAG_INHERIT, 0));
    wchar_t executable[32768]{};
    assert(GetModuleFileNameW(nullptr, executable, 32768));
    auto command = L"\"" + std::wstring(executable) + L"\" drop-child " +
                   std::to_wstring(reinterpret_cast<uintptr_t>(child_input.get()));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    assert(CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &startup, &process));
    Handle child_process(process.hProcess), child_thread(process.hThread);
    child_input.reset();
    FILETIME created{}, exited{}, kernel{}, user{};
    assert(GetProcessTimes(child_process.get(), &created, &exited, &kernel, &user));
    const auto child_started = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    auto client = ipc::MessageConnection::create(process.dwProcessId, child_started);
    assert(client);
    DropContext context{};
    context.session = 789;
    auto &identity = context.identity;



    identity.client_process_id = GetCurrentProcessId();
    identity.client_process_started = started();
    identity.host_process_id = process.dwProcessId;
    identity.host_process_started = child_started;
    identity.windows_session_id = owh_test::windows_session();
    identity.host_nonce = client->descriptor().nonce;
    identity.build_info = {"fixture", 42};
    // ConnectionDescriptor handle values belong to the client process. Agent
    // import duplicates them from the verified owner process.
    context.descriptor = client->descriptor();
    DWORD bytes = 0;
    assert(WriteFile(parent_output.get(), &context, sizeof(context), &bytes, nullptr) &&
           bytes == sizeof(context));
    const core::HelloRequest greeting = owh_test::greeting(identity);
    owh_sdk *host = nullptr;
    const auto connected = test_fixture::connect_sdk(std::move(client), process.dwProcessId,
                                                     child_started, greeting, &host);
    if (connected.status != OWH_OK)
        std::fprintf(stderr, "drop child fixture connect status=%d stage=%u native=%llu\n",
                     connected.status, connected.failure_stage,
                     static_cast<unsigned long long>(connected.native_code));
    assert(connected.status == OWH_OK);
    DWORD before = 0, after = 0;
    assert(GetProcessHandleCount(GetCurrentProcess(), &before));
    const owh_overlay_spec spec{sizeof(spec), 2, 2, 0, 0};
    owh_overlay *overlay = nullptr;
    assert(owh_overlay_create_pixel_buffer(host, &spec, nullptr, &overlay).status != OWH_OK && !overlay);
    assert(GetProcessHandleCount(GetCurrentProcess(), &after) && after <= before);
    assert(owh_sdk_detach(host).status == OWH_OK);
    owh_sdk_close(host);
    assert(WaitForSingleObject(child_process.get(), 5000) == WAIT_OBJECT_0);
    DWORD exit_code = 99;
    assert(GetExitCodeProcess(child_process.get(), &exit_code));
    if (exit_code != 0)
        std::fprintf(stderr, "drop child exit code=%lu\n", exit_code);
    assert(exit_code == 0);
}
} // namespace
int main(int argc, char **argv) {
    const std::string mode = argc > 1 ? argv[1] : "overlay";
    if (mode == "drop-child" && argc == 3)
        return drop_child(reinterpret_cast<HANDLE>(std::stoull(argv[2])));
    if (mode == "drop-reply") {
        dropped_create_reply();
        std::cout << "cross-process Create reply loss after queue import cleaned both owners\n";
        return 0;
    }

    const bool capture_mode = mode.starts_with("capture");
    const bool malformed = mode == "malformed";
    const bool malformed_event = mode == "capture-malformed";
    owh_test::Session fixture(456);
    auto &identity = fixture.identity;
    fixture.options.maximum_receipts_per_overlay = 1;
    if (mode == "allocation")
        fixture.options.maximum_overlays = 1;
    identity.build_info = {"fixture", 42};
    Publisher publisher;
    if (mode == "managed") {
        ResetEvent(publisher.resume.get());
        fixture.options.maximum_receipts_per_overlay = 256;
    }
    publisher.identity = identity;
    publisher.session_id = fixture.session_id;
    if (mode.starts_with("caps-corrupt-"))
        publisher.corrupt_capability = std::stoul(mode.substr(13));
    publisher.connection = fixture.server.get();
    publisher.options = fixture.options;
    publisher.malformed_info = malformed;
    Captures captures;
    captures.malformed_info = malformed_event;
    SessionService session(identity, fixture.session_id);
    ipc::ServiceRouter router;
    router.bind(session);
    router.bind(publisher);
    router.bind(captures);
    std::thread serving([&] {
        const auto outcome = ipc::serve(*fixture.server, identity, fixture.session_id, router);
        const auto expected = (malformed || malformed_event || publisher.corrupt_capability)
                                  ? OWH_AGENT_LOST
                                  : OWH_OK;
        if (outcome.status != expected)
            std::fprintf(
                stderr,
                "public fixture service status=%d stage=%u domain=%u native=%llu malformed=%d\n",
                outcome.status, outcome.failure_stage, outcome.error_domain,
                static_cast<unsigned long long>(outcome.native_code), malformed);
        assert(outcome.status == expected);
    });
    owh_sdk *host = nullptr;
    assert(test_fixture::connect_sdk(std::move(fixture.client), GetCurrentProcessId(),
                                     fixture.started, owh_test::greeting(identity, fixture.options), &host)
               .status == OWH_OK);
    if (publisher.corrupt_capability) {
        owh_overlay_capabilities caps{};
        caps.size = sizeof(caps);
        assert(owh_overlay_query_capabilities(host, &caps).status == OWH_CORRUPT);
        assert(owh_sdk_detach(host).status == OWH_OK);
        serving.join();
        owh_sdk_close(host);
        return 0;
    }
    if (mode == "readiness") {
        publisher.ready = false;
        owh_overlay_capabilities caps{};
        caps.size = sizeof(caps);
        assert(owh_overlay_query_capabilities(host, &caps).status == OWH_OK);
        assert(caps.pixel_buffer_status.status == OWH_INCOMPATIBLE);
        publisher.ready = true;
        assert(owh_overlay_query_capabilities(host, &caps).status == OWH_OK &&
               caps.pixel_buffer_status.status == OWH_OK);
        publisher.ready = false;
        owh_overlay_spec spec{sizeof(spec), 2, 2};
        owh_overlay *rejected = nullptr;
        assert(owh_overlay_create_pixel_buffer(host, &spec, nullptr, &rejected).status ==
                   OWH_INCOMPATIBLE &&
               !rejected);
        // Readiness recovers without a reattach or a preceding capability query.
        publisher.ready = true;
    }
    if (mode == "allocation") {
        const owh_overlay_spec large{sizeof(owh_overlay_spec), 4096, 4096};
        const owh_pixel_buffer_overlay_options many{sizeof(owh_pixel_buffer_overlay_options), 8, OWH_QUEUE_FIFO};
        for (unsigned attempt = 0; attempt != 2; ++attempt) {
            owh_overlay *rejected = nullptr;
            assert(owh_overlay_create_pixel_buffer(host, &large, &many, &rejected).status ==
                       OWH_RESOURCE_EXHAUSTED &&
                   !rejected);
        }
        // Continue with an ordinary small overlay: failed layout reservations
        // must not exhaust this session's single allowed resource.
    }
    if (capture_mode) {
        owh_event_options options{sizeof(options), 900001};
        owh_subscription *subscription = nullptr;
        assert(owh_event_subscribe(host, &options, &subscription).status == OWH_OK && subscription);
        owh_subscription_info statistics{};
        statistics.size = sizeof(statistics);
        if (malformed_event) {
            assert(owh_subscription_query(subscription, &statistics).status == OWH_CORRUPT);
            assert(statistics.size == sizeof(statistics) && statistics.hook_enabled == 0);
            assert(owh_subscription_close(subscription).status == OWH_AGENT_LOST);
            assert(owh_sdk_detach(host).status == OWH_OK);
            serving.join();
            owh_sdk_close(host);
            std::cout << "malformed event statistics rejected without exposing output\n";
            return 0;
        }
        assert(owh_subscription_query(subscription, &statistics).status == OWH_OK &&
               statistics.state == OWH_SUBSCRIPTION_ACTIVE);
        owh_event event{};
        owh_wait_options wait{sizeof(wait), 1000, nullptr};
        assert(owh_subscription_receive(subscription, &wait, &event).status == OWH_OK);
        assert(event.sequence == 1 && event.lost_before == 0 && event.captured_qpc == 12345 &&
               event.source_channel == 900001 && event.raw_size == sizeof(event.raw) && event.raw[0] == 0x42);
        assert(owh_subscription_poll(subscription, &event).status == OWH_NO_DATA);
        assert(owh_subscription_close(subscription).status == OWH_OK);
        assert(owh_sdk_detach(host).status == OWH_OK);
        serving.join();
        owh_sdk_close(host);
        std::cout << "typed event service live subscription flow passed\n";
        return 0;
    }

    owh_overlay_spec spec{sizeof(spec), 2, 2, 0, 0};
    owh_overlay *overlay = nullptr;
    assert(owh_overlay_create_pixel_buffer(host, &spec, nullptr, &overlay).status == OWH_OK);
    owh_overlay_info_data source_info = OWH_OVERLAY_INFO_INIT;
    if (malformed) {
        assert(owh_overlay_query(overlay, &source_info).status == OWH_CORRUPT);
        assert(source_info.id == 0 && source_info.size == sizeof(source_info));
        assert(owh_overlay_close(overlay).status == OWH_AGENT_LOST);
        assert(owh_sdk_detach(host).status == OWH_OK);
        serving.join();
        owh_sdk_close(host);
        std::cout << "malformed success response rejected without exposing output\n";
        return 0;
    }

    if (mode == "managed") {
        unsigned char image[20];
        std::memset(image, 0x5a, sizeof(image));
        owh_submission *first = nullptr, *last = nullptr;
        assert(owh_overlay_submit_pixels(overlay, image, sizeof(image), 12, nullptr, nullptr, &first).status == OWH_OK);
        for (unsigned i = 0; i < 50; ++i)
            assert(owh_overlay_submit_pixels(overlay, image, sizeof(image), 12, nullptr, nullptr, nullptr).status == OWH_OK);
        // Reject invalid input before replacing the pending image.
        assert(owh_overlay_submit_pixels(overlay, image, 1, 12, nullptr, nullptr, nullptr).status == OWH_INVALID_ARGUMENT);
        assert(owh_overlay_submit_pixels(overlay, image, sizeof(image), 12, nullptr, nullptr, &last).status == OWH_OK);
        owh_submission_info outcome = OWH_SUBMISSION_INFO_INIT;
        assert(owh_submission_poll(first, &outcome).status == OWH_OK && outcome.state == OWH_SUBMISSION_REPLACED);
        SetEvent(publisher.resume.get());
        owh_wait_options wait{sizeof(wait), 2000, nullptr};
        assert(owh_submission_wait(last, &wait, &outcome).status == OWH_OK && outcome.state == OWH_SUBMISSION_PUBLISHED);
        assert(publisher.published == 1);
        owh_submission_close(first);
        owh_submission_close(last);
        assert(owh_overlay_close(overlay).status == OWH_OK);

        ResetEvent(publisher.resume.get());
        owh_pixel_buffer_overlay_options fifo = OWH_PIXEL_BUFFER_OVERLAY_OPTIONS_INIT;
        fifo.policy = OWH_QUEUE_FIFO;
        assert(owh_overlay_create_pixel_buffer(host, &spec, &fifo, &overlay).status == OWH_OK);
        for (unsigned i = 0; i < 3; ++i)
            assert(owh_overlay_submit_pixels(overlay, image, 16, 0, nullptr, nullptr, nullptr).status == OWH_OK);
        assert(owh_overlay_try_submit_pixels(overlay, image, 16, 0, nullptr, nullptr).status == OWH_BUSY);
        wait.timeout_ms = 20;
        assert(owh_overlay_submit_pixels(overlay, image, 16, 0, nullptr, &wait, nullptr).status == OWH_TIMEOUT);
        owh_cancel *cancel = nullptr;
        assert(owh_cancel_create(&cancel).status == OWH_OK);
        wait.timeout_ms = OWH_WAIT_INFINITE;
        wait.cancellation = cancel;
        auto blocked = std::async(std::launch::async, [&] {
            return owh_overlay_submit_pixels(overlay, image, 16, 0, nullptr, &wait, nullptr);
        });
        assert(blocked.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
        owh_cancel_signal(cancel);
        assert(blocked.get().status == OWH_CANCELLED);
        owh_cancel_close(cancel);
        wait.cancellation = nullptr;
        auto waiting = std::async(std::launch::async, [&] {
            return owh_overlay_submit_pixels(overlay, image, 16, 0, nullptr, &wait, &last);
        });
        assert(waiting.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
        SetEvent(publisher.resume.get());
        assert(waiting.get().status == OWH_OK);
        wait.timeout_ms = 2000;
        assert(owh_submission_wait(last, &wait, &outcome).status == OWH_OK && outcome.state == OWH_SUBMISSION_PUBLISHED);
        assert(publisher.published == 5);
        owh_submission_close(last);
        assert(owh_overlay_close(overlay).status == OWH_OK);
        assert(owh_sdk_detach(host).status == OWH_OK);
        serving.join();
        owh_sdk_close(host);
        std::cout << "managed latest, tracked replacement, strided input, FIFO backpressure/timeout/cancellation passed\n";
        return 0;
    }

    assert(owh_overlay_query(overlay, &source_info).status == OWH_OK && source_info.width == 2);
    auto acquire = [&] {
        owh_frame *frame = nullptr;
        owh_pixel_buffer buffer = OWH_PIXEL_BUFFER_INIT;
        const auto deadline = GetTickCount64() + 1000;
        owh_result acquired{};
        do {
            acquired = owh_frame_acquire(overlay, &frame, &buffer);
            if (acquired.status != OWH_BUSY)
                break;
            SwitchToThread();
        } while (GetTickCount64() < deadline);
        assert(acquired.status == OWH_OK);
        std::memset(buffer.pixels, 0x5a, static_cast<size_t>(buffer.pixel_bytes));
        return frame;
    };
    owh_submission *receipt = nullptr;
    assert(owh_frame_commit(acquire(), &receipt).status == OWH_OK && receipt);
    owh_wait_options wait{sizeof(wait), 5000, nullptr};
    owh_submission_info info = OWH_SUBMISSION_INFO_INIT;
    assert(owh_submission_wait(receipt, &wait, &info).status == OWH_OK &&
           info.state == OWH_SUBMISSION_PUBLISHED);
    owh_submission *limited = nullptr;
    assert(owh_frame_commit(acquire(), &limited).status == OWH_RESOURCE_EXHAUSTED && !limited);
    assert(owh_frame_commit(acquire(), nullptr).status == OWH_OK);
    owh_submission_close(receipt);
    auto *held = acquire();
    assert(owh_overlay_close(overlay).status == OWH_OK);
    // Close consumed the overlay; the issued frame still pins its mapping.
    assert(owh_frame_commit(held, nullptr).status == OWH_CLOSED);
    assert(owh_sdk_detach(host).status == OWH_OK);
    serving.join();
    owh_sdk_close(host);
    std::cout << "public ABI acquire/commit, receipt capacity and close lifecycle passed\n";
    return 0;
}
