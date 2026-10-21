#ifndef OVERWOLF_HOOK_BRIDGE_H
#define OVERWOLF_HOOK_BRIDGE_H

#include <stdint.h>

#if defined(OWH_BUILD_owh_capture)
#define OWH_CAPTURE __declspec(dllexport)
#else
#define OWH_CAPTURE __declspec(dllimport)
#endif
#if defined(OWH_BUILD_owh_client)
#define OWH_CLIENT __declspec(dllexport)
#else
#define OWH_CLIENT __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define OWH_ABI_VERSION 1u
typedef struct owh_writer owh_writer;
typedef struct owh_reader owh_reader;

typedef int32_t owh_status;
enum {
    OWH_OK = 0,
    OWH_NO_DATA = 1,
    OWH_CLOSED = 2,
    OWH_NOT_FOUND = 3,
    OWH_BUSY = 4,
    OWH_BUFFER_TOO_SMALL = 5,
    OWH_INVALID_ARGUMENT = 6,
    OWH_ALREADY_EXISTS = 7,
    OWH_INCOMPATIBLE = 8,
    OWH_CORRUPT = 9,
    OWH_SYSTEM_ERROR = 10,
    OWH_RESOURCE_EXHAUSTED = 11
};

/** Status and original Win32 error when applicable; no thread-local error state. */
typedef struct owh_result {
    owh_status status;
    uint32_t native_error;
} owh_result;

typedef int32_t owh_start;
enum { OWH_RETAINED = 0, OWH_NEW_ONLY = 1 };

typedef struct owh_record {
    uint64_t sequence;
    uint64_t lost_before;
    uint32_t size;
    uint32_t reserved;
} owh_record;

/** Creates one bounded channel in the current Windows session. Channel names are
 *  1..100 ASCII letters/digits/'-'/'.'/'_'. No hidden process discovery or hooks.
 *  Limits: 1..4096 slots, 1..65536 bytes/record, <=16 MiB total mapping.
 *  Objects use the caller's default Windows security descriptor.
 */
OWH_CAPTURE owh_result __cdecl owh_writer_create(uint32_t abi_version,
    const char* channel, uint32_t slots, uint32_t record_bytes, owh_writer** output);

/** Copies opaque bytes. Never waits for readers; reports BUSY on mutex contention.
 *  Oversized records are rejected rather than truncated. No heap allocation on
 *  the successful publication path; not a hard-real-time contract.
 *  Calls must not race close on the same handle.
 */
OWH_CAPTURE owh_result __cdecl owh_writer_publish(owh_writer* writer,
    const void* bytes, uint32_t size);

/** Marks the channel closed, then releases this writer. On BUSY it remains owned
 *  by the caller and close must be retried after stopping callbacks.
 */
OWH_CAPTURE owh_result __cdecl owh_writer_close(owh_writer* writer);

/** Every reader owns its cursor; opening never removes other readers' data.
 *  RETAINED starts at the oldest retained record; NEW_ONLY establishes a boundary
 *  at open. Opening unavailable sources returns NOT_FOUND, not an empty stream.
 */
OWH_CLIENT owh_result __cdecl owh_reader_open(uint32_t abi_version,
    const char* channel, owh_start start, owh_reader** output);

/** Copies one complete record. Lost records are counted from sequence gaps.
 *  BUFFER_TOO_SMALL reports required size without advancing this reader.
 *  NO_DATA means an open writer has no new record; CLOSED means drained and the
 *  writer closed or its process terminated. Native bytes have no implicit UTF-8
 *  interpretation. This is a polling API, not a shared notification wait.
 *  Serialize calls for a given reader and do not race close.
 */
OWH_CLIENT owh_result __cdecl owh_reader_next(owh_reader* reader, void* bytes,
    uint32_t capacity, owh_record* record);
OWH_CLIENT void __cdecl owh_reader_close(owh_reader* reader);

#ifdef __cplusplus
}
#endif
#endif
