#include "transport/channel.hpp"
#include <cstring>
#include <limits>

struct oeh_writer { std::unique_ptr<overwolf_event_hook::Channel> channel; };

extern "C" OEH_CAPTURE oeh_result __cdecl oeh_writer_create(uint32_t abi,
    const char* name, uint32_t slots, uint32_t bytes, oeh_writer** output) {
    if (output) *output = nullptr;
    return overwolf_event_hook::boundary([&] {
        if (!output || abi != OEH_ABI_VERSION) overwolf_event_hook::fail(OEH_INVALID_ARGUMENT);
        auto writer = std::make_unique<oeh_writer>();
        writer->channel = overwolf_event_hook::Channel::create(name, slots, bytes);
        *output = writer.release();
        return overwolf_event_hook::ok;
    });
}

extern "C" OEH_CAPTURE oeh_result __cdecl oeh_writer_publish(oeh_writer* writer,
    const void* bytes, uint32_t size) {
    return overwolf_event_hook::boundary([&] {
        if (!writer || (!bytes && size)) overwolf_event_hook::fail(OEH_INVALID_ARGUMENT);
        auto& channel = *writer->channel;
        overwolf_event_hook::Lock lock(channel.mutex.get(), 0);
        if (lock.abandoned()) overwolf_event_hook::fail(OEH_CORRUPT);
        channel.validate();
        auto& h = channel.header();
        if (size > h.record_bytes) overwolf_event_hook::fail(OEH_INVALID_ARGUMENT);
        if (h.published == std::numeric_limits<uint64_t>::max())
            overwolf_event_hook::fail(OEH_RESOURCE_EXHAUSTED);
        const uint64_t sequence = h.published + 1;
        auto* slot = channel.slot(sequence);
        const overwolf_event_hook::Slot entry{sequence, size, 0};
        std::memcpy(slot, &entry, sizeof(entry));
        if (size) std::memcpy(slot + sizeof(entry), bytes, size);
        h.published = sequence;
        return overwolf_event_hook::ok;
    });
}

extern "C" OEH_CAPTURE oeh_result __cdecl oeh_writer_close(oeh_writer* writer) {
    if (!writer) return overwolf_event_hook::ok;
    const auto result = overwolf_event_hook::boundary([&] {
        auto& channel = *writer->channel;
        overwolf_event_hook::Lock lock(channel.mutex.get(), 100);
        channel.header().closed = 1;
        return overwolf_event_hook::ok;
    });
    if (result.status == OEH_OK) delete writer;
    return result;
}
