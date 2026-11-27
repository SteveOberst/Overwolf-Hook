#include <overwolf_hook/types/common.h>

typedef struct OWH_META("model=InputFixture;input;init=owh_input_fixture_init;size=size") owh_input_fixture {
    uint32_t size;
    uint32_t timeout_ms;
    void *cancellation OWH_META("pointer=borrowed");
} owh_input_fixture;

void owh_input_fixture_init(owh_input_fixture *value);
