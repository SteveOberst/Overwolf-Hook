#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <overwolf_hook/overwolf_hook.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

static void check_d3d(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << ": HRESULT=0x" << std::hex
                << static_cast<uint32_t>(result);
        throw std::runtime_error(message.str());
    }
}

int main() {
    try {
        auto sdk = overwolf_hook::OverwolfHook::open();
        // --8<-- [start:attach]
        owh_client_options options{};
        owh_client_options_init(&options);
        options.experimental_features = OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE;
        sdk.attach(&options); // Default restart: run before entering the game.
        // --8<-- [end:attach]

        constexpr uint32_t width = 256, height = 128;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        check_d3d(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
            D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
            context.GetAddressOf()), "create device");

        // --8<-- [start:allocation]
        D3D11_TEXTURE2D_DESC shape{};
        shape.Width = width;
        shape.Height = height;
        shape.MipLevels = 1;
        shape.ArraySize = 1;
        shape.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        shape.SampleDesc.Count = 1;
        shape.Usage = D3D11_USAGE_DEFAULT;
        shape.BindFlags = D3D11_BIND_RENDER_TARGET |
                          D3D11_BIND_SHADER_RESOURCE;
        shape.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        ComPtr<ID3D11Texture2D> texture;
        check_d3d(device->CreateTexture2D(&shape, nullptr,
            texture.GetAddressOf()), "create shared texture");
        // --8<-- [end:allocation]

        // --8<-- [start:render]
        ComPtr<ID3D11RenderTargetView> target;
        check_d3d(device->CreateRenderTargetView(texture.Get(), nullptr,
            target.GetAddressOf()), "create render target");
        const float cyan[4]{0.0f, 0.75f, 1.0f, 1.0f}; // RGBA floats
        context->ClearRenderTargetView(target.Get(), cyan);
        // --8<-- [end:render]

        // --8<-- [start:completion]
        D3D11_QUERY_DESC query_spec{D3D11_QUERY_EVENT, 0};
        ComPtr<ID3D11Query> completed;
        check_d3d(device->CreateQuery(&query_spec, completed.GetAddressOf()),
                  "create completion query");
        context->End(completed.Get());
        context->Flush(); // Submit commands; the query proves completion.
        const auto deadline = GetTickCount64() + 5000;
        for (;;) {
            BOOL done = FALSE;
            const auto result = context->GetData(completed.Get(), &done,
                sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            check_d3d(result, "wait for initial GPU rendering");
            if (result == S_OK && done)
                break;
            if (GetTickCount64() >= deadline)
                throw std::runtime_error("Initial GPU rendering timed out");
            Sleep(1);
        }
        // --8<-- [end:completion]

        ComPtr<IDXGIResource> resource;
        check_d3d(texture.As(&resource), "query shared resource");
        HANDLE token = nullptr;
        check_d3d(resource->GetSharedHandle(&token), "get legacy token");
        ComPtr<IDXGIDevice> dxgi_device;
        ComPtr<IDXGIAdapter> adapter;
        check_d3d(device.As(&dxgi_device), "query DXGI device");
        check_d3d(dxgi_device->GetAdapter(adapter.GetAddressOf()),
                  "get adapter");
        DXGI_ADAPTER_DESC adapter_info{};
        check_d3d(adapter->GetDesc(&adapter_info), "get adapter LUID");

        owh_legacy_shared_texture binding{};
        owh_legacy_shared_texture_init(&binding);
        binding.legacy_shared_token = reinterpret_cast<uintptr_t>(token);
        binding.adapter_luid_low = adapter_info.AdapterLuid.LowPart;
        binding.adapter_luid_high = adapter_info.AdapterLuid.HighPart;

        // --8<-- [start:bind]
        owh_overlay_spec spec{};
        owh_overlay_spec_init(&spec);
        spec.width = width;
        spec.height = height;
        spec.left = 64;
        spec.top = 64;
        auto overlay = sdk.overlays().create_shared_texture(spec, binding);
        // --8<-- [end:bind]
        // The texture/device/target stay alive and unchanged until close.
        std::cout << "Texture accepted. Enter the game and check the cyan "
                     "rectangle. Press Enter here to close.\n";
        std::cin.get();
        overlay.close(); // Before releasing the producer's texture references.
        sdk.detach();
        // Scoped COM references are released after the overlay has closed.
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
