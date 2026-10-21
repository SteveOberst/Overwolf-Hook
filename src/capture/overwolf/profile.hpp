#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace overwolf_hook::overwolf {
// Reviewed artifact only: OWGameEventsConsumer.dll 2.0.0.5 from Overwolf 0.311.1.1.
// Reviewed through static caller/reader analysis and an owned-host fixture;
// subsequently exercised in the real host on 2026-10-21. See README compatibility.
// These are native ABI/layout facts, not game-mechanic coefficients.
inline constexpr std::array<uint8_t, 32> consumer_sha256{
    0x52,0xc4,0x17,0xc3,0x74,0xc9,0x33,0x1b,0x6b,0xb8,0xd1,0x3b,0x78,0x4c,0xb1,0xab,
    0x51,0xff,0x5f,0x97,0x5f,0xf1,0xc5,0x1b,0x8e,0xe1,0x02,0xa4,0xd1,0x6e,0x74,0x26};
inline constexpr size_t reader_rva = 0xa200;
inline constexpr std::array<uint8_t, 10> reader_entry{
    0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10};
inline constexpr size_t reader_service = 0x38;
inline constexpr size_t service_name = 8;
inline constexpr size_t service_name_length = 0x18;
inline constexpr size_t service_name_capacity = 0x20;
inline constexpr size_t raw_event_bytes = 255 + 63 * 1024;
inline constexpr size_t maximum_batch = 30;

// Native std::vector-like output observed in the caller/reader; no STL object is
// constructed or destroyed by the hook. Original allocation remains untouched.
struct EventRange { uintptr_t begin, end, reserved_end; };
static_assert(sizeof(EventRange) == 24);
}
