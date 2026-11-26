#pragma once

#include <owh/core/types.hpp>
#include <owh/ipc/pixel_stream.hpp>
#include <owh/overwolf/overlays/overlay_host.hpp>

#include <memory>
#include <utility>

namespace overwolf_hook::agent {

/** Agent-owned overlay resources and pixel stream drain. OverlayHost
 * supplies only actor-thread work scheduling and opaque native handles.
 */
class OverlayDomain final {
  public:
    explicit OverlayDomain(overwolf::OverlayHost &);
    ~OverlayDomain();
    OverlayDomain(const OverlayDomain &) = delete;
    OverlayDomain &operator=(const OverlayDomain &) = delete;

    std::pair<owh_result, core::ResourceRef> create_pixel_buffer(core::ResourceRef, core::Geometry,
                                             const core::PixelBufferRef &, HANDLE verified_peer,
                                             std::shared_ptr<const core::SessionPolicy>);
    std::pair<owh_result, core::ResourceRef> create_shared_texture(core::ResourceRef, core::Geometry,
                                              std::shared_ptr<const core::SessionPolicy>);
    owh_result bind(core::ResourceRef, core::LegacySharedTexture) noexcept;
    std::pair<owh_result, owh_overlay_info_data> info(core::ResourceRef) noexcept;
    owh_result close(core::ResourceRef) noexcept;
    owh_overlay_capabilities capabilities(core::SessionPolicy, uint32_t process_id,
                                          uint64_t process_started,
                                          uint64_t session_id) const noexcept;
    owh_result retire(uint64_t session_id) noexcept;
    uint32_t child_count(uint64_t session_id) const noexcept;

  private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace overwolf_hook::agent
