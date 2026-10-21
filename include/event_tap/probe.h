#ifndef EVENT_TAP_PROBE_H
#define EVENT_TAP_PROBE_H
#include "bridge.h"
#ifdef __cplusplus
extern "C" {
#endif

/** Experimental controller ABI. Start explicitly, outside loader-lock code.
 *  The capture DLL and reviewed consumer remain resident until host exit, even
 *  after stop. Stop disables interception and closes capture resources; it does
 *  not attempt unsafe concurrent code/trampoline unloading.
 */
#define OET_ALL_CHANNELS 0u

typedef struct oet_probe_options {
    uint32_t size;
    uint32_t abi_version;
    uint32_t owner_process_id;
    /** Overwolf event channel, or OET_ALL_CHANNELS for all channels read by the
     *  reviewed consumer. A channel ID is not the public Overwolf game ID.
     */
    uint32_t source_channel;
    uint64_t owner_started;
    char channel[104];
} oet_probe_options;

typedef struct oet_probe_statistics {
    uint64_t calls;
    uint64_t seen;
    uint64_t forwarded;
    uint64_t rejected;
    uint64_t filtered;
    uint64_t invalid;
    uint64_t active_calls;
    uint32_t status;
    uint32_t native_error;
    uint32_t hook_enabled;
    uint32_t reserved;
} oet_probe_statistics;

/** Source-specific raw frame, not a canonical game event. Raw bytes retain
 *  all 255 name bytes and 63*1024 payload bytes of the copied SDK event.
 *  captured_qpc is the capture time, not the source event's game time.
 */
typedef struct oet_probe_frame {
    uint64_t captured_qpc;
    /** Actual source channel, including when all channels are selected. */
    uint32_t source_channel;
    uint32_t raw_size;
    unsigned char raw[255 + 63 * 1024];
} oet_probe_frame;

OET_CAPTURE uint32_t __stdcall oet_probe_start(void* options);
OET_CAPTURE uint32_t __stdcall oet_probe_stop(void* unused);
OET_CAPTURE uint32_t __stdcall oet_probe_query(void* statistics);
#ifdef __cplusplus
}
static_assert(sizeof(oet_probe_options) == 128);
static_assert(sizeof(oet_probe_statistics) == 72);
static_assert(sizeof(oet_probe_frame) == 64784);
#endif
#endif
