#include <overwolf_hook/abi/sdk.h>
#include <inttypes.h>
#include <stdio.h>

int main(void) {
    owh_build_info info = {0};
    owh_result result = owh_sdk_build_info(&info);
    if (result.status != OWH_OK) {
        fprintf(stderr, "SDK load check failed (status %u)\n", result.status);
        return 1;
    }
    printf("Overwolf Hook %s loaded (build %" PRIu64 ")\n",
           info.sdk_version, info.build_number);
    return 0;
}
