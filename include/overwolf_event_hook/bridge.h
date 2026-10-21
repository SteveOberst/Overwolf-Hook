#ifndef OVERWOLF_EVENT_HOOK_BRIDGE_H
#define OVERWOLF_EVENT_HOOK_BRIDGE_H

#include <stdint.h>

#if defined(OEH_BUILD_oeh_capture)
#define OEH_CAPTURE __declspec(dllexport)
#else
#define OEH_CAPTURE __declspec(dllimport)
#endif
#if defined(OEH_BUILD_oeh_client)
#define OEH_CLIENT __declspec(dllexport)
#else
#define OEH_CLIENT __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define OEH_ABI_VERSION 1u
typedef struct oeh_writer oeh_writer;
typedef struct oeh_reader oeh_reader;

typedef int32_t oeh_status;
enum {
    OEH_OK = 0,
    OEH_NO_DATA = 1,
    OEH_CLOSED = 2,
    OEH_NOT_FOUND = 3,
    OEH_BUSY = 4,
    OEH_BUFFER_TOO_SMALL = 5,
    OEH_INVALID_ARGUMENT = 6,
    OEH_ALREADY_EXISTS = 7,
    OEH_INCOMPATIBLE = 8,
    OEH_CORRUPT = 9,
    OEH_SYSTEM_ERROR = 10,
    OEH_RESOURCE_EXHAUSTED = 11
};

/** Status and original Win32 error when applicable; no thread-local error state. */
typedef struct oeh_result {
    oeh_status status;
    uint32_t native_error;
} oeh_result;

typedef int32_t oeh_start;
enum { OEH_RETAINED = 0, OEH_NEW_ONLY = 1 };

typedef struct oeh_record {
    uint64_t sequence;
    uint64_t lost_before;
    uint32_t size;
    uint32_t reserved;
} oeh_record;

/** Creates one bounded channel in the current Windows session. Channel names are
 *  1..100 ASCII letters/digits/'-'/'.'/'_'. No hidden process discovery or hooks.
 *  Limits: 1..4096 slots, 1..65536 bytes/record, <=16 MiB total mapping.
 *  Objects use the caller's default Windows security descriptor.
 */
OEH_CAPTURE oeh_result __cdecl oeh_writer_create(uint32_t abi_version,
    const char* channel, uint32_t slots, uint32_t record_bytes, oeh_writer** output);

/** Copies opaque bytes. Never waits for readers; reports BUSY on mutex contention.
 *  Oversized records are rejected rather than truncated. No heap allocation on
 *  the successful publication path; not a hard-real-time contract.
 *  Calls must not race close on the same handle.
 */
OEH_CAPTURE oeh_result __cdecl oeh_writer_publish(oeh_writer* writer,
    const void* bytes, uint32_t size);

/** Marks the channel closed, then releases this writer. On BUSY it remains owned
 *  by the caller and close must be retried after stopping callbacks.
 */
OEH_CAPTURE oeh_result __cdecl oeh_writer_close(oeh_writer* writer);

/** Every reader owns its cursor; opening never removes other readers' data.
 *  RETAINED starts at the oldest retained record; NEW_ONLY establishes a boundary
 *  at open. Opening unavailable sources returns NOT_FOUND, not an empty stream.
 */
OEH_CLIENT oeh_result __cdecl oeh_reader_open(uint32_t abi_version,
    const char* channel, oeh_start start, oeh_reader** output);

/** Copies one complete record. Lost records are counted from sequence gaps.
 *  BUFFER_TOO_SMALL reports required size without advancing this reader.
 *  NO_DATA means an open writer has no new record; CLOSED means drained and the
 *  writer closed or its process terminated. Native bytes have no implicit UTF-8
 *  interpretation. This is a polling API, not a shared notification wait.
 *  Serialize calls for a given reader and do not race close.
 */
OEH_CLIENT oeh_result __cdecl oeh_reader_next(oeh_reader* reader, void* bytes,
    uint32_t capacity, oeh_record* record);
OEH_CLIENT void __cdecl oeh_reader_close(oeh_reader* reader);

#ifdef __cplusplus
}
#endif
#endif
