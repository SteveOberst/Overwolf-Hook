#include <overwolf_hook/abi/sdk.h>
#include <stdio.h>

int main(void) {
    owh_result result;
    owh_client_sanity_report report = {0};
    report.size = sizeof(report);
    result = owh_sdk_check(NULL, &report);
    if (result.status != OWH_OK) {
        fprintf(stderr, "Host check failed: %d\n", result.status);
        return 2;
    }
    printf("Checked: 0x%08x\n", report.checked);
    printf("Passed:  0x%08x\n", report.passed);
    printf("Failed:  0x%08x\n", report.checked & ~report.passed);
    return 0;
}
