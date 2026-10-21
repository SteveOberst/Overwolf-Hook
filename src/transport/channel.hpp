#pragma once

#include <overwolf_hook/bridge.h>
#include <windows.h>
#include <cstddef>
#include <memory>
#include <utility>

namespace overwolf_hook {
struct Failure { owh_result result; };
[[noreturn]] inline void fail(owh_status status, DWORD native = 0) {
    throw Failure{{status, native}};
}

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE value) noexcept : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        std::swap(value_, other.value_);
        return *this;
    }
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_ = nullptr;
};

class View {
public:
    ~View() { if (address) UnmapViewOfFile(address); }
    View() = default;
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    void* address = nullptr;
    size_t size = 0;
};

// Plain fixed-width wire values. All reads/writes are under the named mutex;
// no STL objects, addresses or C++ atomics cross a process boundary.
struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t slots;
    uint32_t record_bytes;
    uint64_t published;
    uint64_t process_started;
    uint32_t process_id;
    uint32_t closed;
};
struct Slot { uint64_t sequence; uint32_t size; uint32_t reserved; };
static_assert(sizeof(Header) == 40 && sizeof(Slot) == 16);

struct Channel {
    Handle mutex;
    Handle mapping;
    Handle producer;
    View view;
    Header& header() const { return *static_cast<Header*>(view.address); }
    std::byte* slot(uint64_t sequence) const;
    void validate() const;
    bool producer_exited() const;
    static std::unique_ptr<Channel> create(const char*, uint32_t slots, uint32_t bytes);
    static std::unique_ptr<Channel> open(const char*);
};

class Lock {
public:
    explicit Lock(HANDLE mutex, DWORD timeout);
    ~Lock() { if (owned_) ReleaseMutex(mutex_); }
    bool abandoned() const { return abandoned_; }
private:
    HANDLE mutex_;
    bool owned_ = false;
    bool abandoned_ = false;
};

template<typename F> owh_result boundary(F&& action) noexcept {
    try { return action(); }
    catch (const Failure& error) { return error.result; }
    catch (const std::bad_alloc&) { return {OWH_RESOURCE_EXHAUSTED, 0}; }
    catch (...) { return {OWH_SYSTEM_ERROR, ERROR_UNHANDLED_EXCEPTION}; }
}
inline constexpr owh_result ok{OWH_OK, 0};
}
