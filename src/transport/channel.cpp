#include "channel.hpp"
#include <cstring>
#include <string>

namespace overwolf_event_hook {
namespace {
constexpr uint32_t magic = 0x3148454f; // OEH1
constexpr size_t maximum_mapping = 16 * 1024 * 1024;

std::string object_name(const char* channel, const char* suffix) {
    if (!channel) fail(OEH_INVALID_ARGUMENT);
    size_t length = 0;
    while (channel[length]) {
        const char c = channel[length++];
        if (length > 100 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            fail(OEH_INVALID_ARGUMENT);
    }
    if (!length) fail(OEH_INVALID_ARGUMENT);
    return std::string("Local\\overwolf-event-hook.") + channel + suffix;
}
size_t stride(uint32_t bytes) { return (sizeof(Slot) + bytes + 7u) & ~size_t{7}; }
size_t mapping_size(uint32_t slots, uint32_t bytes) {
    if (!slots || slots > 4096 || !bytes || bytes > 65536) fail(OEH_INVALID_ARGUMENT);
    const size_t size = sizeof(Header) + slots * stride(bytes);
    if (size > maximum_mapping) fail(OEH_INVALID_ARGUMENT);
    return size;
}
uint64_t started(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
        fail(OEH_SYSTEM_ERROR, GetLastError());
    return (uint64_t{creation.dwHighDateTime} << 32u) | creation.dwLowDateTime;
}
void map(Channel& channel, DWORD access) {
    channel.view.address = MapViewOfFile(channel.mapping.get(), access, 0, 0, 0);
    if (!channel.view.address) fail(OEH_SYSTEM_ERROR, GetLastError());
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(channel.view.address, &region, sizeof(region)))
        fail(OEH_SYSTEM_ERROR, GetLastError());
    channel.view.size = region.RegionSize;
}
}

Lock::Lock(HANDLE mutex, DWORD timeout) : mutex_(mutex) {
    const DWORD result = WaitForSingleObject(mutex, timeout);
    owned_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    abandoned_ = result == WAIT_ABANDONED;
    if (result == WAIT_TIMEOUT) fail(OEH_BUSY);
    if (!owned_) fail(OEH_SYSTEM_ERROR, GetLastError());
}

std::unique_ptr<Channel> Channel::create(const char* name, uint32_t slots, uint32_t bytes) {
    const size_t size = mapping_size(slots, bytes);
    auto channel = std::make_unique<Channel>();
    const auto mutex_name = object_name(name, ".mutex");
    const auto data_name = object_name(name, ".data");
    channel->mutex = Handle(CreateMutexA(nullptr, FALSE, mutex_name.c_str()));
    if (!channel->mutex.get()) fail(OEH_SYSTEM_ERROR, GetLastError());
    Lock lock(channel->mutex.get(), 100);
    channel->mapping = Handle(CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, static_cast<DWORD>(size), data_name.c_str()));
    const DWORD error = GetLastError();
    if (!channel->mapping.get()) fail(OEH_SYSTEM_ERROR, error);
    if (error == ERROR_ALREADY_EXISTS) fail(OEH_ALREADY_EXISTS);
    map(*channel, FILE_MAP_READ | FILE_MAP_WRITE);
    std::memset(channel->view.address, 0, size);
    channel->header() = Header{magic, OEH_ABI_VERSION, slots, bytes, 0,
        started(GetCurrentProcess()), GetCurrentProcessId(), 0};
    return channel;
}

std::unique_ptr<Channel> Channel::open(const char* name) {
    auto channel = std::make_unique<Channel>();
    const auto data_name = object_name(name, ".data");
    const auto mutex_name = object_name(name, ".mutex");
    channel->mapping = Handle(OpenFileMappingA(FILE_MAP_READ, FALSE, data_name.c_str()));
    if (!channel->mapping.get()) {
        const DWORD error = GetLastError();
        fail(error == ERROR_FILE_NOT_FOUND ? OEH_NOT_FOUND : OEH_SYSTEM_ERROR, error);
    }
    channel->mutex = Handle(OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutex_name.c_str()));
    if (!channel->mutex.get()) fail(OEH_SYSTEM_ERROR, GetLastError());
    map(*channel, FILE_MAP_READ);
    Lock lock(channel->mutex.get(), 100);
    if (lock.abandoned()) fail(OEH_CORRUPT);
    channel->validate();
    channel->producer = Handle(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE, channel->header().process_id));
    if (!channel->producer.get()) {
        const DWORD error = GetLastError();
        if (error != ERROR_INVALID_PARAMETER) fail(OEH_SYSTEM_ERROR, error);
    } else if (started(channel->producer.get()) != channel->header().process_started) {
        // A recycled PID is not the producer of this retained mapping.
        channel->producer = Handle{};
    }
    return channel;
}

void Channel::validate() const {
    if (view.size < sizeof(Header)) fail(OEH_CORRUPT);
    const auto& h = header();
    if (h.magic != magic || h.version != OEH_ABI_VERSION) fail(OEH_INCOMPATIBLE);
    size_t expected;
    try { expected = mapping_size(h.slots, h.record_bytes); }
    catch (const Failure&) { fail(OEH_CORRUPT); }
    if (expected > view.size || h.closed > 1 || !h.process_id) fail(OEH_CORRUPT);
}

std::byte* Channel::slot(uint64_t sequence) const {
    return static_cast<std::byte*>(view.address) + sizeof(Header) +
        ((sequence - 1) % header().slots) * stride(header().record_bytes);
}

bool Channel::producer_exited() const {
    if (!producer.get()) return true;
    const DWORD result = WaitForSingleObject(producer.get(), 0);
    if (result == WAIT_FAILED) fail(OEH_SYSTEM_ERROR, GetLastError());
    return result == WAIT_OBJECT_0;
}
}
