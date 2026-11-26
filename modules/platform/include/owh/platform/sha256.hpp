#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace overwolf_hook::platform {
bool sha256_file(const std::wstring &path, std::array<uint8_t, 32> &digest) noexcept;
}
