#include <overwolf_hook/abi/game_state.h>
#include <stdio.h>
#include <string.h>

static void print_name(const char *name) {
    unsigned i = 0;
    putchar('"');
    for (; i < 64 && name[i]; ++i) {
        unsigned char byte = (unsigned char)name[i];
        if (byte >= 32 && byte <= 126 && byte != '"' && byte != '\\')
            putchar(byte);
        else
            printf("\\x%02x", (unsigned)byte);
    }
    if (i == 64 && name[i])
        fputs("...", stdout);
    putchar('"');
}

static void failed(const char *operation, owh_result result) {
    fprintf(stderr, "%s: status=%d stage=%u domain=%u native=0x%llx\n",
            operation, result.status, result.failure_stage, result.error_domain,
            (unsigned long long)result.native_code);
}

int main(int argc, char **argv) {
    if (argc != 1 && argc != 3) {
        fputs("Usage: read_state [CATEGORY KEY]\n", stderr);
        return 1;
    }
    unsigned matches = 0;
    owh_channels *channels = NULL;
    owh_result result = owh_game_state_sources(&channels);
    if (result.status != OWH_OK) {
        failed("discover sources", result);
        return 1;
    }
    unsigned info_channels = 0;
    int exit_code = 0;
    for (uint32_t i = 0; i < owh_source_list_count(channels); ++i) {
        owh_source_channel channel = {0};
        result = owh_source_list_at(channels, i, &channel);
        if (result.status != OWH_OK) {
            failed("read source", result);
            exit_code = 1;
            continue;
        }
        if (!(channel.kinds & OWH_CHANNEL_INFO))
            continue;
        ++info_channels;
        owh_state *state = NULL;
        result = owh_game_state_read_from(channel.source_channel, channel.producer_process_id,
                                     OWH_STATE_DEFAULT_TIMEOUT_MS, &state);
        if (result.status != OWH_OK) {
            fprintf(stderr, "channel=%u ", channel.source_channel);
            failed("read state", result);
            exit_code = 1;
            continue;
        }
        if (owh_snapshot_count(state) == 0)
            printf("channel=%u: snapshot contains zero fields.\n",
                   channel.source_channel);
        for (uint32_t j = 0; j < owh_snapshot_count(state); ++j) {
            owh_state_field field = {0};
            result = owh_snapshot_at(state, j, &field);
            if (result.status != OWH_OK) {
                failed("read field", result);
                exit_code = 1;
                break;
            }
            if (argc == 3 && (strcmp(field.category, argv[1]) != 0 ||
                              strcmp(field.key, argv[2]) != 0))
                continue;
            ++matches;
            printf("channel=%u category=", channel.source_channel);
            print_name(field.category);
            fputs(" key=", stdout);
            print_name(field.key);
            printf(" valueBytes=%u firstBytes=", field.size);
            const unsigned char *value = field.bytes;
            for (uint32_t k = 0; k < field.size && k < 16; ++k)
                printf("%02x ", (unsigned)value[k]);
            puts(field.size > 16 ? "..." : "");
        }
        owh_snapshot_close(state);
    }
    if (!info_channels)
        puts("No state (INFO) channels were discovered.");
    if (argc == 3 && !matches)
        puts("Requested field not present.");
    owh_source_list_close(channels);
    return exit_code;
}
