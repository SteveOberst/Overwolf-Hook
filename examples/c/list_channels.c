#include <overwolf_hook/discovery.h>
#include <stdio.h>

_Static_assert(sizeof(owh_source_channel) == 8, "channel ABI");

int main(void) {
    owh_channels* channels = NULL;
    owh_result result = owh_channels_discover(OWH_ABI_VERSION, &channels);
    if (result.status != OWH_OK) {
        fprintf(stderr, "Discovery: status=%d native_error=%u\n", result.status, result.native_error);
        return 1;
    }
    for (uint32_t i = 0; i < owh_channels_count(channels); ++i) {
        owh_source_channel channel = {0};
        if (owh_channels_at(channels, i, &channel).status != OWH_OK) { owh_channels_close(channels); return 1; }
        printf("source_channel=%u info=%u events=%u\n", channel.source_channel,
            !!(channel.kinds & OWH_CHANNEL_INFO), !!(channel.kinds & OWH_CHANNEL_EVENTS));
    }
    owh_channels_close(channels);
    return 0;
}
