#pragma once

#include <owh/core/types.hpp>
#include <owh/overwolf/overlays/lifetime_admission.hpp>
#include <owh/overwolf/overlays/pixel_view.hpp>
#include <owh/platform/result.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace overwolf_hook::overwolf {

class NativeOverlay;

struct NativeOverlayResult {
    owh_result result{};
    std::shared_ptr<NativeOverlay> overlay;
};

/** Narrow work hook. The Agent owns all stream state; the host only invokes it
 * on the existing window actor and polls whether more work is ready.
 */
class OverlayWork {
  public:
    virtual ~OverlayWork() = default;
    virtual void process_on_actor() noexcept = 0;
    virtual bool has_pending_work() const noexcept = 0;
};

/** Opaque native source handle. It pins its host and contains only native
 * window/registration/texture lifetime state. Pixel bytes are borrowed per call.
 * All operations run on the OverlayHost actor, via execute() or its work pump;
 * this native adapter never dispatches work or synchronizes domain state itself.
 */
class NativeOverlay final {
  public:
    ~NativeOverlay();
    NativeOverlay(const NativeOverlay &) = delete;
    NativeOverlay &operator=(const NativeOverlay &) = delete;

    owh_result publish(core::ResourceRef, const PixelView &) noexcept;
    owh_result bind(core::LegacySharedTexture, uint64_t available_budget_bytes) noexcept;
    uint64_t texture_allocation_bytes() const noexcept;
    std::pair<owh_result, owh_overlay_info_data> info() noexcept;
    owh_result close() noexcept;
    bool needs_cleanup() const noexcept;
    bool closing() const noexcept;

  private:
    friend class OverlayHost;
    struct Impl;
    explicit NativeOverlay(std::shared_ptr<Impl>);
    std::shared_ptr<Impl> impl_;
};

/** Process-resident native overlay actor and factory. No resource-ID table or
 * pixel queue policy lives here. Agent callbacks are weak and explicitly
 * clearable; NativeOverlay pins the shared host implementation.
 */
class OverlayHost final {
  public:
    explicit OverlayHost(LifetimeAdmission &);
    ~OverlayHost();
    OverlayHost(const OverlayHost &) = delete;
    OverlayHost &operator=(const OverlayHost &) = delete;

    void set_work(std::weak_ptr<OverlayWork>) noexcept;
    void clear_work() noexcept;
    void wake() noexcept;
    /** Run one operation on the existing overlay actor. Domain transitions that
     * depend on native facts belong in the same callback. */
    owh_result execute(std::function<owh_result()> operation) noexcept;
    template <class F> owh_result execute(F &&operation) noexcept {
        try {
            return execute(std::function<owh_result()>(std::forward<F>(operation)));
        } catch (...) {
            return make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION);
        }
    }
    // Actor-only factories; enter through execute().
    NativeOverlayResult create_pixel_buffer(core::Geometry) noexcept;
    NativeOverlayResult create_shared_texture(core::Geometry) noexcept;
    void fill_capabilities(owh_overlay_capabilities &, bool d3d11_opt_in) const noexcept;

  private:
    friend class NativeOverlay;
    NativeOverlayResult create(uint32_t kind, core::Geometry) noexcept;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace overwolf_hook::overwolf
