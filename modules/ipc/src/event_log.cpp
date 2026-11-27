#include <algorithm>
#include <cstring>
#include <new>
#include <owh/ipc/event_log.hpp>
#include <owh/platform/identity.hpp>

namespace overwolf_hook::ipc {
namespace {
constexpr uint32_t magic = 0x3148454f, wire_version = 2;
constexpr size_t slot_bytes = (sizeof(EventLogSlot) + sizeof(core::CapturedEvent) + 7) & ~size_t{7};
std::string object_name(const char *name) {
    if (!name || !*name) fail(OWH_INVALID_ARGUMENT);
    size_t length = 0;
    for (; name[length]; ++length) {
        const unsigned char c = name[length];
        if (length >= 100 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                               (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            fail(OWH_INVALID_ARGUMENT);
    }
    return std::string("Local\\overwolf-hook.") + name + ".data";
}
size_t mapping_size(uint32_t slots) {
    if (!slots || slots > maximum_event_slots ||
        slots > (event_mapping_limit - sizeof(EventLogHeader)) / slot_bytes)
        fail(OWH_INVALID_ARGUMENT);
    return sizeof(EventLogHeader) + slots * slot_bytes;
}
}
std::unique_ptr<EventLog> EventLog::create(const char *name, uint32_t slots) {
    if (!slots || slots > maximum_event_slots) fail(OWH_INVALID_ARGUMENT);
    auto log = std::make_unique<EventLog>();
    log->name = name;
    const auto count = std::min<uint32_t>(slots,
        static_cast<uint32_t>((event_mapping_limit - sizeof(EventLogHeader)) / slot_bytes));
    log->region = SharedRegion::create(mapping_size(count), object_name(name).c_str());
    auto *h = ::new (log->region->data()) EventLogHeader{};
    h->magic = magic; h->version = wire_version; h->slots = count;
    h->record_bytes = sizeof(core::CapturedEvent);
    h->process_id = GetCurrentProcessId();
    h->process_started = platform::process_start(GetCurrentProcess());
    return log;
}
std::unique_ptr<EventLog> EventLog::open(const char *name) {
    auto log = std::make_unique<EventLog>();
    log->name = name;
    log->region = SharedRegion::open(object_name(name).c_str(), 0);
    // Immutable framing must be checked before interpreting the shared mutex.
    log->validate();
    auto &h = log->header();
    log->producer.reset(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, h.process_id));
    if (!log->producer || platform::process_start(log->producer.get()) != h.process_started)
        fail(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME);
    return log;
}
void EventLog::validate() const {
    if (region->size() < sizeof(EventLogHeader)) fail(OWH_CORRUPT);
    const auto &h = header();
    if (h.magic != magic || h.version != wire_version) fail(OWH_INCOMPATIBLE);
    if (!h.slots || h.slots > maximum_event_slots || h.record_bytes != sizeof(core::CapturedEvent) ||
        h.slots > (event_mapping_limit - sizeof(EventLogHeader)) / slot_bytes ||
        sizeof(EventLogHeader) + h.slots * slot_bytes > region->size() ||
        region->size() > event_mapping_limit || !h.process_id)
        fail(OWH_CORRUPT);
}
std::byte *EventLog::slot(uint64_t sequence) const {
    return static_cast<std::byte *>(region->data()) + sizeof(EventLogHeader) +
           ((sequence - 1) % header().slots) * slot_bytes;
}
bool EventLog::producer_exited() const {
    return producer && WaitForSingleObject(producer.get(), 0) == WAIT_OBJECT_0;
}
EventPosition EventLog::position() const {
    SharedLock lock(header().mutex, 100);
    if (lock.status() != SharedLockStatus::locked)
        fail(lock.status() == SharedLockStatus::corrupt ? OWH_CORRUPT : OWH_BUSY);
    if (header().published == UINT64_MAX) fail(OWH_RESOURCE_EXHAUSTED);
    return {header().published + 1, header().capture_loss};
}
owh_result EventLog::publish(const core::CapturedEvent &event, uint64_t loss) noexcept {
    return boundary([&] {
        SharedLock lock(header().mutex, 0);
        if (lock.status() != SharedLockStatus::locked)
            return make_result(lock.status() == SharedLockStatus::corrupt ? OWH_CORRUPT : OWH_BUSY);
        auto &h = header();
        if (h.closed) return make_result(OWH_CLOSED);
        if (h.published == UINT64_MAX) return make_result(OWH_RESOURCE_EXHAUSTED);
        if (loss < h.capture_loss || event.raw_size > sizeof(event.raw))
            return make_result(OWH_INVALID_ARGUMENT);
        const EventLogSlot entry{h.published + 1, loss};
        auto *target = slot(entry.sequence);
        std::memcpy(target, &entry, sizeof(entry));
        std::memcpy(target + sizeof(entry), &event, sizeof(event));
        h.capture_loss = loss;
        h.published = entry.sequence;
        return ok;
    });
}
owh_result EventLog::close(uint32_t timeout) noexcept {
    SharedLock lock(header().mutex, timeout);
    if (lock.status() != SharedLockStatus::locked)
        return make_result(lock.status() == SharedLockStatus::corrupt ? OWH_CORRUPT : OWH_BUSY);
    header().closed = 1;
    return ok;
}
} // namespace overwolf_hook::ipc
