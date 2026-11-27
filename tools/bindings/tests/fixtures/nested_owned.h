#include <overwolf_hook/types/common.h>

typedef struct OWH_META("model=NestedBytes;output") owh_nested_bytes {
    const unsigned char *data OWH_META("span=size;max=16");
    uint32_t size;
    const char *label OWH_META("cstring;max=24");
    const unsigned char *borrowed OWH_META("pointer=borrowed;copy=clear");
} owh_nested_bytes;

typedef struct OWH_META("model=NestedOwned;output") owh_nested_owned {
    owh_nested_bytes nested;
    uint32_t marker;
} owh_nested_owned;
