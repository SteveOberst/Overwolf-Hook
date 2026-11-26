#pragma once
#include "../session_context.hpp"
#include "overlay_domain.hpp"
#include <owh/core/services.hpp>
#include <owh/platform/handle.hpp>
namespace overwolf_hook::ipc {
class MessageConnection;
}
namespace overwolf_hook::agent {
class OverlayServiceImpl final : public core::OverlayService {
  public:
    OverlayServiceImpl(OverlayDomain &, ipc::MessageConnection &, SessionContext &);
    core::CreateSourceResponse
    create_pixel_buffer(const core::CreatePixelBufferSourceRequest &) noexcept override;
    core::ResultResponse close(const core::SourceRequest &) noexcept override;
    core::ResultResponse bind_texture(const core::BindD3D11Request &) noexcept override;
    core::OverlayInfoResponse get_info(const core::SourceRequest &) noexcept override;
    core::CreateSourceResponse
    create_shared_texture(const core::CreateSharedTextureSourceRequest &) noexcept override;
    core::OverlayCapabilitiesResponse capabilities(const core::Empty &) noexcept override;

  private:
    OverlayDomain &domain_;
    Handle peer_;
    SessionContext &context_;
};

} // namespace overwolf_hook::agent
