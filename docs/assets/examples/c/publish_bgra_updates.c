#include <overwolf_hook/abi/overlays.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int checked(const char *operation, owh_result result) {
    if (result.status == OWH_OK)
        return 1;
    fprintf(stderr, "%s: status=%d stage=%u native=0x%llx\n",
            operation, result.status, result.failure_stage,
            (unsigned long long)result.native_code);
    return 0;
}

int main(void) {
    enum { width = 256, height = 128 };
    owh_sdk *sdk = NULL;
    owh_overlay *overlay = NULL;
    int attached = 0, success = 0;
    if (!checked("open SDK", owh_sdk_open(&sdk)))
        return 1;
    if (!checked("attach", owh_sdk_attach(sdk, NULL)))
        goto cleanup;
    attached = 1;

    owh_overlay_spec spec;
    owh_overlay_spec_init(&spec);
    spec.width = width;
    spec.height = height;
    spec.left = 64;
    spec.top = 64;
    if (!checked("create overlay",
                 owh_overlay_create_pixel_buffer(sdk, &spec, NULL, &overlay)))
        goto cleanup;

    unsigned char image[width * height * 4];
    for (int update = 0; update < 2; ++update) {
        // --8<-- [start:draw]
        /* A complete image: clear the previous square, then draw the next. */
        memset(image, 0, sizeof(image));
        int square_left = 16 + update * 160;
        for (int y = 32; y < 96; ++y) {
            for (int x = square_left; x < square_left + 64; ++x) {
                unsigned char *pixel = image + (y * width + x) * 4;
                pixel[0] = 0x20; /* B */
                pixel[1] = 0x80; /* G */
                pixel[2] = 0xff; /* R */
                pixel[3] = 0xff; /* A */
            }
        }
        // --8<-- [end:draw]
        // --8<-- [start:submit]
        if (!checked("submit image", owh_overlay_submit_pixels(
                overlay, image, sizeof(image), 0, NULL, NULL, NULL)))
            goto cleanup;
        /* image is reusable now; no acquire/commit or busy retry loop. */
        // --8<-- [end:submit]
        puts(update == 0
            ? "First image accepted. Enter the game, then press Enter here "
              "to move the square."
            : "Second image accepted. Press Enter to close the overlay.");
        (void)getchar();
    }
    success = 1;

cleanup:
    if (overlay && !checked("close overlay", owh_overlay_close(overlay)))
        success = 0;
    if (attached && !checked("detach", owh_sdk_detach(sdk)))
        success = 0;
    owh_sdk_close(sdk);
    return success ? 0 : 1;
}
