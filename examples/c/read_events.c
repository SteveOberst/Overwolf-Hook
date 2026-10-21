#include <event_tap/probe.h>
#include <windows.h>
#include <stdio.h>
#include <stddef.h>

_Static_assert(sizeof(oet_result) == 8, "result ABI");
_Static_assert(sizeof(oet_record) == 24, "record ABI");
_Static_assert(offsetof(oet_record, size) == 16, "record ABI offsets");

/* Reads the channel printed by oet_probe. Never installs a hook itself. */
int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: read_events <channel printed by oet_probe>\n");
        return 2;
    }
    oet_reader* reader = NULL;
    oet_result result = oet_reader_open(OET_ABI_VERSION, argv[1], OET_RETAINED, &reader);
    if (result.status != OET_OK) {
        fprintf(stderr, "Open: status=%d native_error=%u\n", result.status, result.native_error);
        return 1;
    }
    int exit_code = 0;
    for (;;) {
        oet_probe_frame frame = {0};
        oet_record record = {0};
        result = oet_reader_next(reader, &frame, sizeof(frame), &record);
        if (result.status == OET_CLOSED) break;
        if (result.status == OET_NO_DATA || result.status == OET_BUSY) { Sleep(1); continue; }
        if (result.status != OET_OK || record.size != sizeof(frame) || frame.raw_size != sizeof(frame.raw)) {
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
    oet_reader_close(reader);
    return exit_code;
}
