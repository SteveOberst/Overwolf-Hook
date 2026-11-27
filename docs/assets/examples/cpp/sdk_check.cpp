#include <overwolf_hook/overwolf_hook.hpp>

int main() {
    auto sdk = overwolf_hook::OverwolfHook::open();
    return 0; // RAII closes this unattached context.
}
