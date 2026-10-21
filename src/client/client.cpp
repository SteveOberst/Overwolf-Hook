#include "transport/channel.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

struct oeh_reader {
    std::unique_ptr<overwolf_event_hook::Channel> channel;
    uint64_t next = 1;
};

extern "C" OEH_CLIENT oeh_result __cdecl oeh_reader_open(uint32_t abi,
    const char* name, oeh_start start, oeh_reader** output) {
    if (output) *output = nullptr;
    return overwolf_event_hook::boundary([&] {
        if (!output || abi != OEH_ABI_VERSION || (start != OEH_RETAINED && start != OEH_NEW_ONLY))
            overwolf_event_hook::fail(OEH_INVALID_ARGUMENT);
        auto reader = std::make_unique<oeh_reader>();
        reader->channel = overwolf_event_hook::Channel::open(name);
        overwolf_event_hook::Lock lock(reader->channel->mutex.get(), 100);
        if (lock.abandoned()) overwolf_event_hook::fail(OEH_CORRUPT);
        const auto& h = reader->channel->header();
        if (h.published == std::numeric_limits<uint64_t>::max())
            overwolf_event_hook::fail(OEH_RESOURCE_EXHAUSTED);
        reader->next = start == OEH_NEW_ONLY ? h.published + 1 :
            (h.published >= h.slots ? h.published - h.slots + 1 : 1);
        *output = reader.release();
        return overwolf_event_hook::ok;
    });
}

extern "C" OEH_CLIENT oeh_result __cdecl oeh_reader_next(oeh_reader* reader,
    void* bytes, uint32_t capacity, oeh_record* record) {
    if (record) *record = {};
    return overwolf_event_hook::boundary([&] {
        if (!reader || !record || (!bytes && capacity)) overwolf_event_hook::fail(OEH_INVALID_ARGUMENT);
        auto& channel = *reader->channel;
        overwolf_event_hook::Lock lock(channel.mutex.get(), 100);
        if (lock.abandoned()) overwolf_event_hook::fail(OEH_CORRUPT);
        channel.validate();
        const auto& h = channel.header();
        if (reader->next > h.published)
            return oeh_result{h.closed || channel.producer_exited() ? OEH_CLOSED : OEH_NO_DATA, 0};
        const uint64_t oldest = h.published >= h.slots ? h.published - h.slots + 1 : 1;
        const uint64_t sequence = std::max(reader->next, oldest);
        overwolf_event_hook::Slot entry{};
        const auto* slot = channel.slot(sequence);
        std::memcpy(&entry, slot, sizeof(entry));
        if (entry.sequence != sequence || entry.size > h.record_bytes || entry.reserved)
            overwolf_event_hook::fail(OEH_CORRUPT);
        *record = oeh_record{sequence, sequence - reader->next, entry.size, 0};
        if (capacity < entry.size) return oeh_result{OEH_BUFFER_TOO_SMALL, 0};
        if (entry.size) std::memcpy(bytes, slot + sizeof(entry), entry.size);
        if (sequence == std::numeric_limits<uint64_t>::max())
            overwolf_event_hook::fail(OEH_RESOURCE_EXHAUSTED);
        reader->next = sequence + 1;
        return overwolf_event_hook::ok;
    });
}

extern "C" OEH_CLIENT void __cdecl oeh_reader_close(oeh_reader* reader) { delete reader; }
