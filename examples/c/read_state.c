#include <overwolf_hook/state.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* One bounded passive read. No hook or running controller is required. */
int main(int argc, char** argv) {
    char* end = NULL;
    errno = 0;
    const unsigned long long channel = argc == 2 ? strtoull(argv[1], &end, 10) : 0;
    if (argc != 2 || errno || !end || *end || !channel || channel > UINT32_MAX) {
        fprintf(stderr, "Usage: read_state <original Overwolf source channel>\n");
        return 2;
    }
    owh_state* state = NULL;
    owh_result result = owh_state_read(OWH_ABI_VERSION, (uint32_t)channel, 100, &state);
    if (result.status != OWH_OK) {
        fprintf(stderr, "Read: status=%d native_error=%u\n", result.status, result.native_error);
        return 1;
    }
    printf("source_channel=%u fields=%u\n", (uint32_t)channel, owh_state_count(state));
    for (uint32_t i = 0; i < owh_state_count(state); ++i) {
        owh_state_field field = {0};
        result = owh_state_at(state, i, &field);
        if (result.status != OWH_OK) { owh_state_close(state); return 1; }
        printf("%s.%s ticks=%llu size=%u reserved=%u hex=", field.category, field.key,
            (unsigned long long)field.producer_ticks, field.size, field.reserved_bytes);
        for (uint32_t n = 0; n < field.size; ++n) printf("%02x", ((const unsigned char*)field.bytes)[n]);
        puts("");
    }
    owh_state_close(state);
    return 0;
}
