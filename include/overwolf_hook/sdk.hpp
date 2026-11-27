#pragma once

#include <overwolf_hook/abi/sdk.h>
#include <overwolf_hook/detail/handle.hpp>

#include <memory>
#include <utility>

namespace overwolf_hook {

class OverlayController;
class EventController;
class GameStateController;
class OverwolfController;

namespace detail {

class SdkContext final {
  public:
    explicit SdkContext(owh_sdk *value) : handle(value) {}
    HandleSlot<owh_sdk> handle;
};

inline std::shared_ptr<SdkContext> lock_context(const std::weak_ptr<SdkContext> &context) {
    auto locked = context.lock();
    if (!locked)
        throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
    return locked;
}

} // namespace detail

/** One independently attachable SDK context. Only the loaded native module is process-wide. */
class OverwolfHook final {
  public:
    OverwolfHook(const OverwolfHook &) = delete;
    OverwolfHook &operator=(const OverwolfHook &) = delete;
    OverwolfHook(OverwolfHook &&) noexcept = default;
    OverwolfHook &operator=(OverwolfHook &&other) noexcept {
        if (this != &other) {
            close();
            context_ = std::move(other.context_);
        }
        return *this;
    }
    ~OverwolfHook() noexcept { close(); }

    static OverwolfHook open() {
        owh_sdk *value = nullptr;
        const auto status = owh_sdk_open(&value);
        detail::NativeHandleGuard<owh_sdk, owh_sdk_close> adoption(value);
        check(status);
        if (!value)
            throw NativeError({OWH_CORRUPT, OWH_STAGE_ALLOCATION, OWH_ERROR_NONE, 0, 0});
        auto context = std::make_shared<detail::SdkContext>(adoption.get());
        adoption.release();
        return OverwolfHook(std::move(context));
    }

    /** Defaults to a normal Overwolf restart and early startup attachment.
     * Explicit OWH_ATTACH_HOT accepts the initial race in an already running host. */
    void attach(const owh_client_options *options = nullptr) {
        auto context = require_context();
        check(owh_sdk_attach(context->handle.get(), options));
    }

    void detach() {
        auto context = require_context();
        const auto result = owh_sdk_detach(context->handle.get());
        // The native contract detaches locally even when the remote result fails.
        check(result);
    }

    owh_client_sanity_report check_client(const owh_client_options *options = nullptr) const {
        owh_client_sanity_report report{};
        report.size = sizeof(report);
        check(owh_sdk_check(options, &report));
        return report;
    }

    owh_agent_info probe_agent(const owh_client_options *options = nullptr) const {
        owh_agent_info info{};
        info.size = sizeof(info);
        check(owh_sdk_probe_agent(options, &info));
        return info;
    }

    owh_agent_info agent_info() const {
        auto context = require_context();
        owh_agent_info info{};
        info.size = sizeof(info);
        check(owh_sdk_agent_info(context->handle.get(), &info));
        return info;
    }

    void ping(uint32_t timeout_ms = 1000) const {
        auto context = require_context();
        check(owh_sdk_ping(context->handle.get(), timeout_ms));
    }

    void close() noexcept {
        if (!context_)
            return;
        context_->handle.close_void(owh_sdk_close);
    }

    static uint32_t abi_version() noexcept { return owh_sdk_abi_version(); }
    static owh_build_info build_info() {
        owh_build_info output{};
        check(owh_sdk_build_info(&output));
        return output;
    }

    OverlayController overlays() const;
    EventController events() const;
    GameStateController game_state() const;
    OverwolfController overwolf() const;

  private:
    friend class OverlayController;
    friend class EventController;
    friend class GameStateController;
    explicit OverwolfHook(std::shared_ptr<detail::SdkContext> context)
        : context_(std::move(context)) {}
    std::shared_ptr<detail::SdkContext> require_context() const {
        if (!context_ || !context_->handle.open())
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return context_;
    }
    std::shared_ptr<detail::SdkContext> context_;
};

} // namespace overwolf_hook
