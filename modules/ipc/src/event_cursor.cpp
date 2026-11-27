#include <algorithm>
#include <cstring>
#include <owh/ipc/event_log.hpp>
#include <owh/platform/deadline.hpp>
#include <owh/platform/identity.hpp>
#include <owh/platform/wait.hpp>

namespace overwolf_hook::ipc {
namespace {
Handle duplicate(HANDLE process, uint64_t token) {
    HANDLE local = nullptr;
    if (!token || !DuplicateHandle(process, reinterpret_cast<HANDLE>(token), GetCurrentProcess(),
                                    &local, SYNCHRONIZE, FALSE, 0))
        fail_win32(OWH_SYSTEM_ERROR, GetLastError(), OWH_STAGE_CONTROL_IO);
    return Handle(local);
}
owh_result read_locked(EventLog &log, EventPosition &position, uint64_t &pending,
                       uint32_t filter, uint32_t producer_filter, owh_event &out) {
    const auto &h = log.header();
    if (h.closed > 1) return make_result(OWH_CORRUPT);
    if (h.closed) return make_result(OWH_CLOSED);
    if (position.next > h.published) return make_result(OWH_NO_DATA);
    const uint64_t oldest = h.published >= h.slots ? h.published - h.slots + 1 : 1;
    if (position.next < oldest) {
        pending += oldest - position.next;
        position.next = oldest;
    }
    // At most the bounded ring is inspected. Skip foreign channels by metadata;
    // their raw payload never enters a client-side staging queue.
    while (position.next <= h.published) {
        const auto *slot = log.slot(position.next);
        EventLogSlot entry{};
        std::memcpy(&entry, slot, sizeof(entry));
        if (entry.sequence != position.next || entry.capture_loss < position.capture_loss ||
            entry.sequence == UINT64_MAX)
            return make_result(OWH_CORRUPT);
        pending += entry.capture_loss - position.capture_loss;
        position.capture_loss = entry.capture_loss;
        ++position.next;
        const auto *payload = slot + sizeof(entry);
        uint64_t qpc = 0;
        uint32_t channel = 0, producer = 0, size = 0;
        std::memcpy(&qpc, payload, sizeof(qpc));
        std::memcpy(&channel, payload + 8, sizeof(channel));
        std::memcpy(&producer, payload + offsetof(core::CapturedEvent, producer_process_id), sizeof(producer));
        std::memcpy(&size, payload + offsetof(core::CapturedEvent, raw_size), sizeof(size));
        if (size > sizeof(out.raw)) return make_result(OWH_CORRUPT);
        if (filter && channel != filter) continue;
        if (producer_filter && producer != producer_filter) continue;
        out.sequence = entry.sequence; out.lost_before = pending;
        out.captured_qpc = qpc; out.source_channel = channel; out.raw_size = size;
        out.producer_process_id = producer;
        std::memcpy(out.raw, payload + offsetof(core::CapturedEvent, raw), size);
        pending = 0;
        return ok;
    }
    return make_result(OWH_NO_DATA);
}
}
std::pair<owh_result, std::unique_ptr<EventCursor>> EventCursor::open(
    const char *name, EventPosition position, uint32_t filter, HANDLE verified_producer,
    uint64_t wake, uint64_t closed, uint32_t producer_filter) noexcept {
    std::unique_ptr<EventCursor> cursor;
    auto result = boundary([&] {
        if (!position.next || !verified_producer) return make_result(OWH_INVALID_ARGUMENT);
        cursor = std::make_unique<EventCursor>();
        cursor->channel_ = EventLog::open(name);
        const auto &h = cursor->channel_->header();
        if (GetProcessId(verified_producer) != h.process_id ||
            platform::process_start(verified_producer) != h.process_started)
            return make_result(OWH_INCOMPATIBLE, OWH_STAGE_AGENT_LIFETIME);
        cursor->changed_ = duplicate(verified_producer, wake);
        cursor->closed_ = duplicate(verified_producer, closed);
        cursor->position_ = position; cursor->filter_ = filter;
        cursor->producer_filter_ = producer_filter;
        return ok;
    });
    if (result.status != OWH_OK) return {result, nullptr};
    return {result, std::move(cursor)};
}
owh_result EventCursor::poll(owh_event &out) noexcept { return read(false, 0, nullptr, out); }
owh_result EventCursor::receive(uint32_t timeout, HANDLE cancellation, owh_event &out) noexcept {
    return read(true, timeout, cancellation, out);
}
owh_result EventCursor::read(bool wait, uint32_t timeout, HANDLE cancellation, owh_event &out) noexcept {
    return boundary([&] {
        const Deadline deadline(wait ? timeout : 0);
        for (;;) {
            if (cancellation && WaitForSingleObject(cancellation, 0) == WAIT_OBJECT_0)
                return make_result(OWH_CANCELLED, OWH_STAGE_WAIT);
            if (WaitForSingleObject(closed_.get(), 0) == WAIT_OBJECT_0)
                return make_result(OWH_CLOSED);
            if (channel_->producer_exited()) return make_result(OWH_AGENT_LOST);
            SharedLock lock(channel_->header().mutex, wait ? std::min<DWORD>(10, deadline.remaining_ms()) : 0,
                            channel_->producer.get(), cancellation);
            if (lock.status() == SharedLockStatus::cancelled) return make_result(OWH_CANCELLED);
            if (lock.status() == SharedLockStatus::peer_gone) return make_result(OWH_AGENT_LOST);
            if (lock.status() == SharedLockStatus::corrupt) return make_result(OWH_CORRUPT);
            if (lock.status() == SharedLockStatus::locked) {
                auto result = read_locked(*channel_, position_, lost_pending_, filter_, producer_filter_, out);
                if (!wait || result.status != OWH_NO_DATA) return result;
                lock.unlock();
                if (lock.status() == SharedLockStatus::corrupt) return make_result(OWH_CORRUPT);
            } else if (!wait) return make_result(OWH_BUSY);
            if (deadline.expired()) return make_result(OWH_TIMEOUT, OWH_STAGE_WAIT);
            HANDLE handles[]{changed_.get(), closed_.get(), channel_->producer.get(), cancellation};
            const auto signaled = wait_any(handles, cancellation ? 4 : 3,
                                          std::min<DWORD>(100, deadline.remaining_ms()));
            if (signaled.kind == WaitKind::failed || signaled.kind == WaitKind::abandoned)
                return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_WAIT, OWH_ERROR_WIN32, signaled.error);
        }
    });
}
} // namespace overwolf_hook::ipc
