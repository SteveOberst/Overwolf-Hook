#include <dxgi.h>
#include <new>
#include <owh/overwolf/overlays/d3d11_raw_resource.hpp>
#include <utility>

namespace overwolf_hook::overwolf {
namespace {
owh_result result(owh_status status, uint32_t stage = OWH_STAGE_NONE,
                  HRESULT code = S_OK) noexcept {
    return {status, stage, static_cast<uint32_t>(code == S_OK ? OWH_ERROR_NONE : OWH_ERROR_HRESULT),
            0, static_cast<uint32_t>(code)};
}
owh_result failure(HRESULT code, uint32_t stage) noexcept {
    const bool removed = code == DXGI_ERROR_DEVICE_REMOVED || code == DXGI_ERROR_DEVICE_RESET ||
                         code == DXGI_ERROR_DEVICE_HUNG || code == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    return result(removed ? OWH_DEVICE_LOST : OWH_SYSTEM_ERROR, stage, code);
}
} // namespace

std::pair<owh_result, std::unique_ptr<D3D11RawResource>>
D3D11RawResource::import(const core::LegacySharedTexture &input, uint32_t width, uint32_t height,
                         uint64_t budget) noexcept {
    using Microsoft::WRL::ComPtr;
    if (input.size != sizeof(input) || input.reserved || !input.legacy_shared_token || !width ||
        !height)
        return {result(OWH_INVALID_ARGUMENT, OWH_STAGE_D3D11_VALIDATE), nullptr};
    // The product limits dimensions before import. Keep this private boundary
    // overflow-safe independently, including a fixture or future caller.
    const uint64_t pixels = uint64_t{width} * height;
    if (pixels > UINT64_MAX / 4 || pixels * 4 > budget)
        return {result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION), nullptr};
    try {
        ComPtr<IDXGIFactory1> factory;
        HRESULT code = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(code))
            return {failure(code, OWH_STAGE_D3D11_ADAPTER), nullptr};
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            code = factory->EnumAdapters1(index, candidate.GetAddressOf());
            if (code == DXGI_ERROR_NOT_FOUND)
                return {result(OWH_UNSUPPORTED, OWH_STAGE_D3D11_ADAPTER), nullptr};
            if (FAILED(code))
                return {failure(code, OWH_STAGE_D3D11_ADAPTER), nullptr};
            DXGI_ADAPTER_DESC1 description{};
            code = candidate->GetDesc1(&description);
            if (FAILED(code))
                return {failure(code, OWH_STAGE_D3D11_ADAPTER), nullptr};
            if (description.AdapterLuid.LowPart == input.adapter_luid_low &&
                description.AdapterLuid.HighPart == input.adapter_luid_high) {
                adapter = std::move(candidate);
                break;
            }
        }
        auto pin = std::unique_ptr<D3D11RawResource>(new D3D11RawResource());
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
        code = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1, D3D11_SDK_VERSION,
                                 pin->device_.GetAddressOf(), nullptr, nullptr);
        if (FAILED(code))
            return {failure(code, OWH_STAGE_D3D11_DEVICE), nullptr};
        // OpenSharedResource is intentionally the legacy path. Substituting an
        // NT handle/keyed mutex would change Overwolf's reviewed source contract.
        code = pin->device_->OpenSharedResource(
            reinterpret_cast<HANDLE>(static_cast<uintptr_t>(input.legacy_shared_token)),
            IID_PPV_ARGS(pin->texture_.GetAddressOf()));
        if (FAILED(code))
            return {failure(code, OWH_STAGE_D3D11_IMPORT), nullptr};
        D3D11_TEXTURE2D_DESC actual{};
        pin->texture_->GetDesc(&actual);
        const UINT bindings = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (actual.Width != width || actual.Height != height ||
            actual.Format != DXGI_FORMAT_B8G8R8A8_UNORM || actual.MipLevels != 1 ||
            actual.ArraySize != 1 || actual.SampleDesc.Count != 1 || actual.SampleDesc.Quality ||
            actual.Usage != D3D11_USAGE_DEFAULT || actual.CPUAccessFlags ||
            actual.BindFlags != bindings || actual.MiscFlags != D3D11_RESOURCE_MISC_SHARED)
            return {result(OWH_INVALID_ARGUMENT, OWH_STAGE_D3D11_VALIDATE), nullptr};
        code = pin->device_->CreateShaderResourceView(pin->texture_.Get(), nullptr,
                                                      pin->view_.GetAddressOf());
        if (FAILED(code))
            return {failure(code, OWH_STAGE_D3D11_VALIDATE), nullptr};
        const auto healthy = pin->health();
        if (healthy.status != OWH_OK)
            return {healthy, nullptr};
        pin->token_ = input.legacy_shared_token;
        pin->allocation_bytes_ = pixels * 4;
        return {result(OWH_OK), std::move(pin)};
    } catch (const std::bad_alloc &) {
        return {result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION), nullptr};
    } catch (...) {
        return {result(OWH_SYSTEM_ERROR, OWH_STAGE_D3D11_IMPORT), nullptr};
    }
}

owh_result D3D11RawResource::health() const noexcept {
    const HRESULT code = device_->GetDeviceRemovedReason();
    return SUCCEEDED(code) ? result(OWH_OK) : result(OWH_DEVICE_LOST, OWH_STAGE_D3D11_DEVICE, code);
}
} // namespace overwolf_hook::overwolf
