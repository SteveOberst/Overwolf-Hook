#include <overwolf_hook/types/common.h>

typedef struct OWH_META("model=UnsafeBorrowedOutputFixture;output") owh_unsafe_borrowed_output_fixture {
    const void *borrowed OWH_META("pointer=borrowed");
} owh_unsafe_borrowed_output_fixture;
