#include <overwolf_hook/types/common.h>

typedef struct OWH_META("internal") owh_span_item {
    uint32_t value;
} owh_span_item;

typedef struct OWH_META("model=UnsupportedSpan;output") owh_unsupported_span {
    const owh_span_item *items OWH_META("span=count;max=32");
    uint32_t count;
} owh_unsupported_span;
