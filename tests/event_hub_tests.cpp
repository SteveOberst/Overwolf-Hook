#include <windows.h>
#include "../tools/capture_recording.hpp"
#include "events/event_hub.hpp"
#include <overwolf_hook/types/common.h>
#include <overwolf_hook/types/events.h>
#include <owh/ipc/event_log.hpp>
#include <owh/overwolf/signatures/event_resolver.hpp>
#include <owh/overwolf/signatures/event_targets.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define REQUIRE(test)                                                                              \
    do {                                                                                           \
        if (!(test))                                                                               \
            throw std::runtime_error(#test);                                                       \
    } while (false)

namespace {
namespace profile = overwolf_hook::overwolf::signatures::events;
using DispatchEvents = void(__fastcall *)(void *, profile::EventRange *);
template <typename T> void put(void *base, size_t offset, T value) {
    std::memcpy(static_cast<char *>(base) + offset, &value, sizeof(value));
}
uint32_t original_callbacks = 0;
void __cdecl native_event(uint32_t channel, uint32_t count, uint32_t index,
                         char *, char *payload, uint32_t) {
    REQUIRE(channel == 900001 && count == 2 && index < 2);
    ++original_callbacks;
    payload[1] = 17; // SDK captured a copy before the original callback mutates it.
}
struct Fixture {
    HMODULE consumer = nullptr;
    DispatchEvents dispatch = nullptr;
    alignas(8) std::array<unsigned char, 16> delegate{};
    std::vector<unsigned char> output = std::vector<unsigned char>(2 * profile::raw_event_bytes);
    profile::EventRange range{};
    void load(const char *path) {
        consumer = LoadLibraryA(path);
        REQUIRE(consumer);
        dispatch = reinterpret_cast<DispatchEvents>(profile::resolve_event_dispatcher(consumer));
        using Init = void(__cdecl *)(void *, void *, void *, void *, void *, void *);
        auto init = reinterpret_cast<Init>(GetProcAddress(consumer, "InitializeCallbacks"));
        REQUIRE(init);
        // Register only in this owned fixture process, never in product code.
        init(nullptr, nullptr, nullptr, reinterpret_cast<void *>(&native_event), nullptr, nullptr);
        put(delegate.data(), 8, uint32_t{900001});
        put(delegate.data(), 12, GetCurrentProcessId());
    }
    void populate() {
        std::fill(output.begin(), output.end(), 0);
        for (size_t i = 0; i < 2; ++i) {
            auto *raw = output.data() + i * profile::raw_event_bytes;
            const char event[] = "fixture.native-capture";
            std::memcpy(raw, event, sizeof(event));
            raw[255] = static_cast<unsigned char>(0xf0 + i);
            raw[256] = 0;
            raw[257] = 255;
        }
        range = {reinterpret_cast<uintptr_t>(output.data()),
                 reinterpret_cast<uintptr_t>(output.data() + output.size()),
                 reinterpret_cast<uintptr_t>(output.data() + output.size())};
    }
};
void emit(Fixture &fixture) {
    fixture.populate();
    const auto before = original_callbacks;
    fixture.dispatch(fixture.delegate.data(), &fixture.range);
    REQUIRE(original_callbacks == before + 2);
    REQUIRE(fixture.output[256] == 17);
}

auto cursor(const overwolf_hook::agent::events::SubscriptionStream &stream, uint32_t filter = 0) {
    auto [status, value] = overwolf_hook::ipc::EventCursor::open(
        stream.channel.c_str(), stream.position, filter, GetCurrentProcess(), stream.wake_handle,
        stream.closed_handle);
    REQUIRE(status.status == OWH_OK);
    return std::move(value);
}
void receive(overwolf_hook::ipc::EventCursor &cursor, uint32_t timeout = 1000) {
    owh_event event{};
    REQUIRE(cursor.receive(timeout, nullptr, event).status == OWH_OK);
    REQUIRE(event.source_channel == 900001 && event.producer_process_id == GetCurrentProcessId() && event.raw_size == profile::raw_event_bytes);
    REQUIRE(event.raw[256] == 0 && event.raw[257] == 255);
}
void emit_until_delivered(Fixture &fixture, overwolf_hook::ipc::EventCursor &cursor) {
    // Capture intentionally drops on contention. Exercise eventual live delivery,
    // without introducing a product-side lossless barrier for this test.
    for (unsigned attempt = 0; attempt != 20; ++attempt) {
        emit(fixture);
        owh_event event{};
        const auto result = cursor.receive(50, nullptr, event);
        if (result.status == OWH_TIMEOUT)
            continue;
        REQUIRE(result.status == OWH_OK);
        REQUIRE(event.source_channel == 900001 && event.producer_process_id == GetCurrentProcessId() && event.raw_size == profile::raw_event_bytes);
        REQUIRE(event.raw[256] == 0 && event.raw[257] == 255);
        return;
    }
    throw std::runtime_error("no live event delivered within fixture deadline");
}
void prepared_export(Fixture &fixture) {
    struct Capture {
        uint32_t count = 0;
        static void captured(void *context, const overwolf_hook::core::CapturedEvent &) noexcept {
            ++static_cast<Capture *>(context)->count;
        }
    } capture;
    overwolf_hook::overwolf::EventSource source;
    REQUIRE(source.prepare(&Capture::captured, &capture).status == OWH_OK);
    REQUIRE(!source.metrics().enabled);
    emit(fixture);
    REQUIRE(capture.count == 0);
    source.set_enabled(true);
    emit(fixture);
    REQUIRE(capture.count == 2);
    source.set_enabled(false);
    REQUIRE(source.prepare(&Capture::captured, &capture).status == OWH_OK);
    REQUIRE(!source.metrics().enabled);
    emit(fixture);
    REQUIRE(capture.count == 2);
    source.set_enabled(true);
    emit(fixture);
    REQUIRE(capture.count == 4);
    source.shutdown();
    emit(fixture);
    REQUIRE(capture.count == 4 && !source.metrics().enabled);
}
} // namespace
int main(int argc, char **argv) {
    try {
        REQUIRE(argc == 2 || argc == 3);
        {
            overwolf_hook::overwolf::EventSource pending;
            REQUIRE(pending.capability().status == OWH_AGENT_NOT_READY);
        }
        Fixture fixture;
        fixture.load(argv[1]);
        if (argc == 3) {
            prepared_export(fixture);
            return 0;
        }
        overwolf_hook::overwolf::EventSource source;
        overwolf_hook::agent::events::EventHub hub(source);
        auto [a, first] = hub.subscribe({1, 1});
        auto [b, second] = hub.subscribe({2, 1});
        REQUIRE(a.status == OWH_OK && b.status == OWH_OK);
        REQUIRE(first.channel == second.channel); // one Agent mapping
        auto reader = cursor(first);
        auto peer = cursor(second);
        auto filtered = cursor(second, 900002);
        owh_subscription_info info{};
        REQUIRE(hub.query({1, 1}, info).status == OWH_OK && info.qpc_frequency && info.started_qpc);
        {
            overwolf_hook::overwolf::EventSource other_source;
            overwolf_hook::agent::events::EventHub other(other_source);
            REQUIRE(other.subscribe({3, 1}).first.status == OWH_ALREADY_EXISTS);
        }
        emit_until_delivered(fixture, *reader);
        receive(*peer);
        owh_event out{};
        REQUIRE(filtered->poll(out).status == OWH_NO_DATA);
        REQUIRE(hub.close({1, 1}).status == OWH_OK);
        REQUIRE(reader->poll(out).status == OWH_CLOSED);
        emit_until_delivered(fixture, *peer);
        REQUIRE(hub.cleanup_owner(2).status == OWH_OK);
        REQUIRE(peer->poll(out).status == OWH_CLOSED);
        REQUIRE(!source.metrics().enabled); // no subscriptions => no capture
        auto log = overwolf_hook::ipc::EventLog::open(first.channel.c_str());
        const auto inactive = log->position();
        emit(fixture);
        Sleep(25);
        REQUIRE(log->position().next == inactive.next); // no background history
        auto [c, third] = hub.subscribe({4, 1});
        REQUIRE(c.status == OWH_OK);
        auto fresh = cursor(third);
        REQUIRE(fresh->poll(out).status == OWH_NO_DATA);
        emit_until_delivered(fixture, *fresh);
        REQUIRE(hub.close({4, 1}).status == OWH_OK);
        REQUIRE(!source.metrics().enabled && !hub.has_children(4));
        std::cout
            << "PASS shared live ring, isolation, export activation and no inactive history\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
