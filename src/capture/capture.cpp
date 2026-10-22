#include "transport/channel.hpp"
#include <cstring>
#include <limits>

struct owh_writer { std::unique_ptr<overwolf_hook::Channel> channel; };

extern "C" OWH_CAPTURE owh_result __cdecl owh_writer_create(uint32_t abi,
    const char* name, uint32_t slots, uint32_t bytes, owh_writer** output) {
    if (output) *output = nullptr;
    return overwolf_hook::boundary([&] {
        if (!output || abi != OWH_ABI_VERSION) overwolf_hook::fail(OWH_INVALID_ARGUMENT);
        auto writer = std::make_unique<owh_writer>();
        writer->channel = overwolf_hook::Channel::create(name, slots, bytes);
        *output = writer.release();
        return overwolf_hook::ok;
    });
}

extern "C" OWH_CAPTURE owh_result __cdecl owh_writer_publish(owh_writer* writer,
    const void* bytes, uint32_t size) {
    return overwolf_hook::boundary([&] {
        if (!writer || (!bytes && size)) overwolf_hook::fail(OWH_INVALID_ARGUMENT);
        auto& channel = *writer->channel;
        overwolf_hook::Lock lock(channel.mutex.get(), 0);
        if (lock.abandoned()) overwolf_hook::fail(OWH_CORRUPT);
        channel.validate();
        auto& h = channel.header();
        if (size > h.record_bytes) overwolf_hook::fail(OWH_INVALID_ARGUMENT);
        if (h.published == std::numeric_limits<uint64_t>::max())
            overwolf_hook::fail(OWH_RESOURCE_EXHAUSTED);
        const uint64_t sequence = h.published + 1;
        auto* slot = channel.slot(sequence);
        const overwolf_hook::Slot entry{sequence, size, 0};
        std::memcpy(slot, &entry, sizeof(entry));
        if (size) std::memcpy(slot + sizeof(entry), bytes, size);
        h.published = sequence;
        channel.notify_readers();
        return overwolf_hook::ok;
    });
}

extern "C" OWH_CAPTURE owh_result __cdecl owh_writer_close(owh_writer* writer) {
    if (!writer) return overwolf_hook::ok;
    const auto result = overwolf_hook::boundary([&] {
        auto& channel = *writer->channel;
        overwolf_hook::Lock lock(channel.mutex.get(), 100);
        channel.header().closed = 1;
        channel.notify_readers();
        return overwolf_hook::ok;
    });
    if (result.status == OWH_OK) delete writer;
    return result;
}
