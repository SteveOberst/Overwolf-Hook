#include <stdint.h>
#include <windows.h>

// Owned fixture, deliberately unrelated to any inferred Overwolf ABI.
extern "C" __declspec(dllexport) __declspec(noinline) uint32_t __cdecl
fixture_dispatch(uint32_t value) {
    volatile uint32_t mixed = value ^ 0x5a5a5a5a;
    SetLastError(1234);
    return mixed + 17;
}
