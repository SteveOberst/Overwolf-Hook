#pragma once
#include <memory>
#include <overwolf_hook/types/agent.h>
#include <owh/ipc/services.hpp>
#include <owh/platform/handle.hpp>

namespace overwolf_hook::client {
namespace test_fixture {
struct Access;
}
/** A single attached session. Resources pin this exact instance across detach;
 * they cannot accidentally migrate to a later SDK attachment. */
class AttachedSession final {
  public:
    static std::pair<owh_result, std::shared_ptr<AttachedSession>> attach(const owh_client_options *);
    static owh_result probe(const owh_client_options *, owh_agent_info *) noexcept;
    static owh_result check(const owh_client_options *, owh_client_sanity_report *) noexcept;
    owh_result overlay_capabilities(owh_overlay_capabilities &) noexcept;
    owh_result event_capabilities(owh_event_capabilities &) noexcept;
    owh_result ping(uint32_t timeout_ms) noexcept;
    owh_result close() noexcept;
    /** Allocate an ID and send its Create under one ordering boundary. Other
     * calls may interleave, but two Creates cannot reach the Agent in reverse. */
    template <class Method, class Prepare>
    typename Method::Response create(Prepare &&prepare) {
        std::lock_guard lock(creation_mutex_);
        typename Method::Response reply{};
        if (!next_resource_ || next_resource_ == UINT64_MAX) {
            reply.result = make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION);
            return reply;
        }
        const core::ResourceRef ref{id_, next_resource_++};
        return calls_->call<Method>(std::forward<Prepare>(prepare)(ref));
    }
    bool reserve_mapping(uint64_t bytes) noexcept;
    void release_mapping(uint64_t bytes) noexcept;
    const owh_agent_info &agent_info() const noexcept { return agent_info_; }
    core::OverlayService &overlays() noexcept { return *overlays_; }
    core::EventService &events() noexcept { return *events_; }
    bool alive() const noexcept { return transport_ && transport_->alive(); }
    void invalidate() noexcept {
        if (transport_)
            transport_->close();
    }
    HANDLE process() const noexcept { return transport_->peer(); }
    uint32_t process_id() const noexcept { return pid_; }
    uint64_t process_started() const noexcept { return started_; }
    const core::SessionPolicy &policy() const noexcept { return policy_; }

  private:
    friend struct test_fixture::Access;
    AttachedSession() = default;
    owh_result handshake(const core::HelloRequest &) noexcept;
    std::unique_ptr<ipc::MessageConnection> transport_;
    std::unique_ptr<ipc::ServiceConnection> calls_;
    std::unique_ptr<ipc::OverlayClient> overlays_;
    std::unique_ptr<ipc::EventClient> events_;
    uint64_t id_ = 0, started_ = 0;
    uint32_t pid_ = 0;
    uint64_t next_resource_ = 1;
    core::SessionPolicy policy_{};
    std::unique_ptr<ipc::SessionClient> session_;
    owh_agent_info agent_info_{};
    std::mutex budget_mutex_;
    std::mutex creation_mutex_;
    uint64_t mapping_bytes_ = 0;
};
} // namespace overwolf_hook::client
