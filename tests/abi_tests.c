#include <overwolf_hook/abi/events.h>
#include <overwolf_hook/abi/game_state.h>
#include <overwolf_hook/abi/overlays.h>
#include <overwolf_hook/abi/sdk.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(owh_result) == 24, "result");
_Static_assert(offsetof(owh_result, native_code) == 16, "native code");
_Static_assert(sizeof(owh_client_options) == 56, "client options");
_Static_assert(sizeof(owh_client_sanity_report) == 160, "preflight");
_Static_assert(sizeof(owh_build_info) == 136, "build info");
_Static_assert(sizeof(owh_agent_info) == 176, "agent info");
_Static_assert(sizeof(owh_overlay_spec) == 20, "overlay spec");
_Static_assert(sizeof(owh_pixel_region) == 16, "pixel region");
_Static_assert(sizeof(owh_pixel_buffer) == 40, "pixel buffer");
_Static_assert(offsetof(owh_pixel_buffer, pixels) == 8, "pixel pointer");
_Static_assert(sizeof(owh_submission_info) == 32, "submission info");
_Static_assert(sizeof(owh_legacy_shared_texture) == 24, "shared texture");
_Static_assert(sizeof(owh_event) == 64808, "event");
_Static_assert(sizeof(owh_subscription_info) == 64, "subscription info");

int main(void) {
    owh_build_info build = {0};
    if (owh_sdk_build_info(&build).status != OWH_OK || !build.sdk_version[0] ||
        !memchr(build.sdk_version, 0, sizeof(build.sdk_version)))
        return 5;

    owh_sdk *sdk = NULL;
    if (owh_sdk_abi_version() != OWH_ABI_VERSION)
        return 1;
    if (owh_sdk_open(&sdk).status != OWH_OK || sdk == NULL)
        return 2;

    owh_client_options options;
    owh_client_options_init(&options);
    if (options.size != sizeof(options) || options.abi_version != OWH_ABI_VERSION ||
        options.attach_mode != OWH_ATTACH_RESTART) {
        owh_sdk_close(sdk);
        return 3;
    }

    owh_overlay_spec spec;
    owh_overlay_spec_init(&spec);
    if (spec.size != sizeof(spec)) {
        owh_sdk_close(sdk);
        return 4;
    }
    owh_pixel_region damage = {1, 2, 3, 4};
    owh_pixel_region_init(&damage);
    if (damage.x || damage.y || damage.width || damage.height)
        return 6;
    if (owh_frame_set_damage(NULL, &damage).status != OWH_INVALID_ARGUMENT)
        return 7;

    owh_sdk_close(sdk);
    return 0;
}
