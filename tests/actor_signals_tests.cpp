#include <array>
#include <iostream>
#include <owh/platform/actor_signals.hpp>
#include <stdexcept>

namespace {
std::array<HANDLE, 4> opened{};
unsigned calls = 0;
unsigned fail_at = 0;
struct StartupFailure {};
HANDLE WINAPI create(LPSECURITY_ATTRIBUTES a, BOOL manual, BOOL initial, LPCWSTR name) {
    const auto index = calls++;
    if (index == fail_at)
        return nullptr;
    return opened[index] = CreateEventW(a, manual, initial, name);
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void closed() {
    for (const auto handle : opened) {
        if (!handle)
            continue;
        DWORD flags = 0;
        require(!GetHandleInformation(handle, &flags) && GetLastError() == ERROR_INVALID_HANDLE,
                "partially acquired actor event leaked");
    }
}
} // namespace
int main() {
    try {
        for (fail_at = 0; fail_at != 4; ++fail_at) {
            opened.fill(nullptr);
            calls = 0;
            bool failed = false;
            try {
                overwolf_hook::platform::ActorSignals signals(create);
            } catch (const std::bad_alloc &) {
                failed = true;
            }
            require(failed && calls == 4, "event creation failure was not propagated");
            closed();
        }
        fail_at = 4;
        calls = 0;
        opened.fill(nullptr);
        try {
            overwolf_hook::platform::ActorSignals signals(create);
            require(signals.command && signals.stop && signals.ready && signals.source,
                    "events not acquired");
            throw StartupFailure{};
        } catch (const StartupFailure &) {
        }
        closed();
        std::cout << "PASS partial event construction and post-construction "
                     "startup failure cleanup\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
