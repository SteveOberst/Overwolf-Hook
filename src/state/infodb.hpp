#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace overwolf_hook {
inline constexpr size_t maximum_infodb_bytes = 4 * 1024 * 1024;
struct InfoField {
    std::string category;
    std::string key;
    uint64_t ticks;
    uint32_t capacity;
    std::vector<std::byte> bytes;
};
// Decodes an owned, synchronized copy. No I/O or borrowed mapping pointers.
std::vector<InfoField> decode_infodb(std::span<const std::byte> bytes);
}
