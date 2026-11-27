#include <MinHook.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <windows.h>

#define REQUIRE(test)                                                                              \
    do {                                                                                           \
        if (!(test))                                                                               \
            throw std::runtime_error(#test);                                                       \
    } while (false)

namespace {
using Dispatch = uint32_t(__cdecl *)(uint32_t);
Dispatch original = nullptr;
uint32_t calls = 0;

// Dependency smoke only. Actual event capture is tested through EventSource.
uint32_t __cdecl intercepted(uint32_t value) noexcept {
    ++calls;
    return original(value);
}
struct Experiment {
    HMODULE fixture = nullptr;
    void *target = nullptr;
    bool initialized = false;
    bool created = false;
    bool enabled = false;
    ~Experiment() {
        // Test is single-threaded; no callback can be in flight here.
        if (enabled)
            MH_DisableHook(target);
        if (created)
            MH_RemoveHook(target);
        if (initialized)
            MH_Uninitialize();
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
        REQUIRE(MH_Initialize() == MH_OK);
        experiment.initialized = true;
        REQUIRE(MH_CreateHook(experiment.target, reinterpret_cast<void *>(&intercepted),
                              reinterpret_cast<void **>(&original)) == MH_OK);
        experiment.created = true;
        REQUIRE(MH_EnableHook(experiment.target) == MH_OK);
        experiment.enabled = true;
        REQUIRE(dispatch(7) == expected && calls == 1);
        REQUIRE(GetLastError() == 1234);
        REQUIRE(MH_DisableHook(experiment.target) == MH_OK);
        experiment.enabled = false;
        REQUIRE(dispatch(7) == expected && calls == 1);
        std::cout << "PASS MinHook detour, original trampoline and disable (owned fixture)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
