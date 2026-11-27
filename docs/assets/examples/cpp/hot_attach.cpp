#include <overwolf_hook/overwolf_hook.hpp>

// Advanced HOT mode accepts an initial setup race that can crash Overwolf.
void hot_attach(overwolf_hook::OverwolfHook &sdk) {
    // --8<-- [start:attach]
    owh_client_options options{};
    owh_client_options_init(&options);
    options.attach_mode = OWH_ATTACH_HOT;
    sdk.attach(&options);
    // --8<-- [end:attach]
}
