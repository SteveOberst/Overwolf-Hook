#include <overwolf_hook/abi/overlays.h>
#include <stdio.h>

/* Call from your renderer after its GPU work has completed. The caller keeps
 * the allocation alive and unchanged until this function returns. */
int show_completed_texture(uint64_t token_bits, uint32_t luid_low,
                           int32_t luid_high, uint32_t width,
                           uint32_t height) {
    owh_sdk *sdk = NULL;
    owh_overlay *overlay = NULL;
    owh_result result = owh_sdk_open(&sdk);
    int attached = 0;
    if (result.status != OWH_OK)
        goto cleanup;

    // --8<-- [start:attach]
    owh_client_options options;
    owh_client_options_init(&options);
    options.experimental_features = OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE;
    result = owh_sdk_attach(sdk, &options);
    if (result.status != OWH_OK)
        goto cleanup;
    // --8<-- [end:attach]
    attached = 1;

    // --8<-- [start:bind]
    owh_overlay_spec spec;
    owh_overlay_spec_init(&spec);
    spec.width = width;
    spec.height = height;
    spec.left = 64;
    spec.top = 64;
    owh_legacy_shared_texture texture;
    owh_legacy_shared_texture_init(&texture);
    texture.legacy_shared_token = token_bits;
    texture.adapter_luid_low = luid_low;
    texture.adapter_luid_high = luid_high;
    result = owh_overlay_create_shared_texture(sdk, &spec, &texture,
                                               &overlay);
    if (result.status != OWH_OK)
        goto cleanup;
    // --8<-- [end:bind]
    puts("Texture accepted. Enter the game, then press Enter here to close.");
    (void)getchar();

cleanup:
    if (overlay) {
        owh_result closed = owh_overlay_close(overlay);
        overlay = NULL; // Consumed even if close reports an error.
        if (closed.status != OWH_OK && result.status == OWH_OK)
            result = closed;
    }
    if (attached) {
        owh_result detached = owh_sdk_detach(sdk);
        if (detached.status != OWH_OK && result.status == OWH_OK)
            result = detached;
    }
    if (sdk)
        owh_sdk_close(sdk);
    if (result.status != OWH_OK)
        fprintf(stderr, "SDK failure: status=%d stage=%u native=0x%llx\n",
                result.status, result.failure_stage,
                (unsigned long long)result.native_code);
    return result.status == OWH_OK ? 0 : 1;
}
