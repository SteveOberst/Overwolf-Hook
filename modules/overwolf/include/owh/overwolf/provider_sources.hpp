#pragma once

#include <overwolf_hook/types/common.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace overwolf_hook::overwolf {
struct ProviderSource {
    uint32_t source_channel = 0;
    uint32_t producer_process_id = 0;
    std::wstring events_mapping;
    std::wstring info_mapping;
    // Private consumer metadata for its conditional value transform. Never log
    // or expose this string through the public discovery records.
    std::string value_key;
};

// Read existing consumer metadata in the selected Overwolf main process. Does
// not initialize consumers, open producer processes, or connect producer pipes.
owh_result provider_sources(std::vector<ProviderSource> &output) noexcept;

// Mapped-image resolver shared with immutable PE fixtures. Throws Failure for
// unsupported or ambiguous code; the returned RVA is a data slot, not code.
size_t provider_registry_head_offset(std::span<const uint8_t> image);
// Same conditional value transform as the consumer, applied to an owned copy.
std::vector<std::byte> decode_provider_value(std::span<const std::byte>, std::string_view key);
} // namespace overwolf_hook::overwolf
