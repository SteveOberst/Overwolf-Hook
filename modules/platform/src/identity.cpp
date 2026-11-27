#include <windows.h>
#include <bcrypt.h>
#include <owh/platform/handle.hpp>
#include <owh/platform/identity.hpp>
#include <owh/platform/result.hpp>
#include <sddl.h>
#include <vector>

namespace overwolf_hook::platform {
uint64_t process_start(HANDLE process) noexcept {
    FILETIME created{}, exited{}, kernel{}, user{};
    return GetProcessTimes(process, &created, &exited, &kernel, &user)
               ? (uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime
               : 0;
}

bool same_user(HANDLE first, HANDLE second) noexcept {
    try {
        HANDLE first_token = nullptr, second_token = nullptr;
        if (!OpenProcessToken(first, TOKEN_QUERY, &first_token))
            return false;
        Handle first_owner(first_token);
        if (!OpenProcessToken(second, TOKEN_QUERY, &second_token))
            return false;
        Handle second_owner(second_token);
        DWORD first_bytes = 0, second_bytes = 0;
        GetTokenInformation(first_token, TokenUser, nullptr, 0, &first_bytes);
        GetTokenInformation(second_token, TokenUser, nullptr, 0, &second_bytes);
        if (!first_bytes || !second_bytes)
            return false;
        std::vector<std::byte> first_data(first_bytes), second_data(second_bytes);
        return GetTokenInformation(first_token, TokenUser, first_data.data(), first_bytes,
                                   &first_bytes) &&
               GetTokenInformation(second_token, TokenUser, second_data.data(), second_bytes,
                                   &second_bytes) &&
               EqualSid(reinterpret_cast<TOKEN_USER *>(first_data.data())->User.Sid,
                        reinterpret_cast<TOKEN_USER *>(second_data.data())->User.Sid);
    } catch (...) {
        return false;
    }
}

uint64_t random_id() {
    uint64_t value = 0;
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
        !value)
        fail(OWH_SYSTEM_ERROR);
    return value;
}
} // namespace overwolf_hook::platform
