#include <windows.h>
#include <overwolf_hook/abi/events.h>
#include <stdio.h>

static int checked(const char *operation, owh_result result) {
    if (result.status == OWH_OK)
        return 1;
    fprintf(stderr, "%s: status=%d stage=%u domain=%u native=0x%llx\n",
            operation, result.status, result.failure_stage, result.error_domain,
            (unsigned long long)result.native_code);
    return 0;
}

// --8<-- [start:handler]
static void handle_event(const owh_event *event) {
    printf(
        "channel=%u rawBytes=%u lost=%llu firstBytes=", event->source_channel,
        event->raw_size, (unsigned long long)event->lost_before);
    for (uint32_t i = 0; i < event->raw_size && i < 16; ++i)
        printf("%02x ", (unsigned)(unsigned char)event->raw[i]);
    puts(event->raw_size > 16 ? "..." : "");
    fflush(stdout);
}
// --8<-- [end:handler]

int main(void) {
    owh_sdk *sdk = NULL;
    owh_subscription *subscription = NULL;
    int attached = 0, exit_code = 1;
    if (!checked("open SDK", owh_sdk_open(&sdk)))
        return 1;
    /* Default restart attach may close and relaunch Overwolf. */
    if (!checked("attach", owh_sdk_attach(sdk, NULL)))
        goto cleanup;
    attached = 1;
    if (!checked("subscribe", owh_event_subscribe(sdk, NULL, &subscription)))
        goto cleanup;

    puts("Listening for 5 minutes. Enter the game and generate events now.");
    fflush(stdout);
    owh_wait_options wait;
    owh_wait_options_init(&wait);
    wait.timeout_ms = 1000;
    ULONGLONG started = GetTickCount64();
    exit_code = 0;
    while (GetTickCount64() - started < 300000) {
        owh_event event = {0};
        owh_result result =
            owh_subscription_receive(subscription, &wait, &event);
        if (result.status == OWH_TIMEOUT || result.status == OWH_BUSY ||
            result.status == OWH_NO_DATA)
            continue;
        if (result.status == OWH_CLOSED) {
            puts("Event stream closed.");
            break;
        }
        if (!checked("receive", result)) {
            exit_code = 1;
            break;
        }
        handle_event(&event);
    }
    puts("Observation ended.");
cleanup:
    if (subscription &&
        !checked("close subscription", owh_subscription_close(subscription)))
        exit_code = 1;
    if (attached && !checked("detach", owh_sdk_detach(sdk)))
        exit_code = 1;
    owh_sdk_close(sdk);
    return exit_code;
}
