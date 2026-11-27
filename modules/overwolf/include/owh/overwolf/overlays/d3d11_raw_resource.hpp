#pragma once

#include <d3d11.h>
#include <memory>
#include <overwolf_hook/types/overlays.h>
#include <owh/core/types.hpp>
#include <utility>
#include <wrl/client.h>

namespace overwolf_hook::overwolf {

/** Private import pin. Construction performs graphics work and must happen
 * outside LifetimeAdmission and the native registry lock. No upload, readback,
 * GPU wait, implicit adapter selection or writer/consumer handoff is provided.
 * The legacy token is borrowed metadata, never a CloseHandle-owned object.
 */
class D3D11RawResource final {
  public:
    static std::pair<owh_result, std::unique_ptr<D3D11RawResource>>
    import(const core::LegacySharedTexture &, uint32_t expected_width, uint32_t expected_height,
           uint64_t available_budget_bytes) noexcept;

    D3D11RawResource(const D3D11RawResource &) = delete;
    D3D11RawResource &operator=(const D3D11RawResource &) = delete;
    uint64_t token() const noexcept { return token_; }
    uint64_t allocation_bytes() const noexcept { return allocation_bytes_; }
    /** Observe removal only when already servicing an operation/Info request. */
    owh_result health() const noexcept;

  private:
    D3D11RawResource() = default;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    uint64_t token_ = 0;
    uint64_t allocation_bytes_ = 0;
};

} // namespace overwolf_hook::overwolf
