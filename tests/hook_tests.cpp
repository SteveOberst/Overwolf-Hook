#include <MinHook.h>
#include <cstring>
#include <iostream>
#include <owh/ipc/event_log.hpp>
#include <stdexcept>
#include <string>
#include <windows.h>

#define REQUIRE(test)                                                                              \
    do {                                                                                           \
        if (!(test))                                                                               \
            throw std::runtime_error(#test);                                                       \
    } while (false)

namespace {
using Dispatch = uint32_t(__cdecl *)(uint32_t);
Dispatch original = nullptr;
std::unique_ptr<overwolf_hook::ipc::EventLog> writer;
uint32_t forwarded = 0;
uint32_t rejected = 0;
thread_local bool inside = false;

uint32_t __cdecl intercepted(uint32_t value) noexcept {
    // Fixture-only proof: preserve the actual return, never let transport failure
    // replace it. This fixture tests MinHook/transport behavior independently of
    // the private consumer ABI exercised by the optional owned-host test.
    const auto result = original(value);
    const DWORD original_error = GetLastError();
    if (!inside) {
        inside = true;
        overwolf_hook::core::CapturedEvent payload{};
        payload.raw_size = sizeof(payload.raw);
        std::memcpy(payload.raw, &value, sizeof(value));
        if (writer && writer->publish(payload).status == OWH_OK)
            ++forwarded;
        else
            ++rejected;
        inside = false;
    }
    SetLastError(original_error);
    return result;
}
struct Experiment {
    HMODULE fixture = nullptr;
    void *target = nullptr;
    bool initialized = false;
    bool created = false;
    bool enabled = false;
    std::unique_ptr<overwolf_hook::ipc::EventCursor> reader;
    ~Experiment() {
        // Test is single-threaded; no callback can be in flight here.
        if (enabled)
            MH_DisableHook(target);
        if (created)
            MH_RemoveHook(target);
        if (initialized)
            MH_Uninitialize();
        reader.reset();
        if (writer) {
            (void)writer->close();
            writer.reset();
        }
        if (fixture)
            FreeLibrary(fixture);
    }
};
} // namespace

int main(int argc, char **argv) {
    try {
        REQUIRE(argc == 2);
        Experiment experiment;
        experiment.fixture = LoadLibraryA(argv[1]);
        REQUIRE(experiment.fixture);
        experiment.target =
            reinterpret_cast<void *>(GetProcAddress(experiment.fixture, "fixture_dispatch"));
        REQUIRE(experiment.target);
        const auto dispatch = reinterpret_cast<Dispatch>(experiment.target);
        const uint32_t expected = (7u ^ 0x5a5a5a5au) + 17u;
        REQUIRE(dispatch(7) == expected);
        const auto name = "hook-" + std::to_string(GetCurrentProcessId());
        writer = overwolf_hook::ipc::EventLog::create(name.c_str(), 8);
        overwolf_hook::Handle wake(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        overwolf_hook::Handle closed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        auto opened = overwolf_hook::ipc::EventCursor::open(name.c_str(), writer->position(), 0,
            GetCurrentProcess(), reinterpret_cast<uint64_t>(wake.get()), reinterpret_cast<uint64_t>(closed.get()));
        REQUIRE(opened.first.status == OWH_OK);
        experiment.reader = std::move(opened.second);
        REQUIRE(MH_Initialize() == MH_OK);
        experiment.initialized = true;
        REQUIRE(MH_CreateHook(experiment.target, reinterpret_cast<void *>(&intercepted),
                              reinterpret_cast<void **>(&original)) == MH_OK);
        experiment.created = true;
        REQUIRE(MH_EnableHook(experiment.target) == MH_OK);
        experiment.enabled = true;
        REQUIRE(dispatch(7) == expected && forwarded == 1 && rejected == 0);
        REQUIRE(GetLastError() == 1234);
        owh_event record{};
        REQUIRE(experiment.reader->poll(record).status == OWH_OK);
        uint32_t received = 0;
        std::memcpy(&received, record.raw, sizeof(received));
        REQUIRE(received == 7 && record.sequence == 1);
        // A broken capture path must not change the original function's result.
        REQUIRE(writer->close(100).status == OWH_OK);
        writer.reset();
        REQUIRE(dispatch(7) == expected && rejected == 1);
        REQUIRE(GetLastError() == 1234);
        REQUIRE(MH_DisableHook(experiment.target) == MH_OK);
        experiment.enabled = false;
        REQUIRE(dispatch(7) == expected && forwarded == 1 && rejected == 1);
        REQUIRE(experiment.reader->poll(record).status == OWH_CLOSED);
        std::cout << "PASS trampoline, raw forwarding, failure isolation, disable\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
