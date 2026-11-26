#include <overwolf_hook/abi/overlays.h>
#include <overwolf_hook/abi/sdk.h>
#include <stdio.h>
#include <string.h>

static int checked(const char *operation, owh_result result) {
    if (result.status == OWH_OK)
        return 1;
    fprintf(stderr, "%s: status=%d stage=%u domain=%u native=0x%llx\n",
            operation, result.status, result.failure_stage, result.error_domain,
            (unsigned long long)result.native_code);
    return 0;
}

static int publish_panel(owh_overlay *overlay) {
    unsigned char image[256 * 128 * 4] = {0};
    for (uint32_t y = 16; y < 112; ++y) {
        for (uint32_t x = 16; x < 240; ++x) {
            unsigned char *pixel = image + (size_t)y * 256 * 4 + x * 4;
            pixel[0] = 0x20; /* B */
            pixel[1] = 0x80; /* G */
            pixel[2] = 0xff; /* R */
            pixel[3] = 0xff; /* A */
        }
    }
    return checked("submit image", owh_overlay_submit_pixels(overlay, image, sizeof(image), 0, NULL, NULL, NULL));
}

int main(void) {
    owh_sdk *sdk = NULL;
    if (!checked("open SDK", owh_sdk_open(&sdk)))
        return 1;

    int success = 1;
    int attached = 0;
    owh_client_options options;
    owh_client_options_init(&options);
    owh_client_sanity_report report = {0};
    report.size = sizeof(report);
    success = checked("check client", owh_sdk_check(&options, &report));
    if (success) {
        printf("Host checks: checked=0x%x passed=0x%x\n", report.checked,
               report.passed);
        /* Initialized options select OWH_ATTACH_RESTART; attach may close and
         * relaunch Overwolf. */
        success = checked("attach", owh_sdk_attach(sdk, &options));
        attached = success;
    }

    owh_overlay *overlay = NULL;
    if (success) {
        owh_overlay_spec spec;
        owh_overlay_spec_init(&spec);
        spec.width = 256;
        spec.height = 128;
        spec.left = 64;
        spec.top = 64;
        success =
            checked("create pixel source",
                    owh_overlay_create_pixel_buffer(sdk, &spec, NULL, &overlay));
    }
    if (success)
        success = publish_panel(overlay);
    if (success) {
        puts("Image submitted. Check the game window, then press Enter to "
             "close.");
        (void)getchar();
    }

    if (overlay) {
        owh_overlay *closing = overlay;
        overlay = NULL; /* Close consumes the handle even when cleanup reports
                           an error. */
        success =
            checked("close overlay", owh_overlay_close(closing)) && success;
    }
    if (attached)
        success = checked("detach", owh_sdk_detach(sdk)) && success;
    owh_sdk_close(sdk);
    return success ? 0 : 1;
}
