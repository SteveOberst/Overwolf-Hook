#pragma once
#include <memory>
#include <overwolf_hook/types/events.h>
#include <owh/core/types.hpp>
#include <owh/ipc/shared_lock.hpp>
#include <owh/ipc/shared_region.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/result.hpp>
#include <string>

namespace overwolf_hook::ipc {
inline constexpr size_t event_mapping_limit = 4 * 1024 * 1024;
inline constexpr uint32_t maximum_event_slots = 64;
struct EventLogHeader {
    uint32_t magic, version, slots, record_bytes;
    uint64_t published, capture_loss, process_started;
    uint32_t process_id, closed;
    boost::interprocess::interprocess_mutex mutex;
};
struct EventLogSlot {
    uint64_t sequence, capture_loss;
};
struct EventPosition { uint64_t next = 1, capture_loss = 0; };

/** One process-owned publication ring. Subscription registration and wakes
 * belong to the service owner, never to a second registry inside this mapping. */
struct EventLog {
    std::unique_ptr<SharedRegion> region;
    Handle producer;
    std::string name;
    EventLogHeader &header() const { return *static_cast<EventLogHeader *>(region->data()); }
    std::byte *slot(uint64_t sequence) const;
    void validate() const;
    bool producer_exited() const;
    EventPosition position() const;
    owh_result publish(const core::CapturedEvent &, uint64_t capture_loss = 0) noexcept;
    owh_result close(uint32_t timeout_ms = 0) noexcept;
    static std::unique_ptr<EventLog> create(const char *, uint32_t slots = maximum_event_slots);
    static std::unique_ptr<EventLog> open(const char *);
};

/** One local consuming cursor. Its caller serializes use/close. */
class EventCursor final {
  public:
    static std::pair<owh_result, std::unique_ptr<EventCursor>>
    open(const char *name, EventPosition, uint32_t filter, HANDLE verified_producer,
         uint64_t wake_handle, uint64_t closed_handle, uint32_t producer_filter = 0) noexcept;
    owh_result poll(owh_event &) noexcept;
    owh_result receive(uint32_t timeout_ms, HANDLE cancellation, owh_event &) noexcept;
  private:
    owh_result read(bool wait, uint32_t timeout_ms, HANDLE cancellation, owh_event &) noexcept;
    std::unique_ptr<EventLog> channel_;
    Handle changed_, closed_;
    EventPosition position_;
    uint64_t lost_pending_ = 0;
    uint32_t filter_ = OWH_EVENTS_ALL_CHANNELS;
    uint32_t producer_filter_ = 0;
};
static_assert(sizeof(core::CapturedEvent) <= 65536);
} // namespace overwolf_hook::ipc
