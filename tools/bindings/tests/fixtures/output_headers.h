#include <overwolf_hook/types/common.h>

typedef struct OWH_META("model=OutputHeaderFixture;output;size=size") owh_output_header_fixture {
    uint32_t size;
    uint32_t value;
} owh_output_header_fixture;

typedef struct OWH_META("model=PayloadSizeFixture;output") owh_payload_size_fixture {
    uint32_t size;
    uint8_t payload[4];
} owh_payload_size_fixture;

typedef struct OWH_META("model=BorrowedOutputFixture;output;size=size") owh_borrowed_output_fixture {
    uint32_t size;
    const void *pixels OWH_META("pointer=borrowed;copy=clear");
} owh_borrowed_output_fixture;
