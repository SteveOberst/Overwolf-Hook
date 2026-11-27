#include <windows.h>
#include <cstdint>
extern "C" __declspec(dllexport) bool fixture_load(const wchar_t *path) {
    auto module = LoadLibraryW(path);
    if (!module) return false;
    auto run = reinterpret_cast<void (*)(uint32_t)>(GetProcAddress(module,"runAgent"));
    if (!run) return false;
    run(GetCurrentProcessId());
    return true;
}
