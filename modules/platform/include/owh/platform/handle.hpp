#pragma once
#include <windows.h>
#include <cstddef>
#include <utility>

namespace overwolf_hook {
class Handle {
  public:
    Handle() = default;
    explicit Handle(HANDLE value) noexcept : value_(value) {}
    ~Handle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE)
            CloseHandle(value_);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle &operator=(Handle &&other) noexcept {
        std::swap(value_, other.value_);
        return *this;
    }
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = nullptr) noexcept {
        if (value_ == value)
            return;
        if (*this)
            CloseHandle(value_);
        value_ = value;
    }

  private:
    HANDLE value_ = nullptr;
};

class View {
  public:
    ~View() {
        if (address)
            UnmapViewOfFile(address);
    }
    View() = default;
    View(const View &) = delete;
    View &operator=(const View &) = delete;
    void *address = nullptr;
    size_t size = 0;
};

} // namespace overwolf_hook
