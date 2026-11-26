#include <overwolf_hook/abi/events.h>
#include <overwolf_hook/abi/game_state.h>
#include <overwolf_hook/abi/overlays.h>
#include <overwolf_hook/abi/sdk.h>

#include <stdio.h>

int main(void) {
    owh_sdk *sdk = NULL;
    if (owh_sdk_abi_version() != OWH_ABI_VERSION)
        return 1;
    if (owh_sdk_open(&sdk).status != OWH_OK || sdk == NULL)
        return 2;

    owh_client_options options;
    owh_client_options_init(&options);
    if (options.size != sizeof(options) || options.abi_version != OWH_ABI_VERSION) {
        owh_sdk_close(sdk);
        return 3;
    }
    owh_sdk_close(sdk);
    puts("PASS installed C SDK context, headers, and native defaults");
    return 0;
}
