#include <overwolf_hook/abi/overlays.h>

owh_result submit_and_wait(owh_overlay *overlay, const void *image,
                           uint64_t bytes, owh_submission_info *outcome) {
    owh_submission *receipt = NULL;
    owh_result result = owh_overlay_submit_pixels(overlay, image, bytes, 0, NULL, NULL, &receipt);
    if (result.status != OWH_OK) return result;
    owh_wait_options wait;
    owh_wait_options_init(&wait);
    wait.timeout_ms = 1000;
    outcome->size = sizeof(*outcome);
    result = owh_submission_wait(receipt, &wait, outcome);
    owh_submission_close(receipt);
    /* Only OK makes outcome usable. Its nested result describes publication. */
    return result;
}
