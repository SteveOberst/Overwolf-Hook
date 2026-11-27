#include <array>
#include <cstring>
#include <dxgi.h>
#include <iostream>
#include <owh/overwolf/overlays/d3d11_raw_resource.hpp>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
using SharedTexture = overwolf_hook::core::LegacySharedTexture;
using overwolf_hook::overwolf::D3D11RawResource;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void success(HRESULT code, const char *operation) {
    if (FAILED(code)) {
        std::cerr << operation << " HRESULT=" << std::hex << static_cast<uint32_t>(code) << '\n';
        throw std::runtime_error(operation);
    }
}
struct OwnedTexture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> texture;
    SharedTexture binding{sizeof(SharedTexture)};
    HRESULT initialize(UINT bindings = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE) {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        const HRESULT created = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
            D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());
        if (FAILED(created))
            return created;
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        success(device.As(&dxgi), "query DXGI device");
        success(dxgi->GetAdapter(adapter.GetAddressOf()), "get owned adapter");
        DXGI_ADAPTER_DESC adapter_desc{};
        success(adapter->GetDesc(&adapter_desc), "get adapter LUID");
        D3D11_TEXTURE2D_DESC shape{};
        shape.Width = 16;
        shape.Height = 8;
        shape.MipLevels = 1;
        shape.ArraySize = 1;
        shape.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        shape.SampleDesc.Count = 1;
        shape.Usage = D3D11_USAGE_DEFAULT;
        shape.BindFlags = bindings;
        shape.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        std::array<uint32_t, 16 * 8> pixels{};
        pixels.fill(0xff336699);
        D3D11_SUBRESOURCE_DATA initial{pixels.data(), 16 * 4, 0};
        success(device->CreateTexture2D(&shape, &initial, texture.GetAddressOf()),
                "create owned texture");
        // Test producer explicitly waits for its own initial GPU work. The
        // production importer does not claim or manufacture this guarantee.
        D3D11_QUERY_DESC query_desc{D3D11_QUERY_EVENT, 0};
        ComPtr<ID3D11Query> query;
        success(device->CreateQuery(&query_desc, query.GetAddressOf()),
                "create initialization fence");
        context->End(query.Get());
        context->Flush();
        const ULONGLONG deadline = GetTickCount64() + 5000;
        for (;;) {
            const HRESULT completed = context->GetData(query.Get(), nullptr, 0, 0);
            if (completed == S_OK)
                break;
            success(completed, "initialization completion");
            require(GetTickCount64() < deadline, "initialization completion timeout");
            SwitchToThread();
        }
        ComPtr<IDXGIResource> resource;
        success(texture.As(&resource), "query shared resource");
        HANDLE token = nullptr;
        success(resource->GetSharedHandle(&token), "get borrowed legacy token");
        binding = {sizeof(binding), 0, reinterpret_cast<uintptr_t>(token),
                   adapter_desc.AdapterLuid.LowPart, adapter_desc.AdapterLuid.HighPart};
        return S_OK;
    }
};

struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
bool transfer(HANDLE pipe, void *data, DWORD size, bool writing) {
    DWORD done = 0;
    return (writing ? WriteFile(pipe, data, size, &done, nullptr)
                    : ReadFile(pipe, data, size, &done, nullptr)) &&
           done == size;
}
int child(const wchar_t *request, const wchar_t *response) {
    const auto incoming = reinterpret_cast<HANDLE>(_wcstoui64(request, nullptr, 10));
    const auto outgoing = reinterpret_cast<HANDLE>(_wcstoui64(response, nullptr, 10));
    SharedTexture binding{sizeof(SharedTexture)};
    require(transfer(incoming, &binding, sizeof(binding), false), "child reads binding");
    auto [outcome, pin] = D3D11RawResource::import(binding, 16, 8, 512);
    require(transfer(outgoing, &outcome, sizeof(outcome), true), "child reports import");
    if (outcome.status != OWH_OK)
        return 1;
    uint32_t producer_released = 0;
    require(transfer(incoming, &producer_released, sizeof(producer_released), false) &&
                producer_released == 1,
            "child observes producer release");
    // The producer has now released its texture. Open the same allocation a
    // second time using an independent device: only the import pin retains it.
    ComPtr<IDXGIFactory1> factory;
    success(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())), "child create factory");
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        success(factory->EnumAdapters1(index, candidate.GetAddressOf()), "child find adapter");
        DXGI_ADAPTER_DESC1 desc{};
        success(candidate->GetDesc1(&desc), "child adapter description");
        if (desc.AdapterLuid.LowPart == binding.adapter_luid_low &&
            desc.AdapterLuid.HighPart == binding.adapter_luid_high) {
            adapter = candidate;
            break;
        }
    }
    ComPtr<ID3D11Device> verifier;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    success(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                              D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1, D3D11_SDK_VERSION,
                              verifier.GetAddressOf(), nullptr, context.GetAddressOf()),
            "child verifier device");
    ComPtr<ID3D11Texture2D> opened;
    success(verifier->OpenSharedResource(
                reinterpret_cast<HANDLE>(static_cast<uintptr_t>(binding.legacy_shared_token)),
                IID_PPV_ARGS(opened.GetAddressOf())),
            "child pin keeps allocation alive");
    D3D11_TEXTURE2D_DESC staging_desc{};
    opened->GetDesc(&staging_desc);
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.MiscFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    success(verifier->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf()),
            "child test readback");
    context->CopyResource(staging.Get(), opened.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    success(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "child inspect owned pixels");
    bool equal = true;
    for (UINT y = 0; y < 8; ++y) {
        const auto *row = reinterpret_cast<const uint32_t *>(
            static_cast<const std::byte *>(mapped.pData) + size_t{y} * mapped.RowPitch);
        for (UINT x = 0; x < 16; ++x)
            equal = equal && row[x] == 0xff336699;
    }
    context->Unmap(staging.Get(), 0);
    require(equal, "cross-process pixels survive producer texture release");
    outcome = pin->health();
    require(transfer(outgoing, &outcome, sizeof(outcome), true),
            "child reports retained resource health");
    return outcome.status == OWH_OK ? 0 : 1;
}

void cross_process(OwnedTexture &source) {
    SECURITY_ATTRIBUTES inherited{sizeof(inherited), nullptr, TRUE};
    Handle child_read, parent_write, parent_read, child_write;
    require(CreatePipe(&child_read.value, &parent_write.value, &inherited, 4096) != FALSE,
            "create request pipe");
    require(CreatePipe(&parent_read.value, &child_write.value, &inherited, 4096) != FALSE,
            "create response pipe");
    require(SetHandleInformation(parent_write.value, HANDLE_FLAG_INHERIT, 0) != FALSE &&
                SetHandleInformation(parent_read.value, HANDLE_FLAG_INHERIT, 0) != FALSE,
            "private parent handles");
    wchar_t executable[32768]{};
    require(GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable))) != 0,
            "owned executable");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --child " +
                           std::to_wstring(reinterpret_cast<uintptr_t>(child_read.value)) + L" " +
                           std::to_wstring(reinterpret_cast<uintptr_t>(child_write.value));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup, &process) != FALSE,
            "start owned GPU consumer");
    Handle child_process{process.hProcess}, child_thread{process.hThread};
    CloseHandle(child_read.value);
    child_read.value = nullptr;
    CloseHandle(child_write.value);
    child_write.value = nullptr;
    require(transfer(parent_write.value, &source.binding, sizeof(source.binding), true),
            "send binding metadata");
    owh_result imported{};
    require(transfer(parent_read.value, &imported, sizeof(imported), false) &&
                imported.status == OWH_OK,
            "independent child imports shared texture");
    source.texture.Reset();
    source.context->ClearState();
    source.context->Flush();
    uint32_t released = 1;
    require(transfer(parent_write.value, &released, sizeof(released), true),
            "signal producer texture release");
    require(transfer(parent_read.value, &imported, sizeof(imported), false) &&
                imported.status == OWH_OK,
            "independent child retains import pin");
    // The child has already reported successful readback/health. Device and
    // driver teardown can outlive that result under load; keep it bounded by
    // the enclosing 30-second test deadline rather than a five-second exit race.
    require(WaitForSingleObject(child_process.value, 15000) == WAIT_OBJECT_0, "GPU consumer ends");
    DWORD exit_code = 0;
    require(GetExitCodeProcess(child_process.value, &exit_code) && !exit_code,
            "GPU consumer passes");
}

int run() {
    // Argument errors never initialize DXGI or a graphics device.
    SharedTexture invalid{};
    require(D3D11RawResource::import(invalid, 16, 8, 512).first.status == OWH_INVALID_ARGUMENT,
            "invalid metadata rejected before graphics work");
    OwnedTexture source;
    const HRESULT initialized = source.initialize();
    if (FAILED(initialized)) {
        std::cout << "SKIP no hardware D3D11 device HRESULT=" << std::hex
                  << static_cast<uint32_t>(initialized) << '\n';
        return 77;
    }
    const auto binding = source.binding;
    const auto budget = D3D11RawResource::import(binding, 16, 8, 511).first;
    require(budget.status == OWH_RESOURCE_EXHAUSTED && budget.failure_stage == OWH_STAGE_ALLOCATION,
            "budget checked before import");
    require(D3D11RawResource::import(binding, 17, 8, 1024).first.status == OWH_INVALID_ARGUMENT,
            "binding/overlay size mismatch");
    auto mismatch = binding;
    mismatch.adapter_luid_low = UINT32_MAX;
    mismatch.adapter_luid_high = INT32_MAX;
    const auto no_adapter = D3D11RawResource::import(mismatch, 16, 8, 512).first;
    require(no_adapter.status == OWH_UNSUPPORTED &&
                no_adapter.failure_stage == OWH_STAGE_D3D11_ADAPTER,
            "missing exact LUID does not select another adapter");
    mismatch = binding;
    mismatch.legacy_shared_token = UINT64_MAX;
    const auto no_resource = D3D11RawResource::import(mismatch, 16, 8, 512).first;
    require(no_resource.status != OWH_OK && no_resource.failure_stage == OWH_STAGE_D3D11_IMPORT &&
                no_resource.error_domain == OWH_ERROR_HRESULT &&
                no_resource.native_code <= UINT32_MAX,
            "import failure preserves unsigned original HRESULT");
    // Metadata is only a claim. Import must validate the allocation itself.
    const auto wrong_actual = D3D11RawResource::import(binding, 8, 8, 512).first;
    require(wrong_actual.status == OWH_INVALID_ARGUMENT &&
                wrong_actual.failure_stage == OWH_STAGE_D3D11_VALIDATE,
            "actual texture descriptor validated after import");
    OwnedTexture no_target;
    success(no_target.initialize(D3D11_BIND_SHADER_RESOURCE),
            "create unsupported binding flags fixture");
    const auto wrong_flags = D3D11RawResource::import(no_target.binding, 16, 8, 512).first;
    require(wrong_flags.status == OWH_INVALID_ARGUMENT &&
                wrong_flags.failure_stage == OWH_STAGE_D3D11_VALIDATE,
            "actual resource flags validated");
    cross_process(source);
    std::cout << "PASS actual D3D11 import, exact LUID, descriptor/budget/errors "
                 "and cross-process pin/pixels\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        if (argc == 4 && std::wcscmp(argv[1], L"--child") == 0)
            return child(argv[2], argv[3]);
        return run();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
