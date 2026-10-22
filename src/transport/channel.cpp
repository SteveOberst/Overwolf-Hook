#include "channel.hpp"
#include <cstring>
#include <string>
#include <cstdio>

namespace overwolf_hook {
namespace {
constexpr uint32_t magic = 0x3148454f;
constexpr uint32_t wire_version = 2; // Notification registrations; independent of the public C ABI.
constexpr size_t maximum_mapping = 16 * 1024 * 1024;

std::string object_name(const char* channel, const char* suffix) {
    if (!channel) fail(OWH_INVALID_ARGUMENT);
    size_t length = 0;
    while (channel[length]) {
        const char c = channel[length++];
        if (length > 100 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            fail(OWH_INVALID_ARGUMENT);
    }
    if (!length) fail(OWH_INVALID_ARGUMENT);
    return std::string("Local\\overwolf-hook.") + channel + suffix;
}
size_t stride(uint32_t bytes) { return (sizeof(Slot) + bytes + 7u) & ~size_t{7}; }
size_t mapping_size(uint32_t slots, uint32_t bytes) {
    if (!slots || slots > 4096 || !bytes || bytes > 65536) fail(OWH_INVALID_ARGUMENT);
    const size_t size = sizeof(Header) + slots * stride(bytes);
    if (size > maximum_mapping) fail(OWH_INVALID_ARGUMENT);
    return size;
}
uint64_t started(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
        fail(OWH_SYSTEM_ERROR, GetLastError());
    return (uint64_t{creation.dwHighDateTime} << 32u) | creation.dwLowDateTime;
}
void map(Channel& channel, DWORD access) {
    channel.view.address = MapViewOfFile(channel.mapping.get(), access, 0, 0, 0);
    if (!channel.view.address) fail(OWH_SYSTEM_ERROR, GetLastError());
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(channel.view.address, &region, sizeof(region)))
        fail(OWH_SYSTEM_ERROR, GetLastError());
    channel.view.size = region.RegionSize;
}
}

Lock::Lock(HANDLE mutex, DWORD timeout) : mutex_(mutex) {
    const DWORD result = WaitForSingleObject(mutex, timeout);
    owned_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    abandoned_ = result == WAIT_ABANDONED;
    if (result == WAIT_TIMEOUT) fail(OWH_BUSY);
    if (!owned_) fail(OWH_SYSTEM_ERROR, GetLastError());
}
Lock::Lock(HANDLE mutex, DWORD timeout, HANDLE cancel) : mutex_(mutex) {
    HANDLE handles[]{cancel, mutex};
    const DWORD result = WaitForMultipleObjects(2, handles, FALSE, timeout);
    owned_ = result == WAIT_OBJECT_0 + 1 || result == WAIT_ABANDONED_0 + 1;
    abandoned_ = result == WAIT_ABANDONED_0 + 1;
    if (result == WAIT_OBJECT_0) fail(OWH_CANCELLED);
    if (result == WAIT_TIMEOUT) fail(OWH_TIMEOUT);
    if (!owned_) fail(OWH_SYSTEM_ERROR, GetLastError());
}

std::unique_ptr<Channel> Channel::create(const char* name, uint32_t slots, uint32_t bytes) {
    const size_t size = mapping_size(slots, bytes);
    auto channel = std::make_unique<Channel>();
    const auto mutex_name = object_name(name, ".mutex");
    const auto data_name = object_name(name, ".data");
    channel->name = name;
    channel->mutex = Handle(CreateMutexA(nullptr, FALSE, mutex_name.c_str()));
    if (!channel->mutex.get()) fail(OWH_SYSTEM_ERROR, GetLastError());
    Lock lock(channel->mutex.get(), 100);
    channel->mapping = Handle(CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, static_cast<DWORD>(size), data_name.c_str()));
    const DWORD error = GetLastError();
    if (!channel->mapping.get()) fail(OWH_SYSTEM_ERROR, error);
    if (error == ERROR_ALREADY_EXISTS) fail(OWH_ALREADY_EXISTS);
    map(*channel, FILE_MAP_READ | FILE_MAP_WRITE);
    std::memset(channel->view.address, 0, size);
    channel->header() = Header{magic, wire_version, slots, bytes, 0,
        started(GetCurrentProcess()), GetCurrentProcessId(), 0};
    return channel;
}

std::unique_ptr<Channel> Channel::open(const char* name) {
    auto channel = std::make_unique<Channel>();
    const auto data_name = object_name(name, ".data");
    const auto mutex_name = object_name(name, ".mutex");
    channel->name = name;
    // Readers write only their notification registration; publication and cursor
    // ownership stay separate. This shared channel is not a hostile-peer boundary.
    channel->mapping = Handle(OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, data_name.c_str()));
    if (!channel->mapping.get()) {
        const DWORD error = GetLastError();
        fail(error == ERROR_FILE_NOT_FOUND ? OWH_NOT_FOUND : OWH_SYSTEM_ERROR, error);
    }
    channel->mutex = Handle(OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutex_name.c_str()));
    if (!channel->mutex.get()) fail(OWH_SYSTEM_ERROR, GetLastError());
    map(*channel, FILE_MAP_READ | FILE_MAP_WRITE);
    Lock lock(channel->mutex.get(), 100);
    if (lock.abandoned()) fail(OWH_CORRUPT);
    channel->validate();
    channel->producer = Handle(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE, channel->header().process_id));
    if (!channel->producer.get()) {
        const DWORD error = GetLastError();
        if (error != ERROR_INVALID_PARAMETER) fail(OWH_SYSTEM_ERROR, error);
    } else if (started(channel->producer.get()) != channel->header().process_started) {
        // A recycled PID is not the producer of this retained mapping.
        channel->producer = Handle{};
    }
    return channel;
}

void Channel::validate() const {
    if (view.size < sizeof(Header)) fail(OWH_CORRUPT);
    const auto& h = header();
    if (h.magic != magic || h.version != wire_version) fail(OWH_INCOMPATIBLE);
    size_t expected;
    try { expected = mapping_size(h.slots, h.record_bytes); }
    catch (const Failure&) { fail(OWH_CORRUPT); }
    if (expected > view.size || h.closed > 1 || !h.process_id) fail(OWH_CORRUPT);
}

std::byte* Channel::slot(uint64_t sequence) const {
    return static_cast<std::byte*>(view.address) + sizeof(Header) +
        ((sequence - 1) % header().slots) * stride(header().record_bytes);
}

bool Channel::producer_exited() const {
    if (!producer.get()) return true;
    const DWORD result = WaitForSingleObject(producer.get(), 0);
    if (result == WAIT_FAILED) fail(OWH_SYSTEM_ERROR, GetLastError());
    return result == WAIT_OBJECT_0;
}

Handle Channel::reader_event(uint64_t generation, bool create) const {
    char event_name[200];
    std::snprintf(event_name, sizeof(event_name), "Local\\overwolf-hook.%s.reader.%llu", name.c_str(),
        static_cast<unsigned long long>(generation));
    Handle event(create ? CreateEventA(nullptr, TRUE, FALSE, event_name) :
        OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name));
    if (!event.get()) fail(OWH_SYSTEM_ERROR, GetLastError());
    if (create && GetLastError() == ERROR_ALREADY_EXISTS) fail(OWH_ALREADY_EXISTS);
    return event;
}

void Channel::notify_readers() {
    owh_result failure = ok;
    for (size_t i = 0; i < maximum_readers; ++i) {
        const auto generation = header().readers[i].generation;
        auto& notification = notifications[i];
        if (generation != notification.generation) {
            notification.event = Handle{};
            notification.generation = generation;
        }
        if (!generation) continue;
        if (!notification.event.get()) {
            try { notification.event = reader_event(generation, false); }
            catch (const Failure& error) {
                // Reader death before the writer's first notification leaves no
                // event object. Reclaim that abandoned registration.
                if (error.result.native_error == ERROR_FILE_NOT_FOUND) header().readers[i] = {};
                else failure = error.result;
                continue;
            }
        }
        if (!SetEvent(notification.event.get())) failure = {OWH_SYSTEM_ERROR, GetLastError()};
    }
    if (failure.status != OWH_OK) throw Failure{failure};
}
}
