#pragma once
#include <windows.h>
#include <overwolf_hook/types/build_info.h>
#include <cstdint>
#include <string>
#include <vector>
namespace overwolf_hook::client::detail {
struct AgentPayload {
    std::vector<uint8_t> bytes;
    DWORD bootstrap_rva = 0;
    DWORD build_info_rva = 0;
    DWORD image_size = 0;
    owh_build_info build_info{};
    size_t sections_offset = 0;
};

bool parse_agent_payload(const std::wstring &, AgentPayload &);
bool compatible_build_info(const owh_build_info &) noexcept;
} // namespace overwolf_hook::client::detail
