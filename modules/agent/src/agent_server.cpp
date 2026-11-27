#include "agent_server.hpp"
#include "BuildInfo.hpp"
#include "agent_session.hpp"
#include "overlays/overlay_domain.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <owh/ipc/services.hpp>
#include <owh/ipc/session_protocol.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/identity.hpp>
#include <owh/platform/result.hpp>
#include <thread>
#include <vector>

namespace overwolf_hook::agent {
namespace {
constexpr size_t process_session_record_limit = 256;
}
struct AgentServer::Impl {
    struct Worker {
        std::unique_ptr<ipc::MessageConnection> connection;
        std::atomic<bool> done{false};
        std::atomic<bool> serving{true};
        uint64_t session = 0;
        std::thread thread;
        ~Worker() {
            if (thread.joinable())
                thread.join();
        }
    };
    OverlayDomain &overlays;
    events::EventHub &events;
    std::unique_ptr<ipc::AgentEndpoint> endpoint;
    std::vector<std::unique_ptr<Worker>> workers;
    mutable std::mutex retired_mutex;
    // Resource domains retain their own imports and cleanup pins. Retirement
    // needs an owner ID, never a disconnected service/transport graph.
    std::vector<uint64_t> retired_sessions;
    std::atomic<bool> stopping{false};
    std::thread listener;
    std::thread retirement_sweeper;
    owh_agent_info info{};
    Impl(OverlayDomain &o, events::EventHub &e) : overlays(o), events(e) {
        info.size = sizeof(info);
        info.abi_version = OWH_ABI_VERSION;
        info.wire_version = ipc::protocol;
        info.server_state = OWH_AGENT_READY;
        info.process_id = GetCurrentProcessId();
        info.process_started = platform::process_start(GetCurrentProcess());
        info.instance_id = platform::random_id();
        info.build_info = build_info::value;
        retired_sessions.reserve(process_session_record_limit);
        endpoint = ipc::AgentEndpoint::create(info);
        try {
            listener = std::thread([this] { run(); });
            retirement_sweeper = std::thread([this] { sweep_retired(); });
        } catch (...) {
            // A partially started root must not destroy a joinable thread and
            // terminate the host if the second thread cannot be created.
            stopping.store(true, std::memory_order_release);
            if (listener.joinable())
                listener.join();
            throw;
        }
    }
    ~Impl() {
        stopping = true;
        if (listener.joinable())
            listener.join();
        for (auto &worker : workers)
            worker->connection->close();
        workers.clear();
        if (retirement_sweeper.joinable())
            retirement_sweeper.join();
    }
    size_t active_workers() const noexcept {
        return static_cast<size_t>(
            std::count_if(workers.begin(), workers.end(), [](const auto &worker) {
                return worker->serving.load(std::memory_order_acquire);
            }));
    }
    bool admission_available() const noexcept {
        size_t retired_count = 0;
        {
            std::lock_guard lock(retired_mutex);
            retired_count = retired_sessions.size();
        }
        const auto active = active_workers();
        return active < ipc::admission_capacity &&
               active + retired_count < process_session_record_limit;
    }
    void update_admission() noexcept { endpoint->set_admission_available(admission_available()); }
    void reap() {
        for (auto it = workers.begin(); it != workers.end();) {
            if (!(*it)->done.load(std::memory_order_acquire)) {
                ++it;
                continue;
            }
            it = workers.erase(it);
        }
    }
    void sweep_retired() noexcept {
        while (!stopping.load(std::memory_order_acquire)) {
            std::vector<uint64_t> pending;
            try {
                {
                    std::lock_guard lock(retired_mutex);
                    pending = retired_sessions;
                }
                for (const auto session : pending) {
                    const auto overlay_status = overlays.retire(session);
                    const auto event_status = events.cleanup_owner(session);
                    if (overlay_status.status != OWH_OK || event_status.status != OWH_OK)
                        continue;
                    std::lock_guard lock(retired_mutex);
                    retired_sessions.erase(
                        std::remove(retired_sessions.begin(), retired_sessions.end(), session),
                        retired_sessions.end());
                }
            } catch (...) { /* Retain every owner and retry on the next sweep. */
            }
            Sleep(50);
        }
    }
    void admit(const ipc::ConnectionDescriptor &d) {
        if (!admission_available() || !d.process_id || !d.process_started || !d.nonce)
            return;
        for (const auto &worker : workers) {
            if (!worker->serving.load(std::memory_order_acquire))
                continue;
            const auto previous = worker->connection->descriptor();
            if (previous.process_id == d.process_id &&
                previous.process_started == d.process_started && previous.nonce == d.nonce)
                return;
        }
        Handle owner(
            OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                        d.process_id));
        DWORD owner_session = 0, own_session = 0;
        if (!owner || platform::process_start(owner.get()) != d.process_started ||
            !platform::same_user(GetCurrentProcess(), owner.get()) ||
            !ProcessIdToSessionId(d.process_id, &owner_session) ||
            !ProcessIdToSessionId(GetCurrentProcessId(), &own_session) ||
            owner_session != own_session)
            return;
        auto worker = std::make_unique<Worker>();
        worker->connection = ipc::MessageConnection::import(d, owner.get());
        worker->session = platform::random_id();
        core::PeerIdentity identity{};
        identity.host_process_id = info.process_id;
        identity.host_process_started = info.process_started;
        identity.client_process_id = d.process_id;
        identity.client_process_started = d.process_started;
        identity.host_nonce = d.nonce;
        identity.windows_session_id = own_session;
        identity.build_info = info.build_info;
        auto *current = worker.get();
        workers.push_back(std::move(worker));
        try {
            current->thread = std::thread([this, current, identity] {
                try {
                    AgentSession agent_session(overlays, events, *current->connection,
                                               identity, current->session);
                    ipc::ServiceRouter router;
                    agent_session.bind(router);
                    (void)ipc::serve(*current->connection, identity, current->session, router);
                } catch (
                    ...) { /* A single malformed session must not terminate the resident Agent. */
                }
                current->connection->close();
                {
                    std::lock_guard lock(retired_mutex);
                    retired_sessions.push_back(current->session);
                }
                current->serving.store(false, std::memory_order_release);
                current->done.store(true, std::memory_order_release);
            });
        } catch (...) {
            workers.pop_back();
            throw;
        }
    }
    void run() noexcept {
        while (!stopping.load(std::memory_order_acquire)) {
            try {
                reap();
                update_admission();
                ipc::ConnectionDescriptor d{};
                // Do not claim and discard queued clients when all worker slots
                // are occupied. Leave their descriptors in the bounded
                // admission ring until a worker completes and is reaped.
                while (admission_available() && endpoint->accept(d)) {
                    try {
                        admit(d);
                    } catch (...) { /* Isolate one failed admission. */
                    }
                    update_admission();
                }
                endpoint->wait(50);
            } catch (...) {
                Sleep(10);
            }
        }
        endpoint->set_admission_available(false);
    }
};
AgentServer::AgentServer(OverlayDomain &overlays, events::EventHub &events)
    : impl_(std::make_unique<Impl>(overlays, events)) {}
AgentServer::~AgentServer() = default;
} // namespace overwolf_hook::agent
