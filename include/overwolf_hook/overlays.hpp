#pragma once

#include <overwolf_hook/abi/overlays.h>
#include <overwolf_hook/sdk.hpp>
#include <overwolf_hook/wait.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace overwolf_hook {

namespace detail {

class OverlayHandle final {
  public:
    explicit OverlayHandle(owh_overlay *value)
        : slot_(std::make_unique<HandleSlot<owh_overlay>>(value)) {}
    OverlayHandle(const OverlayHandle &) = delete;
    OverlayHandle &operator=(const OverlayHandle &) = delete;
    ~OverlayHandle() noexcept { close_noexcept(); }

    owh_overlay_info_data query() const {
        owh_overlay_info_data info{};
        info.size = sizeof(info);
        check(owh_overlay_query(slot_->get(), &info));
        return info;
    }

    void bind(const owh_legacy_shared_texture &texture) const {
        check(owh_overlay_bind_d3d11(slot_->get(), &texture));
    }

    owh_result close() {
        return slot_->close_result(owh_overlay_close);
    }

    void close_checked() { check(close()); }

    void close_noexcept() noexcept {
        if (!slot_)
            return;
        try {
            (void)slot_->close_result(owh_overlay_close);
        } catch (...) {}
    }

    HandleSlot<owh_overlay> &slot() noexcept { return *slot_; }
  private:
    std::unique_ptr<HandleSlot<owh_overlay>> slot_;
};

} // namespace detail

class Submission final {
  public:
    Submission(const Submission &) = delete;
    Submission &operator=(const Submission &) = delete;
    Submission(Submission &&) noexcept = default;
    Submission &operator=(Submission &&other) noexcept {
        if (this != &other) {
            close();
            slot_ = std::move(other.slot_);
        }
        return *this;
    }
    ~Submission() noexcept { close(); }

    owh_submission_info poll() const {
        owh_submission_info info{};
        info.size = sizeof(info);
        check(owh_submission_poll(require_slot().get(), &info));
        return info;
    }

    owh_submission_info wait(uint32_t timeout_ms = OWH_WAIT_INFINITE,
                             const WaitCancellation *cancellation = nullptr) const {
        owh_cancel *token = nullptr;
        if (cancellation) {
            token = cancellation->require_handle().get();
        }
        owh_wait_options options{};
        owh_wait_options_init(&options);
        options.timeout_ms = timeout_ms;
        options.cancellation = token;
        owh_submission_info info{};
        info.size = sizeof(info);
        check(owh_submission_wait(require_slot().get(), &options, &info));
        return info;
    }

    void close() noexcept {
        if (slot_) slot_->close_void(owh_submission_close);
    }

  private:
    friend class PixelFrame;
    friend class PixelBufferOverlay;
    explicit Submission(owh_submission *value)
        : slot_(std::make_unique<detail::HandleSlot<owh_submission>>(value)) {}
    detail::HandleSlot<owh_submission> &require_slot() const {
        if (!slot_)
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return *slot_;
    }
    std::unique_ptr<detail::HandleSlot<owh_submission>> slot_;
};

class PixelFrame final {
  public:
    PixelFrame(const PixelFrame &) = delete;
    PixelFrame &operator=(const PixelFrame &) = delete;
    PixelFrame(PixelFrame &&) noexcept = default;
    PixelFrame &operator=(PixelFrame &&other) noexcept {
        if (this != &other) {
            close();
            slot_ = std::move(other.slot_);
            view_ = other.view_;
        }
        return *this;
    }
    ~PixelFrame() noexcept { close(); }

    owh_pixel_buffer layout() const {
        (void)require_slot().get();
        auto value = view_;
        value.pixels = nullptr;
        return value;
    }

    /** Borrowed span; serialize its use against commit/discard and keep it inside this frame's
     * lifetime. */
    std::span<std::byte> pixels() const {
        (void)require_slot().get();
        if (!view_.pixels && view_.pixel_bytes != 0)
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return {static_cast<std::byte *>(view_.pixels),
                static_cast<std::size_t>(view_.pixel_bytes)};
    }

    void commit() {
        const auto result = require_slot().consume_result(
            [](owh_frame *frame) { return owh_frame_commit(frame, nullptr); });
        view_.pixels = nullptr;
        check(result);
    }
    /** Optional hint for this complete frame. nullptr resets full-image damage. */
    void set_damage(const owh_pixel_region *damage = nullptr) {
        check(owh_frame_set_damage(require_slot().get(), damage));
    }
    void set_damage(const owh_pixel_region &damage) { set_damage(&damage); }

    Submission commit_tracked() {
        owh_submission *receipt = nullptr;
        const auto status = require_slot().consume_result(
            [&](owh_frame *frame) { return owh_frame_commit(frame, &receipt); });
        view_.pixels = nullptr;
        detail::NativeHandleGuard<owh_submission, owh_submission_close> adoption(receipt);
        check(status);
        if (!receipt)
            throw NativeError({OWH_CORRUPT, OWH_STAGE_CLEANUP, OWH_ERROR_NONE, 0, 0});
        Submission result(adoption.get());
        adoption.release();
        return result;
    }

    void close() noexcept {
        if (slot_)
            slot_->close_void(owh_frame_discard);
        view_.pixels = nullptr;
    }

  private:
    friend class PixelBufferOverlay;
    PixelFrame(owh_frame *frame, owh_pixel_buffer view)
        : slot_(std::make_unique<detail::HandleSlot<owh_frame>>(frame)), view_(view) {}
    detail::HandleSlot<owh_frame> &require_slot() const {
        if (!slot_)
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return *slot_;
    }
    std::unique_ptr<detail::HandleSlot<owh_frame>> slot_;
    owh_pixel_buffer view_{};
};

class PixelBufferOverlay final {
  public:
    PixelBufferOverlay(const PixelBufferOverlay &) = delete;
    PixelBufferOverlay &operator=(const PixelBufferOverlay &) = delete;
    PixelBufferOverlay(PixelBufferOverlay &&) noexcept = default;
    PixelBufferOverlay &operator=(PixelBufferOverlay &&other) noexcept {
        if (this != &other) {
            close_noexcept();
            handle_ = std::move(other.handle_);
        }
        return *this;
    }
    ~PixelBufferOverlay() noexcept { close_noexcept(); }

    /** Default image handoff. Copies once; latest replaces an unread image.
     * FIFO waits for capacity using the optional timeout/cancellation. */
    void submit(std::span<const std::byte> image, uint32_t stride = 0,
                uint32_t timeout_ms = OWH_WAIT_INFINITE,
                const WaitCancellation *cancellation = nullptr,
                const owh_pixel_region *damage = nullptr) const {
        const auto wait = submission_wait(timeout_ms, cancellation);
        check(owh_overlay_submit_pixels(require_handle().slot().get(), image.data(), image.size(),
                                        stride, damage, &wait, nullptr));
    }
    /** Optional changed area; image still contains every pixel. */
    void submit(std::span<const std::byte> image, const owh_pixel_region &damage,
                uint32_t stride = 0, uint32_t timeout_ms = OWH_WAIT_INFINITE,
                const WaitCancellation *cancellation = nullptr) const {
        submit(image, stride, timeout_ms, cancellation, &damage);
    }
    Submission submit_tracked(std::span<const std::byte> image, uint32_t stride = 0,
                              uint32_t timeout_ms = OWH_WAIT_INFINITE,
                              const WaitCancellation *cancellation = nullptr,
                              const owh_pixel_region *damage = nullptr) const {
        const auto wait = submission_wait(timeout_ms, cancellation);
        owh_submission *receipt = nullptr;
        const auto status = owh_overlay_submit_pixels(require_handle().slot().get(), image.data(),
                                                     image.size(), stride, damage, &wait, &receipt);
        detail::NativeHandleGuard<owh_submission, owh_submission_close> adoption(receipt);
        check(status);
        if (!receipt) throw NativeError({OWH_CORRUPT, OWH_STAGE_ALLOCATION, OWH_ERROR_NONE, 0, 0});
        Submission result(adoption.get());
        adoption.release();
        return result;
    }
    Submission submit_tracked(std::span<const std::byte> image, const owh_pixel_region &damage,
                              uint32_t stride = 0, uint32_t timeout_ms = OWH_WAIT_INFINITE,
                              const WaitCancellation *cancellation = nullptr) const {
        return submit_tracked(image, stride, timeout_ms, cancellation, &damage);
    }
    /** Explicit drop-if-full admission. false means the image was not submitted. */
    bool try_submit(std::span<const std::byte> image, uint32_t stride = 0,
                    const owh_pixel_region *damage = nullptr) const {
        const auto result = owh_overlay_try_submit_pixels(require_handle().slot().get(), image.data(),
                                                         image.size(), stride, damage, nullptr);
        if (result.status == OWH_BUSY) return false;
        check(result);
        return true;
    }
    bool try_submit(std::span<const std::byte> image, const owh_pixel_region &damage,
                    uint32_t stride = 0) const {
        return try_submit(image, stride, &damage);
    }

    std::optional<PixelFrame> try_acquire() const {
        auto &handle = require_handle();
        auto *overlay = handle.slot().get();
        owh_frame *frame = nullptr;
        owh_pixel_buffer view{};
        view.size = sizeof(view);
        const auto result = owh_frame_acquire(overlay, &frame, &view);
        if (result.status == OWH_BUSY) {
            if (frame)
                owh_frame_discard(frame);
            return std::nullopt;
        }
        detail::NativeHandleGuard<owh_frame, owh_frame_discard> adoption(frame);
        check(result);
        if (!frame)
            throw NativeError({OWH_CORRUPT, OWH_STAGE_ALLOCATION, OWH_ERROR_NONE, 0, 0});
        PixelFrame result_frame(adoption.get(), view);
        adoption.release();
        return result_frame;
    }

    owh_overlay_info_data info() const { return require_handle().query(); }
    void close() {
        if (handle_)
            handle_->close_checked();
    }
    void close_noexcept() noexcept {
        if (handle_)
            handle_->close_noexcept();
    }

  private:
    static owh_wait_options submission_wait(uint32_t timeout, const WaitCancellation *cancellation) {
        owh_wait_options wait{};
        owh_wait_options_init(&wait);
        wait.timeout_ms = timeout;
        wait.cancellation = cancellation ? cancellation->require_handle().get() : nullptr;
        return wait;
    }
    friend class OverlayController;
    explicit PixelBufferOverlay(std::unique_ptr<detail::OverlayHandle> handle)
        : handle_(std::move(handle)) {}
    detail::OverlayHandle &require_handle() const {
        if (!handle_)
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return *handle_;
    }
    std::unique_ptr<detail::OverlayHandle> handle_;
};

class SharedTextureOverlay final {
  public:
    SharedTextureOverlay(const SharedTextureOverlay &) = delete;
    SharedTextureOverlay &operator=(const SharedTextureOverlay &) = delete;
    SharedTextureOverlay(SharedTextureOverlay &&) noexcept = default;
    SharedTextureOverlay &operator=(SharedTextureOverlay &&other) noexcept {
        if (this != &other) {
            close_noexcept();
            handle_ = std::move(other.handle_);
        }
        return *this;
    }
    ~SharedTextureOverlay() noexcept { close_noexcept(); }

    void bind(const owh_legacy_shared_texture &texture) const { require_handle().bind(texture); }
    owh_overlay_info_data info() const { return require_handle().query(); }
    void close() {
        if (handle_)
            handle_->close_checked();
    }
    void close_noexcept() noexcept {
        if (handle_)
            handle_->close_noexcept();
    }

  private:
    friend class OverlayController;
    explicit SharedTextureOverlay(std::unique_ptr<detail::OverlayHandle> handle)
        : handle_(std::move(handle)) {}
    detail::OverlayHandle &require_handle() const {
        if (!handle_)
            throw NativeError({OWH_CLOSED, 0, 0, 0, 0});
        return *handle_;
    }
    std::unique_ptr<detail::OverlayHandle> handle_;
};

class OverlayController final {
  public:
    owh_overlay_capabilities capabilities() const {
        auto context = detail::lock_context(context_);
        owh_overlay_capabilities output{};
        output.size = sizeof(output);
        check(owh_overlay_query_capabilities(context->handle.get(), &output));
        return output;
    }

    PixelBufferOverlay create_pixel_buffer(const owh_overlay_spec &spec,
                               const owh_pixel_buffer_overlay_options *queue = nullptr) const {
        auto context = detail::lock_context(context_);
        owh_overlay *value = nullptr;
        const auto status = owh_overlay_create_pixel_buffer(context->handle.get(), &spec, queue, &value);
        detail::NativeHandleGuard<owh_overlay, owh_overlay_close> adoption(value);
        check(status);
        if (!value)
            throw NativeError({OWH_CORRUPT, OWH_STAGE_ALLOCATION, OWH_ERROR_NONE, 0, 0});
        auto handle = std::make_unique<detail::OverlayHandle>(adoption.get());
        adoption.release();
        return PixelBufferOverlay(std::move(handle));
    }

    SharedTextureOverlay create_shared_texture(const owh_overlay_spec &spec,
                                                const owh_legacy_shared_texture &texture) const {
        auto context = detail::lock_context(context_);
        owh_overlay *value = nullptr;
        const auto status = owh_overlay_create_shared_texture(context->handle.get(), &spec, &texture, &value);
        detail::NativeHandleGuard<owh_overlay, owh_overlay_close> adoption(value);
        check(status);
        if (!value) throw NativeError({OWH_CORRUPT, OWH_STAGE_ALLOCATION, OWH_ERROR_NONE, 0, 0});
        auto handle = std::make_unique<detail::OverlayHandle>(adoption.get());
        adoption.release();
        return SharedTextureOverlay(std::move(handle));
    }

    SharedTextureOverlay create_unbound_shared_texture(const owh_overlay_spec &spec) const {
        auto context = detail::lock_context(context_);
        owh_overlay *value = nullptr;
        const auto status = owh_overlay_create_unbound_shared_texture(context->handle.get(), &spec, &value);
        detail::NativeHandleGuard<owh_overlay, owh_overlay_close> adoption(value);
        check(status);
        if (!value)
            throw NativeError({OWH_CORRUPT, OWH_STAGE_ALLOCATION, OWH_ERROR_NONE, 0, 0});
        auto handle = std::make_unique<detail::OverlayHandle>(adoption.get());
        adoption.release();
        return SharedTextureOverlay(std::move(handle));
    }

  private:
    friend class OverwolfHook;
    explicit OverlayController(std::weak_ptr<detail::SdkContext> context)
        : context_(std::move(context)) {}
    std::weak_ptr<detail::SdkContext> context_;
};

inline OverlayController OverwolfHook::overlays() const {
    (void)require_context();
    return OverlayController(context_);
}

} // namespace overwolf_hook
