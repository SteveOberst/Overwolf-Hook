#include "transport/channel.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

struct owh_reader {
    std::unique_ptr<overwolf_hook::Channel> channel;
    uint64_t next = 1;
};

extern "C" OWH_CLIENT owh_result __cdecl owh_reader_open(uint32_t abi,
    const char* name, owh_start start, owh_reader** output) {
    if (output) *output = nullptr;
    return overwolf_hook::boundary([&] {
        if (!output || abi != OWH_ABI_VERSION || (start != OWH_RETAINED && start != OWH_NEW_ONLY))
            overwolf_hook::fail(OWH_INVALID_ARGUMENT);
        auto reader = std::make_unique<owh_reader>();
        reader->channel = overwolf_hook::Channel::open(name);
        overwolf_hook::Lock lock(reader->channel->mutex.get(), 100);
        if (lock.abandoned()) overwolf_hook::fail(OWH_CORRUPT);
        const auto& h = reader->channel->header();
        if (h.published == std::numeric_limits<uint64_t>::max())
            overwolf_hook::fail(OWH_RESOURCE_EXHAUSTED);
        reader->next = start == OWH_NEW_ONLY ? h.published + 1 :
            (h.published >= h.slots ? h.published - h.slots + 1 : 1);
        *output = reader.release();
        return overwolf_hook::ok;
    });
}

extern "C" OWH_CLIENT owh_result __cdecl owh_reader_next(owh_reader* reader,
    void* bytes, uint32_t capacity, owh_record* record) {
    if (record) *record = {};
    return overwolf_hook::boundary([&] {
        if (!reader || !record || (!bytes && capacity)) overwolf_hook::fail(OWH_INVALID_ARGUMENT);
        auto& channel = *reader->channel;
        overwolf_hook::Lock lock(channel.mutex.get(), 100);
        if (lock.abandoned()) overwolf_hook::fail(OWH_CORRUPT);
        channel.validate();
        const auto& h = channel.header();
        if (reader->next > h.published)
            return owh_result{h.closed || channel.producer_exited() ? OWH_CLOSED : OWH_NO_DATA, 0};
        const uint64_t oldest = h.published >= h.slots ? h.published - h.slots + 1 : 1;
        const uint64_t sequence = std::max(reader->next, oldest);
        overwolf_hook::Slot entry{};
        const auto* slot = channel.slot(sequence);
        std::memcpy(&entry, slot, sizeof(entry));
        if (entry.sequence != sequence || entry.size > h.record_bytes || entry.reserved)
            overwolf_hook::fail(OWH_CORRUPT);
        *record = owh_record{sequence, sequence - reader->next, entry.size, 0};
        if (capacity < entry.size) return owh_result{OWH_BUFFER_TOO_SMALL, 0};
        if (entry.size) std::memcpy(bytes, slot + sizeof(entry), entry.size);
        if (sequence == std::numeric_limits<uint64_t>::max())
            overwolf_hook::fail(OWH_RESOURCE_EXHAUSTED);
        reader->next = sequence + 1;
        return overwolf_hook::ok;
    });
}

extern "C" OWH_CLIENT void __cdecl owh_reader_close(owh_reader* reader) { delete reader; }
