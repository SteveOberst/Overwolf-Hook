#pragma once
#include <cstdint>
#include <owh/core/policy.hpp>
#include <overwolf_hook/types/build_info.h>
namespace overwolf_hook::ipc {
inline constexpr uint32_t magic = 0x3148574f, maximum_payload = 1024;
inline constexpr uint16_t protocol = 1, response_bit = 0x8000;
struct MessageHeader {
    uint32_t magic;
    uint16_t protocol, kind;
    uint32_t header_bytes, payload_bytes;
    uint64_t request_id, host_nonce, session_id;
};
struct BootstrapArgs {
    uint32_t magic, size, abi_version, attach_mode;
    uint32_t host_process_id, client_process_id, windows_session_id, reserved;
    uint64_t host_process_started, client_process_started;
    owh_build_info build_info;
};
static_assert(sizeof(MessageHeader) == 40);
} // namespace overwolf_hook::ipc
