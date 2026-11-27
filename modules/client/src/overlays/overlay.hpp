#pragma once
#include "session/attached_session.hpp"
#include <mutex>
#include <owh/ipc/pixel_stream.hpp>
#include <owh/platform/handle.hpp>
#include <vector>

namespace overwolf_hook::client {
class PixelFrame;
class Submission;
struct OverlayBacking;
struct CompletionState;

class Overlay final {
  public:
    static std::pair<owh_result, std::unique_ptr<Overlay>> create(std::shared_ptr<AttachedSession>,
                                                                  const owh_overlay_spec &,
                                                                  const owh_pixel_buffer_overlay_options *,
                                                                  uint32_t kind);
    ~Overlay();
    owh_result info(owh_overlay_info_data &) noexcept;
    owh_result bind(const owh_legacy_shared_texture &) noexcept;
    owh_result close() noexcept;
    std::pair<owh_result, std::unique_ptr<PixelFrame>> acquire();
    std::pair<owh_result, std::unique_ptr<PixelFrame>> prepare(
        uint64_t source_bytes, uint32_t source_stride, uint32_t timeout_ms,
        HANDLE cancellation, bool immediate = false, const owh_pixel_region *damage = nullptr);

  private:
    std::shared_ptr<OverlayBacking> backing_;
};

/** Exclusive local write lease. The backing, rather than a parent handle,
 * guarantees that pixels remain mapped until commit/discard. */
class PixelFrame final {
  public:
    ~PixelFrame();
    owh_pixel_buffer view() const noexcept;
    owh_result set_damage(const owh_pixel_region *) noexcept;
    std::pair<owh_result, std::unique_ptr<Submission>> commit(bool tracked);

  private:
    friend class Overlay;
    std::shared_ptr<OverlayBacking> backing_;
    ipc::PixelProducer::Writable writable_{};
    owh_pixel_region damage_{};
    bool consumed_ = false;
    bool managed_ = false;
};

class Submission final {
  public:
    owh_result poll(owh_submission_info &) noexcept;
    owh_result wait(uint32_t timeout_ms, HANDLE cancellation, owh_submission_info &) noexcept;

  private:
    friend class PixelFrame;
    std::shared_ptr<CompletionState> state_;
};
} // namespace overwolf_hook::client
