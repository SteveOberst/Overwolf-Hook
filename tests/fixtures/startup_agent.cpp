#include <windows.h>
#include <array>
#include <cstdint>
#include <stdexcept>

struct Root {
    uint64_t *vtable;
    std::array<std::byte, 0x2b0> padding{};
    uint64_t refresher = 1;
};
static_assert(offsetof(Root, refresher) == 0x2b8);
extern "C" __declspec(dllexport) uint64_t fixture_root = 0;
extern "C" __declspec(dllexport) volatile LONG fixture_calls = 0;
extern "C" __declspec(dllexport) uint64_t fixture_last_init = 0;
extern "C" __declspec(dllexport) __declspec(noinline) uint64_t fixture_init(void *, uint32_t) {
    InterlockedIncrement(&fixture_calls);
    wchar_t mode[20]{};
    GetEnvironmentVariableW(L"OWH_STARTUP_FIXTURE", mode, 20);
    return mode[0] == L'f' ? 0xfedcba9876543200ull : 0xfedcba9876543201ull;
}
extern "C" __declspec(dllexport) __declspec(noinline) void fixture_clear() {
    InterlockedIncrement(&fixture_calls);
}
extern "C" __declspec(dllexport) __declspec(noinline) void runAgent(uint32_t) {
    wchar_t mode[20]{};
    GetEnvironmentVariableW(L"OWH_STARTUP_FIXTURE", mode, 20);
    if (mode[0] == L'n')
        return; // factory returned null
    static uint64_t vtable[]{0, 0, reinterpret_cast<uint64_t>(&fixture_init), 0,
                             reinterpret_cast<uint64_t>(&fixture_clear)};
    static Root root{vtable};
    root.refresher = 1;
    fixture_root = reinterpret_cast<uint64_t>(&root);
    if (mode[0] == L't')
        throw std::runtime_error("owned initialization failure");
    if (mode[0] != L'm') {
        // Match runAgent's indirect vtable call; a failed Init leaves a root
        // with a non-null refresher, and runAgent ignores Init's success byte.
        using Init = uint64_t (*)(void *, uint32_t);
        fixture_last_init = reinterpret_cast<Init>(root.vtable[2])(&root, GetCurrentProcessId());
    }
    InterlockedIncrement(&fixture_calls);
}
extern "C" __declspec(dllexport) __declspec(noinline) void clearAgent() {
    // Keep structurally valid fields after teardown: shape alone must never
    // reopen admission without a newly observed successful initialization.
    InterlockedIncrement(&fixture_calls);
}
extern "C" __declspec(dllexport) __declspec(noinline) void unhookAgent() {
    // Also retain the old root after unhook to exercise readiness revocation.
    InterlockedIncrement(&fixture_calls);
}
