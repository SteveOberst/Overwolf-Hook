#include <overwolf_hook/abi/sdk.h>
#include <stdio.h>

int main(void) {
    unsigned abi = owh_sdk_abi_version();
    printf("Overwolf Hook loaded (ABI %u)\n", abi);
    return abi == OWH_ABI_VERSION ? 0 : 1;
}
