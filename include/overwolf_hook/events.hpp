#pragma once

#include <overwolf_hook/abi/events.h>
#include <overwolf_hook/sdk.hpp>
#include <overwolf_hook/wait.hpp>

#include <optional>
#include <utility>

namespace overwolf_hook {

class EventSubscription final {
  public:
    EventSubscription(const EventSubscription &) = delete;
    EventSubscription &operator=(const EventSubscription &) = delete;
    EventSubscription(EventSubscription &&) noexcept = default;
    EventSubscription &operator=(EventSubscription &&other) noexcept {
        if (this != &other) {
            close_noexcept();
            slot_ = std::move(other.slot_);
        }
        return *this;
    }
    ~EventSubscription() noexcept { close_noexcept(); }

    owh_subscription_info info() const {
        owh_subscription_info info{};
        info.size = sizeof(info);
        check(owh_subscription_query(require_slot().get(), &info));
        return info;
    }

    std::optional<owh_event> poll() const {
        owh_event event{};
        const auto result = owh_subscription_poll(require_slot().get(), &event);
        if (result.status == OWH_NO_DATA || result.status == OWH_BUSY)
            return std::nullopt;
        check(result);
        return event;
    }

    /** Low-level options overload. NULL receives indefinitely without cancellation. */
    std::optional<owh_event> receive(const owh_wait_options *options) const {
        owh_event event{};
        const auto result = owh_subscription_receive(require_slot().get(), options, &event);
        if (result.status == OWH_TIMEOUT || result.status == OWH_NO_DATA ||
            result.status == OWH_BUSY)
            return std::nullopt;
        check(result);
        return event;
    }

    std::optional<owh_event> receive(uint32_t timeout_ms = OWH_WAIT_INFINITE,
                                     const WaitCancellation *cancellation = nullptr) const {
        owh_wait_options options{};
        owh_wait_options_init(&options);
        options.timeout_ms = timeout_ms;
        if (cancellation) {
            options.cancellation = cancellation->require_handle().get();
        }
        return receive(&options);
    }

    void close() {
        if (!slot_)
            return;
        check(slot_->close_result(owh_subscription_close));
    }

  private:
    friend class EventController;
    explicit EventSubscription(owh_subscription *value)
        : slot_(std::make_unique<detail::HandleSlot<owh_subscription>>(value)) {}
    detail::HandleSlot<owh_subscription> &require_slot() const {
        if (!slot_)
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return *slot_;
    }
    void close_noexcept() noexcept {
        if (slot_)
            slot_->close_void(owh_subscription_close);
    }
    std::unique_ptr<detail::HandleSlot<owh_subscription>> slot_;
};

class EventController final {
  public:
    owh_event_capabilities capabilities() const {
        auto context = detail::lock_context(context_);
        owh_event_capabilities result{};
        result.size = sizeof(result);
        check(owh_event_query_capabilities(context->handle.get(), &result));
        return result;
    }

    EventSubscription subscribe(const owh_event_options *options = nullptr) const {
        auto context = detail::lock_context(context_);
        owh_subscription *subscription = nullptr;
        const auto status = owh_event_subscribe(context->handle.get(), options, &subscription);
        detail::NativeHandleGuard<owh_subscription, owh_subscription_close> adoption(subscription);
        check(status);
        if (!subscription)
            throw NativeError({OWH_CORRUPT, OWH_STAGE_ALLOCATION, OWH_ERROR_NONE, 0, 0});
        EventSubscription result(adoption.get());
        adoption.release();
        return result;
    }

  private:
    friend class OverwolfHook;
    explicit EventController(std::weak_ptr<detail::SdkContext> context)
        : context_(std::move(context)) {}
    std::weak_ptr<detail::SdkContext> context_;
};

inline EventController OverwolfHook::events() const {
    (void)require_context();
    return EventController(context_);
}

} // namespace overwolf_hook
