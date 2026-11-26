#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace overwolf_hook::overwolf::signatures::events {
// Reference artifact: OWGameEventsConsumer.dll 2.0.0.12 from Overwolf 0.317.0.19.
// Reviewed through static ConsumerEvents dispatcher/caller analysis. The common
// dispatcher receives decoded records regardless of shared-memory naming.
// These are native ABI/layout facts, not game-mechanic coefficients.
inline constexpr std::array<uint8_t, 32> consumer_sha256{
    0xfa, 0xef, 0x8b, 0x1b, 0x88, 0xd0, 0xbe, 0x4e, 0x45, 0xe3, 0xf4, 0x68, 0xad, 0x9f, 0x63, 0x6d,
    0xee, 0x20, 0x7d, 0x97, 0x2a, 0x38, 0xd6, 0x5c, 0xd5, 0x3d, 0xa6, 0xd0, 0x4d, 0x0b, 0xa4, 0x2e};
inline constexpr std::array<uint8_t, 21> dispatcher_entry{
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48,
    0x89, 0x7c, 0x24, 0x18, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30};
// Two body signatures, bounded by a PE x64 function's unwind extent. Record
// stride, payload offset and delegate channel offset remain checked. Neither
// signature encodes the function's address or distance from
// its entry. A matching hash is evidence of review, not a lookup mechanism.
inline constexpr std::array<int16_t, 17> dispatcher_records{
    0x4c, 0x69, 0xcb, 0xff, 0xfc, 0x00, 0x00, 0x4d, 0x03,
    0xc8, 0x49, 0x8d, 0x89, 0xff, 0x00, 0x00, 0x00};
inline constexpr std::array<int16_t, 18> dispatcher_callback{
    0x89, 0x44, 0x24, 0x28, 0x48, 0x89, 0x4c, 0x24, 0x20,
    0x44, 0x8b, 0xc3, 0x8b, 0x4e, 0x08, 0x41, 0xff, 0xd2};
inline constexpr size_t delegate_channel = 8;
inline constexpr size_t delegate_producer_process_id = 12;
inline constexpr size_t raw_event_bytes = 255 + 63 * 1024;
inline constexpr size_t maximum_batch = 30;

// Native std::vector-like batch observed in the caller/dispatcher; no STL object is
// constructed or destroyed by the hook. Original allocation remains untouched.
struct EventRange {
    uintptr_t begin, end, reserved_end;
};
static_assert(sizeof(EventRange) == 24);
} // namespace overwolf_hook::overwolf::signatures::events
