#include <overwolf_event_hook/bridge.h>
#include <MinHook.h>
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>

#define REQUIRE(test) do { if (!(test)) throw std::runtime_error(#test); } while (false)

namespace {
using Dispatch = uint32_t(__cdecl*)(uint32_t);
Dispatch original = nullptr;
oeh_writer* writer = nullptr;
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
        if (oeh_writer_publish(writer, &value, sizeof(value)).status == OEH_OK) ++forwarded;
        else ++rejected;
        inside = false;
    }
    SetLastError(original_error);
    return result;
}
struct Experiment {
    HMODULE fixture = nullptr;
    void* target = nullptr;
    bool initialized = false;
    bool created = false;
    bool enabled = false;
    oeh_reader* reader = nullptr;
    ~Experiment() {
        // Test is single-threaded; no callback can be in flight here.
        if (enabled) MH_DisableHook(target);
        if (created) MH_RemoveHook(target);
        if (initialized) MH_Uninitialize();
        oeh_reader_close(reader);
        if (writer) { oeh_writer_close(writer); writer = nullptr; }
        if (fixture) FreeLibrary(fixture);
    }
};
}

int main(int argc, char** argv) {
    try {
        REQUIRE(argc == 2);
        Experiment experiment;
        experiment.fixture = LoadLibraryA(argv[1]); REQUIRE(experiment.fixture);
        experiment.target = reinterpret_cast<void*>(GetProcAddress(experiment.fixture, "fixture_dispatch"));
        REQUIRE(experiment.target);
        const auto dispatch = reinterpret_cast<Dispatch>(experiment.target);
        const uint32_t expected = (7u ^ 0x5a5a5a5au) + 17u;
        REQUIRE(dispatch(7) == expected);
        const auto name = "hook-" + std::to_string(GetCurrentProcessId());
        REQUIRE(oeh_writer_create(1, name.c_str(), 8, 16, &writer).status == OEH_OK);
        REQUIRE(oeh_reader_open(1, name.c_str(), OEH_RETAINED, &experiment.reader).status == OEH_OK);
        REQUIRE(MH_Initialize() == MH_OK); experiment.initialized = true;
        REQUIRE(MH_CreateHook(experiment.target, reinterpret_cast<void*>(&intercepted),
            reinterpret_cast<void**>(&original)) == MH_OK); experiment.created = true;
        REQUIRE(MH_EnableHook(experiment.target) == MH_OK); experiment.enabled = true;
        REQUIRE(dispatch(7) == expected && forwarded == 1 && rejected == 0);
        REQUIRE(GetLastError() == 1234);
        uint32_t received = 0; oeh_record record{};
        REQUIRE(oeh_reader_next(experiment.reader, &received, sizeof(received), &record).status == OEH_OK);
        REQUIRE(received == 7 && record.sequence == 1);
        // A broken capture path must not change the original function's result.
        REQUIRE(oeh_writer_close(writer).status == OEH_OK); writer = nullptr;
        REQUIRE(dispatch(7) == expected && rejected == 1);
        REQUIRE(GetLastError() == 1234);
        REQUIRE(MH_DisableHook(experiment.target) == MH_OK); experiment.enabled = false;
        REQUIRE(dispatch(7) == expected && forwarded == 1 && rejected == 1);
        REQUIRE(oeh_reader_next(experiment.reader, &received, sizeof(received), &record).status == OEH_CLOSED);
        std::cout << "PASS trampoline, raw forwarding, failure isolation, disable\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}
