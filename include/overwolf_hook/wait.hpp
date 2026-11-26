#pragma once

#include <overwolf_hook/abi/wait.h>
#include <overwolf_hook/detail/handle.hpp>

#include <memory>
#include <utility>

namespace overwolf_hook {

/** Shared wait token. Signal may run alongside waits; join them before close. */
class WaitCancellation final {
  public:
    WaitCancellation(const WaitCancellation &) = delete;
    WaitCancellation &operator=(const WaitCancellation &) = delete;
    WaitCancellation(WaitCancellation &&) noexcept = default;
    WaitCancellation &operator=(WaitCancellation &&other) noexcept {
        if (this != &other) {
            close();
            handle_ = std::move(other.handle_);
        }
        return *this;
    }
    ~WaitCancellation() noexcept { close(); }

    static WaitCancellation create() {
        owh_cancel *value = nullptr;
        const auto status = owh_cancel_create(&value);
        detail::NativeHandleGuard<owh_cancel, owh_cancel_close> adoption(value);
        check(status);
        if (!value)
            throw NativeError({OWH_CORRUPT, OWH_STAGE_ALLOCATION, OWH_ERROR_NONE, 0, 0});
        WaitCancellation result(adoption.get());
        adoption.release();
        return result;
    }

    void signal() const {
        owh_cancel_signal(require_handle().get());
    }

    void close() noexcept {
        if (handle_) handle_->close_void(owh_cancel_close);
    }

  private:
    friend class Submission;
    friend class PixelBufferOverlay;
    friend class EventSubscription;
    explicit WaitCancellation(owh_cancel *value)
        : handle_(std::make_unique<detail::HandleSlot<owh_cancel>>(value)) {}
    detail::HandleSlot<owh_cancel> &require_handle() const {
        if (!handle_)
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return *handle_;
    }
    std::unique_ptr<detail::HandleSlot<owh_cancel>> handle_;
};

} // namespace overwolf_hook
