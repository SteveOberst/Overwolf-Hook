#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <overwolf_hook/types/common.h>
#include <owh/core/types.hpp>
#include <owh/overwolf/overlays/pixel_view.hpp>
#include <owh/overwolf/signatures/resolver.hpp>
#include <span>

namespace overwolf_hook::overwolf {
class LifetimeAdmission;

/** Native Overwolf source boundary. It owns private ABI calls, connector
 * witnesses, and the Admission/registry lease ordering for register, publish,
 * and cleanup.
 */
class SourceAdapter final {
  public:
    struct Registration {
        HWND connector = nullptr;
        uint64_t epoch = 0;
        bool registered = false;
    };
    struct Geometry {
        uint32_t width, height;
        int32_t left, top;
    };

    SourceAdapter(LifetimeAdmission &, const ResolvedProfile &) noexcept;
    owh_result register_source(HWND window, Geometry, Registration &) noexcept;
    owh_result publish_pixels(HWND window, core::ResourceRef, const Registration &,
                              const overwolf::PixelView &) noexcept;
    owh_result bind_texture(HWND window, Geometry, uint64_t token, Registration &) noexcept;
    owh_result remove_source(Registration &) noexcept;

  private:
    owh_result register_locked(uint64_t root, uint64_t epoch, HWND window, Geometry,
                               Registration &) noexcept;
    owh_result refresh_windows(uint64_t root, HWND) noexcept;
    LifetimeAdmission &admission_;
    ResolvedProfile profile_;
};
} // namespace overwolf_hook::overwolf
