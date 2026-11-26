#include <overwolf_hook/types/common.h>

enum { OWH_FIXTURE_KNOWN = 17 };
typedef struct OWH_META("model=Fixture;output") owh_fixture {
    uint32_t sequence;
    uint8_t digest[4];
    owh_result result;
    uint32_t audit_marker;
} owh_fixture;
