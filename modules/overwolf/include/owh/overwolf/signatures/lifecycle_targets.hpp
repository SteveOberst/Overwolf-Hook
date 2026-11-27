#pragma once
#include <array>
#include <cstdint>
namespace overwolf_hook::overwolf::signatures::lifecycle {
inline constexpr std::array<std::int16_t, 20> main_agent_init_body_anchor_1{
    0x48, 0x83, 0xbf, 0xb8, 0x02, 0x00, 0x00, 0x00, 0x75, -1,
    0xb9, 0xa0, 0x00, 0x00, 0x00, 0xe8, -1,   -1,   -1,   -1};
inline constexpr std::array<std::int16_t, 31> main_agent_init_body_anchor_2{
    0x48, 0x8d, 0x8f, 0xb8, 0x02, 0x00, 0x00, 0xe8, -1,   -1,   -1,   -1, 0x48, 0x8d, 0x15, -1,
    -1,   -1,   -1,   0x48, 0x8b, 0x8f, 0xb8, 0x02, 0x00, 0x00, 0xe8, -1, -1,   -1,   -1};
inline constexpr std::array<std::uint8_t, 5> main_agent_init_body_entry{0x48, 0x89, 0x5c, 0x24,
                                                                        0x18};
inline constexpr std::array<std::int16_t, 16> main_agent_clear_body_anchor_1{
    0xe8, -1, -1, -1, -1, 0x0f, 0xb6, 0xd8, 0xe8, -1, -1, -1, -1, 0x48, 0x8b, 0xc8};
inline constexpr std::array<std::int16_t, 17> main_agent_clear_body_anchor_2{
    0x48, 0x8b, 0xc8, 0xe8, -1, -1, -1, -1, 0x0f, 0xb6, 0xc3, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};
inline constexpr std::array<std::uint8_t, 6> main_agent_clear_body_entry{0x40, 0x53, 0x48,
                                                                         0x83, 0xec, 0x20};
} // namespace overwolf_hook::overwolf::signatures::lifecycle
