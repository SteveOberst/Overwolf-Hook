#include <algorithm>
#include <cstring>
#include <new>
#include <owh/ipc/session_transport.hpp>
#include <owh/ipc/shared_lock.hpp>
#include <owh/platform/deadline.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/identity.hpp>
#include <owh/platform/result.hpp>
#include <owh/platform/wait.hpp>

namespace overwolf_hook::ipc {
std::string agent_endpoint_name(uint32_t pid, uint64_t started) {
    DWORD session = 0;
    if (!pid || !started || !ProcessIdToSessionId(pid, &session))
        fail(OWH_INVALID_ARGUMENT);
    return "Local\\owh.agent." + std::to_string(session) + "." + std::to_string(pid) + "." +
           std::to_string(started);
}

namespace {
constexpr uint32_t connection_magic = 0x4348574f, endpoint_magic = 0x4148574f;
constexpr uint32_t connection_version = 2, endpoint_version = 2;
struct Packet {
    boost::interprocess::interprocess_mutex mutex;
    bool ready = false;
    uint32_t bytes = 0;
    std::byte data[message_capacity]{};
};
struct Layout {
    uint32_t magic, version;
    uint32_t owner_pid, agent_pid;
    uint64_t owner_started, agent_started, nonce;
    volatile LONG closed;
    uint32_t reserved;
    Packet request, reply;
};
struct Admission {
    volatile LONG state;
    uint32_t reserved;
    ConnectionDescriptor connection;
};
struct Directory {
    volatile LONG ready;
    volatile LONG admission_available;
    uint32_t magic, version, bytes;
    owh_agent_info info;
    Admission slots[admission_capacity];
};
LONG load(volatile LONG &value) noexcept { return InterlockedCompareExchange(&value, 0, 0); }
HANDLE duplicate(HANDLE process, uint64_t value, DWORD access) {
    HANDLE result = nullptr;
    if (!value || !DuplicateHandle(process, reinterpret_cast<HANDLE>(value), GetCurrentProcess(),
                                   &result, access, FALSE, 0))
        fail_win32(OWH_SYSTEM_ERROR, GetLastError());
    return result;
}
class DirectoryLock {
  public:
    DirectoryLock(HANDLE mutex, DWORD timeout) noexcept : mutex_(mutex) {
        const auto result = wait_one(mutex_, timeout);
        wait_kind_ = result.kind;
        wait_error_ = result.error;
        held_ = result.kind == WaitKind::signaled || result.kind == WaitKind::abandoned;
    }
    ~DirectoryLock() {
        if (held_)
            ReleaseMutex(mutex_);
    }
    DirectoryLock(const DirectoryLock &) = delete;
    DirectoryLock &operator=(const DirectoryLock &) = delete;
    explicit operator bool() const noexcept { return held_; }
    DWORD error() const noexcept {
        return wait_kind_ == WaitKind::timeout  ? ERROR_TIMEOUT
               : wait_kind_ == WaitKind::failed ? wait_error_
                                                : ERROR_BUSY;
    }
    void release() noexcept {
        if (held_) {
            ReleaseMutex(mutex_);
            held_ = false;
        }
    }

  private:
    HANDLE mutex_;
    bool held_ = false;
    WaitKind wait_kind_ = WaitKind::failed;
    DWORD wait_error_ = ERROR_INVALID_FUNCTION;
};
} // namespace
struct MessageConnection::Impl {
    std::unique_ptr<SharedRegion> region;
    Handle request_ready, reply_ready, owner;
    ConnectionDescriptor descriptor{};
    bool server = false;
    Layout &layout() const { return *static_cast<Layout *>(region->data()); }
};
MessageConnection::MessageConnection() : impl_(std::make_unique<Impl>()) {}
MessageConnection::~MessageConnection() { close(); }
std::unique_ptr<MessageConnection> MessageConnection::create(uint32_t pid, uint64_t started) {
    auto result = std::unique_ptr<MessageConnection>(new MessageConnection());
    auto &self = *result->impl_;
    self.owner.reset(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_DUP_HANDLE,
                                 FALSE, pid));
    if (!self.owner || platform::process_start(self.owner.get()) != started ||
        !platform::same_user(GetCurrentProcess(), self.owner.get()))
        fail(OWH_INCOMPATIBLE);
    self.region = SharedRegion::create(sizeof(Layout));
    self.request_ready.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    self.reply_ready.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!self.request_ready || !self.reply_ready)
        fail_win32(OWH_SYSTEM_ERROR, GetLastError());
    ::new (self.region->data()) Layout{};
    auto &h = self.layout();
    h.magic = connection_magic;
    h.version = connection_version;
    h.owner_pid = GetCurrentProcessId();
    h.agent_pid = pid;
    h.owner_started = platform::process_start(GetCurrentProcess());
    h.agent_started = started;
    h.nonce = platform::random_id();
    self.descriptor = {h.owner_pid,
                       0,
                       h.owner_started,
                       h.nonce,
                       reinterpret_cast<uint64_t>(self.region->handle()),
                       reinterpret_cast<uint64_t>(self.request_ready.get()),
                       reinterpret_cast<uint64_t>(self.reply_ready.get())};
    return result;
}
std::unique_ptr<MessageConnection> MessageConnection::import(const ConnectionDescriptor &d,
                                                             HANDLE owner) {
    if (!owner || d.reserved || !d.nonce || GetProcessId(owner) != d.process_id ||
        platform::process_start(owner) != d.process_started ||
        !platform::same_user(GetCurrentProcess(), owner))
        fail(OWH_INCOMPATIBLE);
    auto result = std::unique_ptr<MessageConnection>(new MessageConnection());
    auto &self = *result->impl_;
    self.server = true;
    self.owner.reset(
        duplicate(GetCurrentProcess(), reinterpret_cast<uint64_t>(owner),
                  SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_DUP_HANDLE));
    self.region = SharedRegion::import(owner, d.mapping, sizeof(Layout));
    self.request_ready.reset(duplicate(owner, d.request_ready, SYNCHRONIZE | EVENT_MODIFY_STATE));
    self.reply_ready.reset(duplicate(owner, d.reply_ready, SYNCHRONIZE | EVENT_MODIFY_STATE));
    const auto &h = self.layout();
    if (h.magic != connection_magic || h.version != connection_version ||
        h.owner_pid != d.process_id || h.owner_started != d.process_started || h.nonce != d.nonce ||
        h.agent_pid != GetCurrentProcessId() ||
        h.agent_started != platform::process_start(GetCurrentProcess()) ||
        load(self.layout().closed))
        fail(OWH_CORRUPT);
    self.descriptor = d;
    return result;
}
ConnectionDescriptor MessageConnection::descriptor() const noexcept { return impl_->descriptor; }
HANDLE MessageConnection::peer() const noexcept { return impl_->owner.get(); }
bool MessageConnection::alive() const noexcept {
    return impl_->region && !load(impl_->layout().closed) &&
           WaitForSingleObject(impl_->owner.get(), 0) == WAIT_TIMEOUT;
}
void MessageConnection::close() noexcept {
    if (!impl_ || !impl_->region)
        return;
    InterlockedExchange(&impl_->layout().closed, 1);
    if (impl_->request_ready)
        SetEvent(impl_->request_ready.get());
    if (impl_->reply_ready)
        SetEvent(impl_->reply_ready.get());
}
namespace {
DWORD lock_error(SharedLockStatus status) noexcept {
    switch (status) {
    case SharedLockStatus::busy:
        return ERROR_TIMEOUT;
    case SharedLockStatus::cancelled:
        return ERROR_CANCELLED;
    case SharedLockStatus::peer_gone:
        return ERROR_PROCESS_ABORTED;
    case SharedLockStatus::corrupt:
        return ERROR_INVALID_DATA;
    case SharedLockStatus::locked:
        return ERROR_SUCCESS;
    }
    return ERROR_INVALID_DATA;
}
} // namespace
bool MessageConnection::send(std::span<const std::byte> bytes, DWORD timeout) noexcept {
    if (!alive() || bytes.size() > message_capacity) {
        SetLastError(ERROR_BROKEN_PIPE);
        return false;
    }
    try {
        auto &packet = impl_->server ? impl_->layout().reply : impl_->layout().request;
        SharedLock lock(packet.mutex, timeout, peer());
        if (lock.status() != SharedLockStatus::locked) {
            SetLastError(lock_error(lock.status()));
            return false;
        }
        if (load(impl_->layout().closed)) {
            SetLastError(ERROR_BROKEN_PIPE);
            return false;
        }
        if (packet.ready) {
            SetLastError(ERROR_BUSY);
            return false;
        }
        packet.bytes = static_cast<uint32_t>(bytes.size());
        std::memcpy(packet.data, bytes.data(), bytes.size());
        packet.ready = true;
        lock.unlock();
        return SetEvent(impl_->server ? impl_->reply_ready.get() : impl_->request_ready.get()) !=
               FALSE;
    } catch (...) {
        close();
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
}
bool MessageConnection::receive(std::span<std::byte> output, uint32_t &received,
                                DWORD timeout) noexcept {
    received = 0;
    const Deadline deadline(timeout);
    try {
        auto &packet = impl_->server ? impl_->layout().request : impl_->layout().reply;
        for (;;) {
            SharedLock lock(packet.mutex, deadline.remaining_ms(), peer());
            if (lock.status() != SharedLockStatus::locked) {
                SetLastError(lock_error(lock.status()));
                return false;
            }
            if (packet.ready) {
                const uint32_t size = packet.bytes;
                if (size > message_capacity || size > output.size()) {
                    SetLastError(ERROR_INVALID_DATA);
                    close();
                    return false;
                }
                std::memcpy(output.data(), packet.data, size);
                received = size;
                packet.ready = false;
                return true;
            }
            // A server may close immediately after publishing its final reply.
            // Consume that committed packet before reporting endpoint closure.
            if (load(impl_->layout().closed)) {
                SetLastError(ERROR_BROKEN_PIPE);
                return false;
            }
            lock.unlock();
            if (!alive()) {
                SetLastError(ERROR_BROKEN_PIPE);
                return false;
            }
            const auto left = deadline.remaining_ms();
            if (!left) {
                SetLastError(ERROR_TIMEOUT);
                return false;
            }
            HANDLE waits[]{impl_->server ? impl_->request_ready.get() : impl_->reply_ready.get(),
                           peer()};
            const auto waited = wait_any(waits, 2, std::min<DWORD>(left, 100));
            if (waited.kind != WaitKind::signaled || waited.index != 0) {
                if (waited.kind == WaitKind::timeout)
                    continue;
                SetLastError(waited.kind == WaitKind::failed ? waited.error : ERROR_INVALID_DATA);
                return false;
            }
        }
    } catch (...) {
        close();
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
}
struct AgentEndpoint::Impl {
    std::unique_ptr<SharedRegion> region;
    Handle wake, admission_mutex;
    Directory &directory() const { return *static_cast<Directory *>(region->data()); }
    void clear_abandoned_reservations() noexcept {
        for (auto &slot : directory().slots)
            if (load(slot.state) < 0)
                InterlockedExchange(&slot.state, 0);
    }
};
AgentEndpoint::AgentEndpoint() : impl_(std::make_unique<Impl>()) {}
AgentEndpoint::~AgentEndpoint() = default;
std::unique_ptr<AgentEndpoint> AgentEndpoint::create(const owh_agent_info &info) {
    auto result = std::unique_ptr<AgentEndpoint>(new AgentEndpoint());
    const auto name = agent_endpoint_name(info.process_id, info.process_started);
    result->impl_->region = SharedRegion::create(sizeof(Directory), name.c_str());
    result->impl_->wake.reset(CreateEventA(nullptr, FALSE, FALSE, (name + ".wake").c_str()));
    if (!result->impl_->wake)
        fail_win32(OWH_SYSTEM_ERROR, GetLastError());
    result->impl_->admission_mutex.reset(
        CreateMutexA(nullptr, FALSE, (name + ".admission").c_str()));
    if (!result->impl_->admission_mutex)
        fail_win32(OWH_SYSTEM_ERROR, GetLastError());
    auto &d = result->impl_->directory();
    std::memset(&d, 0, sizeof(d));
    d.magic = endpoint_magic;
    d.version = endpoint_version;
    d.bytes = sizeof(d);
    d.info = info;
    InterlockedExchange(&d.admission_available, 1);
    InterlockedExchange(&d.ready, 1);
    return result;
}
std::unique_ptr<AgentEndpoint> AgentEndpoint::open(uint32_t pid, uint64_t started) {
    auto result = std::unique_ptr<AgentEndpoint>(new AgentEndpoint());
    const auto name = agent_endpoint_name(pid, started);
    result->impl_->region = SharedRegion::open(name.c_str(), sizeof(Directory));
    result->impl_->wake.reset(
        OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (name + ".wake").c_str()));
    if (!result->impl_->wake)
        fail_win32(OWH_SYSTEM_ERROR, GetLastError());
    result->impl_->admission_mutex.reset(
        OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + ".admission").c_str()));
    if (!result->impl_->admission_mutex)
        fail_win32(OWH_SYSTEM_ERROR, GetLastError());
    const auto info = result->info();
    if (info.process_id != pid || info.process_started != started)
        fail(OWH_CORRUPT);
    return result;
}
owh_agent_info AgentEndpoint::info() const {
    auto &d = impl_->directory();
    if (!load(d.ready))
        fail(OWH_BUSY);
    if (d.magic != endpoint_magic || d.version != endpoint_version || d.bytes != sizeof(d) ||
        d.info.size != sizeof(owh_agent_info) || !d.info.instance_id)
        fail(OWH_INCOMPATIBLE);
    return d.info;
}
bool AgentEndpoint::submit(const ConnectionDescriptor &descriptor, DWORD timeout) noexcept {
    auto &directory = impl_->directory();
    if (!load(directory.admission_available)) {
        SetLastError(ERROR_BUSY);
        return false;
    }
    const Deadline deadline(timeout);
    do {
        {
            DirectoryLock lock(impl_->admission_mutex.get(),
                               std::min<DWORD>(deadline.remaining_ms(), 50));
            if (!lock) {
                if (lock.error() != ERROR_TIMEOUT || deadline.expired()) {
                    SetLastError(lock.error());
                    return false;
                }
            } else {
                // Negative slot states exist only while a submitter holds the
                // mutex. Once acquired, any such reservation was abandoned.
                impl_->clear_abandoned_reservations();
                if (!load(directory.admission_available)) {
                    lock.release();
                    SetLastError(ERROR_BUSY);
                    return false;
                }
                for (auto &slot : directory.slots) {
                    const LONG reservation = static_cast<LONG>(descriptor.process_id | 0x80000000u);
                    if (InterlockedCompareExchange(&slot.state, reservation, 0) != 0)
                        continue;
                    slot.connection = descriptor;
                    InterlockedExchange(&slot.state, 2);
                    SetEvent(impl_->wake.get());
                    return true;
                }
            }
        }
        if (!timeout)
            break;
        Sleep(1);
    } while (!deadline.expired());
    SetLastError(ERROR_BUSY);
    return false;
}
bool AgentEndpoint::accept(ConnectionDescriptor &output) noexcept {
    DirectoryLock lock(impl_->admission_mutex.get(), 0);
    if (!lock)
        return false;
    impl_->clear_abandoned_reservations();
    for (auto &slot : impl_->directory().slots) {
        if (InterlockedCompareExchange(&slot.state, 3, 2) != 2)
            continue;
        output = slot.connection;
        InterlockedExchange(&slot.state, 0);
        return true;
    }
    return false;
}
void AgentEndpoint::set_admission_available(bool available) noexcept {
    InterlockedExchange(&impl_->directory().admission_available, available ? 1 : 0);
}
void AgentEndpoint::wait(DWORD timeout) noexcept {
    WaitForSingleObject(impl_->wake.get(), timeout);
}
} // namespace overwolf_hook::ipc
