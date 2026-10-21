#pragma once
#include <charconv>
#include <cstdint>
#include <string_view>

namespace overwolf_hook::overwolf {
// Only discrete-event services belong to this hook. InfoDB and unrelated
// services must not be forwarded, even when every event channel is selected.
inline uint32_t event_channel(std::string_view name) noexcept {
    constexpr std::string_view prefix = "ow_events_";
    if (!name.starts_with(prefix)) return 0;
    name.remove_prefix(prefix.size());
    uint32_t channel = 0;
    const auto parsed = std::from_chars(name.data(), name.data() + name.size(), channel);
    return parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() ? channel : 0;
}
}
