#include <overwolf_hook/overwolf_hook.hpp>

#include <iostream>

int main() {
    using namespace overwolf_hook;
    if (OverwolfHook::abi_version() != OWH_ABI_VERSION)
        return 1;

    auto sdk = OverwolfHook::open();
    auto overlays = sdk.overlays();
    auto events = sdk.events();
    auto state = sdk.game_state();
    (void)overlays;
    (void)events;
    (void)state;

    sdk.close();
    std::cout << "PASS installed C++ SDK controllers and context close\n";
}
