#include <overwolf_hook/probe.h>
#include <windows.h>
#include <stdio.h>
#include <stddef.h>

_Static_assert(sizeof(owh_result) == 8, "result ABI");
_Static_assert(sizeof(owh_record) == 24, "record ABI");
_Static_assert(offsetof(owh_record, size) == 16, "record ABI offsets");

/* Reads the channel printed by owh_probe. Never installs a hook itself. */
int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: read_events <channel printed by owh_probe>\n");
        return 2;
    }
    owh_reader* reader = NULL;
    owh_result result = owh_reader_open(OWH_ABI_VERSION, argv[1], OWH_RETAINED, &reader);
    if (result.status != OWH_OK) {
        fprintf(stderr, "Open: status=%d native_error=%u\n", result.status, result.native_error);
        return 1;
    }
    int exit_code = 0;
    for (;;) {
        owh_probe_frame frame = {0};
        owh_record record = {0};
        result = owh_reader_receive(reader, &frame, sizeof(frame), OWH_WAIT_INFINITE, &record);
        if (result.status == OWH_CLOSED) break;
        if (result.status != OWH_OK || record.size != sizeof(frame) || frame.raw_size != sizeof(frame.raw)) {
            fprintf(stderr, "Read: status=%d native_error=%u\n", result.status, result.native_error);
            exit_code = 1;
            break;
        }
        /* Display only a bounded ASCII name. Keep frame.raw untouched when
         * forwarding/storing it; the payload need not be UTF-8 or JSON. */
        char name[256] = {0};
        for (size_t i = 0; i < 255 && frame.raw[i]; ++i)
            name[i] = frame.raw[i] >= 32 && frame.raw[i] <= 126 ? (char)frame.raw[i] : '?';
        printf("sequence=%llu lost_before=%llu source_channel=%u event=%s\n",
            (unsigned long long)record.sequence, (unsigned long long)record.lost_before,
            frame.source_channel, name);
    }
    owh_reader_close(reader);
    return exit_code;
}
