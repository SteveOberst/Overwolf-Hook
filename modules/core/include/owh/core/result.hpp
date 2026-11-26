#pragma once
#include <overwolf_hook/types/common.h>
namespace overwolf_hook {
inline constexpr owh_result make_result(owh_status status, uint32_t stage = OWH_STAGE_NONE,
                                        uint32_t domain = OWH_ERROR_NONE,
                                        uint64_t code = 0) noexcept {
    return {status, stage, domain, 0, code};
}
inline constexpr owh_result ok = make_result(OWH_OK);
inline constexpr bool valid_result(const owh_result &value) noexcept {
    return value.status >= OWH_OK && value.status <= OWH_OPT_IN_REQUIRED && value.status != 19 &&
           !value.reserved && value.failure_stage <= OWH_STAGE_OVERWOLF_STOP &&
           value.error_domain <= OWH_ERROR_HRESULT &&
           (value.error_domain != OWH_ERROR_NONE || !value.native_code);
}
} // namespace overwolf_hook
