#include "transport/channel.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

struct owh_reader {
    std::unique_ptr<overwolf_hook::Channel> channel;
    overwolf_hook::Handle changed, cancel;
    uint64_t next = 1;
    size_t registration = overwolf_hook::maximum_readers;
    uint64_t generation = 0;
    ~owh_reader() {
        if (!channel || registration == overwolf_hook::maximum_readers) return;
        const auto locked = WaitForSingleObject(channel->mutex.get(), INFINITE);
        if (locked == WAIT_OBJECT_0 || locked == WAIT_ABANDONED) {
            auto& row = channel->header().readers[registration];
            if (row.generation == generation) row = {};
            ReleaseMutex(channel->mutex.get());
        }
    }
};

namespace {
using namespace overwolf_hook;
uint64_t process_started(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) fail(OWH_SYSTEM_ERROR, GetLastError());
    return (uint64_t{creation.dwHighDateTime} << 32) | creation.dwLowDateTime;
}
bool retired(const ReaderRegistration& row) {
    Handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.process_id));
    if (!process.get()) return GetLastError() == ERROR_INVALID_PARAMETER;
    return WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0 || process_started(process.get()) != row.process_started;
}
owh_result read_locked(owh_reader& reader, void* bytes, uint32_t capacity, owh_record& record) {
    auto& channel = *reader.channel;
    channel.validate();
    const auto& h = channel.header();
    if (reader.next > h.published)
        return {h.closed || channel.producer_exited() ? OWH_CLOSED : OWH_NO_DATA, 0};
    const uint64_t oldest = h.published >= h.slots ? h.published - h.slots + 1 : 1;
    const uint64_t sequence = std::max(reader.next, oldest);
    Slot entry{};
    const auto* slot = channel.slot(sequence);
    std::memcpy(&entry, slot, sizeof(entry));
    if (entry.sequence != sequence || entry.size > h.record_bytes || entry.reserved) fail(OWH_CORRUPT);
    record = owh_record{sequence, sequence - reader.next, entry.size, 0};
    if (capacity < entry.size) return {OWH_BUFFER_TOO_SMALL, 0};
    if (entry.size) std::memcpy(bytes, slot + sizeof(entry), entry.size);
    if (sequence == std::numeric_limits<uint64_t>::max()) fail(OWH_RESOURCE_EXHAUSTED);
    reader.next = sequence + 1;
    return ok;
}
}

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
        auto& h = reader->channel->header();
        if (h.published == std::numeric_limits<uint64_t>::max())
            overwolf_hook::fail(OWH_RESOURCE_EXHAUSTED);
        reader->next = start == OWH_NEW_ONLY ? h.published + 1 :
            (h.published >= h.slots ? h.published - h.slots + 1 : 1);
        size_t slot = maximum_readers;
        for (size_t i = 0; i < maximum_readers; ++i) if (!h.readers[i].generation) { slot = i; break; }
        if (slot == maximum_readers) {
            for (size_t i = 0; i < maximum_readers; ++i) if (retired(h.readers[i])) {
                h.readers[i] = {}; slot = i; break;
            }
        }
        if (slot == maximum_readers || h.reader_generation == UINT64_MAX) fail(OWH_RESOURCE_EXHAUSTED);
        reader->generation = ++h.reader_generation;
        reader->changed = reader->channel->reader_event(reader->generation, true);
        reader->cancel = Handle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!reader->cancel.get()) fail(OWH_SYSTEM_ERROR, GetLastError());
        h.readers[slot] = {reader->generation, process_started(GetCurrentProcess()), GetCurrentProcessId(), 0};
        reader->registration = slot;
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
        if (WaitForSingleObject(reader->cancel.get(), 0) == WAIT_OBJECT_0) return owh_result{OWH_CANCELLED, 0};
        overwolf_hook::Lock lock(channel.mutex.get(), 100);
        if (lock.abandoned()) overwolf_hook::fail(OWH_CORRUPT);
        return read_locked(*reader, bytes, capacity, *record);
    });
}

extern "C" OWH_CLIENT owh_result __cdecl owh_reader_receive(owh_reader* reader,
    void* bytes, uint32_t capacity, uint32_t timeout_ms, owh_record* record) {
    if (record) *record = {};
    return overwolf_hook::boundary([&]() -> owh_result {
        if (!reader || !record || (!bytes && capacity)) fail(OWH_INVALID_ARGUMENT);
        const uint64_t deadline = timeout_ms == INFINITE ? UINT64_MAX : GetTickCount64() + timeout_ms;
        const auto remaining = [&] { const auto now = GetTickCount64();
            return deadline == UINT64_MAX ? INFINITE : static_cast<DWORD>(now >= deadline ? 0 : deadline - now); };
        auto& channel = *reader->channel;
        for (;;) {
            {
                Lock lock(channel.mutex.get(), remaining(), reader->cancel.get());
                if (lock.abandoned()) fail(OWH_CORRUPT);
                const auto result = read_locked(*reader, bytes, capacity, *record);
                if (result.status != OWH_NO_DATA) return result;
                // Reset only our signal, while publication is excluded. A write
                // after unlocking signals it before or during the kernel wait.
                if (!ResetEvent(reader->changed.get())) fail(OWH_SYSTEM_ERROR, GetLastError());
            }
            HANDLE handles[]{reader->cancel.get(), reader->changed.get(), channel.producer.get()};
            const DWORD count = channel.producer.get() ? 3 : 2;
            const auto waited = WaitForMultipleObjects(count, handles, FALSE, remaining());
            if (waited == WAIT_TIMEOUT) return {OWH_TIMEOUT, 0};
            if (waited == WAIT_OBJECT_0) return {OWH_CANCELLED, 0};
            if (waited == WAIT_FAILED) fail(OWH_SYSTEM_ERROR, GetLastError());
            // A notification or process exit prompts a cursor/closure recheck.
        }
    });
}
extern "C" OWH_CLIENT owh_result __cdecl owh_reader_cancel(owh_reader* reader) {
    if (!reader) return {OWH_INVALID_ARGUMENT, 0};
    return SetEvent(reader->cancel.get()) ? overwolf_hook::ok : owh_result{OWH_SYSTEM_ERROR, GetLastError()};
}

extern "C" OWH_CLIENT void __cdecl owh_reader_close(owh_reader* reader) { delete reader; }
