#pragma once

#include <windows.h>
#include <cstdint>

namespace overwolf_hook::platform {
uint64_t process_start(HANDLE process) noexcept;
bool same_user(HANDLE first, HANDLE second) noexcept;
uint64_t random_id();
} // namespace overwolf_hook::platform
