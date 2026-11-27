#pragma once
#include <array>
#include <memory>
#include <overwolf_hook/types/agent.h>
#include <owh/ipc/shared_region.hpp>
#include <span>
#include <string>

namespace overwolf_hook::ipc {
std::string agent_endpoint_name(uint32_t pid, uint64_t started);
inline constexpr size_t message_capacity = 1024 + 64;
inline constexpr size_t admission_capacity = 32;
/** Values reference handles in the submitting process, never the recipient. */
struct ConnectionDescriptor {
    uint32_t process_id, reserved;
    uint64_t process_started, nonce, mapping, request_ready, reply_ready;
};

/** Single logical session, duplex messages in Boost-backed shared memory. Local
 * request serialization is owned by ServiceConnection, not a cross-process lock.
 * A timeout breaks the session: requests are never automatically replayed. */
class MessageConnection final {
  public:
    static std::unique_ptr<MessageConnection> create(uint32_t agent_pid, uint64_t agent_started);
    static std::unique_ptr<MessageConnection> import(const ConnectionDescriptor &,
                                                     HANDLE verified_owner);
    ~MessageConnection();
    ConnectionDescriptor descriptor() const noexcept;
    bool send(std::span<const std::byte> bytes, DWORD timeout = 5000) noexcept;
    bool receive(std::span<std::byte> bytes, uint32_t &received, DWORD timeout = INFINITE) noexcept;
    bool alive() const noexcept;
    void close() noexcept;
    HANDLE peer() const noexcept;

  private:
    MessageConnection();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** Stable discovery/admission layout, deliberately independent of feature wire
 * version. Per-client connection storage is imported from that process, binding
 * the descriptor to an actual process-owned mapping and challenge nonce. */
class AgentEndpoint final {
  public:
    static std::unique_ptr<AgentEndpoint> create(const owh_agent_info &);
    static std::unique_ptr<AgentEndpoint> open(uint32_t pid, uint64_t started);
    ~AgentEndpoint();
    owh_agent_info info() const;
    bool submit(const ConnectionDescriptor &, DWORD timeout = 5000) noexcept;
    bool accept(ConnectionDescriptor &descriptor) noexcept;
    void set_admission_available(bool available) noexcept;
    void wait(DWORD timeout = 100) noexcept;

  private:
    AgentEndpoint();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace overwolf_hook::ipc
