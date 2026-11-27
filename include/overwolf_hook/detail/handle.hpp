#pragma once

#include <overwolf_hook/types/common.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace overwolf_hook {

class NativeError final : public std::runtime_error {
  public:
    explicit NativeError(owh_result result)
        : std::runtime_error("Overwolf Hook operation failed (status " +
                             std::to_string(result.status) + ")"),
          result_(result) {}
    const owh_result &result() const noexcept { return result_; }

  private:
    owh_result result_;
};

inline void check(owh_result result) {
    if (result.status != OWH_OK)
        throw NativeError(result);
}

namespace detail {

template <class T, auto Release> class NativeHandleGuard final {
  public:
    explicit NativeHandleGuard(T *handle) noexcept : handle_(handle) {}
    NativeHandleGuard(const NativeHandleGuard &) = delete;
    NativeHandleGuard &operator=(const NativeHandleGuard &) = delete;
    ~NativeHandleGuard() noexcept { reset(); }
    T *get() const noexcept { return handle_; }
    T *release() noexcept { return std::exchange(handle_, nullptr); }

  private:
    void reset() noexcept {
        if (!handle_)
            return;
        T *value = std::exchange(handle_, nullptr);
        try {
            if constexpr (std::is_void_v<decltype(Release(value))>)
                Release(value);
            else
                (void)Release(value);
        } catch (...) {
        }
    }
    T *handle_ = nullptr;
};

/** A local owning handle slot. Callers serialize use and close themselves. */
template <class T> class HandleSlot final {
  public:
    explicit HandleSlot(T *handle = nullptr) noexcept : handle_(handle) {}
    HandleSlot(const HandleSlot &) = delete;
    HandleSlot &operator=(const HandleSlot &) = delete;

    T *get() const { return require_open(); }

    bool open() const noexcept { return handle_ != nullptr; }

    template <class Release> owh_result close_result(Release &&release) {
        return release_and_consume(std::forward<Release>(release), false);
    }

    template <class Release> owh_result consume_result(Release &&operation) {
        return release_and_consume(std::forward<Release>(operation), true);
    }

    template <class Release> void close_void(Release &&release) noexcept {
        try {
            (void)close_result([&](T *handle) {
                release(handle);
                return owh_result{OWH_OK, 0, 0, 0, 0};
            });
        } catch (...) { /* Destruction is non-throwing; explicit close exposes errors. */ }
    }

  private:
    template <class Release> owh_result release_and_consume(Release &&release, bool require_open) {
        if (!handle_)
            return require_open ? owh_result{OWH_CLOSED, 0, 0, 0, 0}
                                : owh_result{OWH_OK, 0, 0, 0, 0};
        T *handle = handle_;
        owh_result result{OWH_OK, 0, 0, 0, 0};
        result = release(handle);
        handle_ = nullptr;
        return result;
    }
    T *require_open() const {
        if (!handle_)
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return handle_;
    }
    T *handle_ = nullptr;
};

} // namespace detail
} // namespace overwolf_hook
