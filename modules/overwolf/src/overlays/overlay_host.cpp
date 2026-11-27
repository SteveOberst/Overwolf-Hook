#include <owh/overwolf/overlays/overlay_host.hpp>

#include <owh/overwolf/overlays/d3d11_raw_resource.hpp>
#include <owh/overwolf/overlays/native_contract.hpp>
#include <owh/overwolf/overlays/overlay_executor.hpp>
#include <owh/overwolf/overlays/source_adapter.hpp>
#include <owh/overwolf/signatures/overlay_targets.hpp>
#include <owh/overwolf/signatures/resolver.hpp>
#include <owh/platform/result.hpp>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <utility>

namespace overwolf_hook::overwolf {
namespace {
using namespace native_contract;
using namespace overwolf_hook::overwolf::signatures::overlay;

bool valid_geometry(core::Geometry g) noexcept {
    return g.size == sizeof(core::Geometry) && core::valid_geometry(g);
}
LRESULT CALLBACK overlay_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

struct OverlayHost::Impl : std::enable_shared_from_this<OverlayHost::Impl> {
    explicit Impl(LifetimeAdmission &a)
        : admission(a), profile(a.profile_snapshot()),
          source_adapter(std::make_shared<SourceAdapter>(a, profile)) {}
    ~Impl() { executor.shutdown(); }

    bool start_actor() {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        class_name = L"OWH.NativeOverlay." + std::to_wstring(GetCurrentProcessId());
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = overlay_window_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = class_name.c_str();
        registered_class = RegisterClassExW(&wc) != 0;
        return registered_class;
    }
    void pump_messages() noexcept {
        MSG message{};
        for (unsigned n = 0; n < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++n) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    void pump_agent() noexcept {
        std::shared_ptr<OverlayWork> work;
        {
            std::lock_guard lock(work_mutex);
            work = work_callback.lock();
        }
        if (work)
            work->process_on_actor();
    }
    bool retry_agent() noexcept {
        std::shared_ptr<OverlayWork> work;
        {
            std::lock_guard lock(work_mutex);
            work = work_callback.lock();
        }
        return work && work->has_pending_work();
    }
    bool ensure_actor() noexcept {
        const std::weak_ptr<Impl> weak = weak_from_this();
        return executor.ensure_started({[weak] {
                                            const auto self = weak.lock();
                                            return self && self->start_actor();
                                        },
                                        [weak] {
                                            if (auto self = weak.lock())
                                                self->pump_messages();
                                        },
                                        [weak] {
                                            if (auto self = weak.lock())
                                                self->pump_agent();
                                        },
                                        [weak] {
                                            const auto self = weak.lock();
                                            return self && self->retry_agent();
                                        },
                                        [this] { stop_actor(); }});
    }
    bool refresh_profile(bool shared_texture) noexcept {
        const auto ready = [shared_texture](const ResolvedProfile &p) {
            return p.common && p.lifecycle && (shared_texture ? p.shared_texture_source : p.pixel_buffer_source);
        };
        if (ready(profile))
            return true;
        HMODULE agent = GetModuleHandleW(L"OWAgent.dll");
        if (!agent)
            return false;
        ResolvedProfile current{};
        if (!admission.resolve_profile(agent, current) || !ready(current))
            return false;
        try {
            auto adapter = std::make_shared<SourceAdapter>(admission, current);
            profile = current;
            source_adapter = std::move(adapter);
            return true;
        } catch (...) {
            return false;
        }
    }
    void stop_actor() noexcept {
        // Native handles own all per-source state and close themselves from
        // Agent retirement before the resident host is stopped.
        pump_messages();
        if (registered_class) {
            UnregisterClassW(class_name.c_str(), GetModuleHandleW(nullptr));
            registered_class = false;
        }
    }

    LifetimeAdmission &admission;
    ResolvedProfile profile{};
    std::shared_ptr<SourceAdapter> source_adapter;
    OverlayExecutor executor;
    std::mutex work_mutex;
    std::weak_ptr<OverlayWork> work_callback;
    bool registered_class = false;
    std::wstring class_name;
};

struct NativeOverlay::Impl {
    std::shared_ptr<OverlayHost::Impl> host;
    core::Geometry geometry{};
    uint32_t kind = OWH_OVERLAY_PIXEL_BUFFER;
    HWND window = nullptr;
    SourceAdapter::Registration registration{};
    std::shared_ptr<SourceAdapter> adapter;
    std::unique_ptr<D3D11RawResource> shared_texture;
    bool closing = false;

    Impl(std::shared_ptr<OverlayHost::Impl> h, core::Geometry g, uint32_t k)
        : host(std::move(h)), geometry(g), kind(k) {}

    owh_result create() noexcept {
        if (!host->refresh_profile(kind == OWH_OVERLAY_SHARED_TEXTURE))
            return make_result(OWH_INCOMPATIBLE, OWH_STAGE_PROFILE_RESOLVE);
        adapter = host->source_adapter;
        if (!host->admission.installed() && !host->admission.install_lifecycle_hooks(host->profile))
            return make_result(OWH_AGENT_NOT_READY, OWH_STAGE_AGENT_LIFETIME);
        if (kind == OWH_OVERLAY_PIXEL_BUFFER ? !host->profile.pixel_buffer_source : !host->profile.shared_texture_source)
            return make_result(OWH_INCOMPATIBLE, OWH_STAGE_PROFILE_RESOLVE);
        if (kind == OWH_OVERLAY_SHARED_TEXTURE)
            return make_result(OWH_OK);
        window =
            CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            host->class_name.c_str(), L"", WS_POPUP, 0, 0,
                            static_cast<int>(geometry.width), static_cast<int>(geometry.height),
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window)
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_WINDOW, OWH_ERROR_WIN32, GetLastError());
        if (!SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA) ||
            !SetPropA(window, "OWOSR", reinterpret_cast<HANDLE>(1)) ||
            !SetPropA(window, "owosr.ingameonly", reinterpret_cast<HANDLE>(1))) {
            const auto failure =
                make_result(OWH_SYSTEM_ERROR, OWH_STAGE_WINDOW, OWH_ERROR_WIN32, GetLastError());
            if (DestroyWindow(window))
                window = nullptr;
            else {
                closing = true;
            }
            return failure;
        }
        if (kind == OWH_OVERLAY_PIXEL_BUFFER) {
            const auto registered = adapter->register_source(
                window, {geometry.width, geometry.height, geometry.left, geometry.top},
                registration);
            if (registered.status != OWH_OK) {
                (void)close_on_actor(); // Retained handles remain visible to domain retirement.
                return registered;
            }
        }
        ShowWindow(window, SW_SHOWNOACTIVATE);
        return make_result(OWH_OK);
    }

    owh_result close_on_actor() noexcept {
        closing = true;
        if (window && !DestroyWindow(window)) {
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_CLEANUP, OWH_ERROR_WIN32,
                               GetLastError());
        }
        window = nullptr;
        const auto removed = adapter ? adapter->remove_source(registration) : make_result(OWH_OK);
        if (removed.status != OWH_OK)
            return removed;
        shared_texture.reset();
        return make_result(OWH_OK);
    }
};

NativeOverlay::NativeOverlay(std::shared_ptr<Impl> p) : impl_(std::move(p)) {}
NativeOverlay::~NativeOverlay() = default;

owh_result NativeOverlay::publish(core::ResourceRef ref, const PixelView &view) noexcept {
    const auto &p = impl_;
    if (!p->window || p->closing)
        return make_result(OWH_CLOSED, OWH_STAGE_CLEANUP);
    return p->adapter->publish_pixels(p->window, ref, p->registration, view);
}

owh_result NativeOverlay::bind(core::LegacySharedTexture texture, uint64_t budget) noexcept {
    const auto &p = impl_;
    if (p->kind != OWH_OVERLAY_SHARED_TEXTURE)
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
    if (p->closing)
        return make_result(OWH_BUSY, OWH_STAGE_CLEANUP);
    if (p->shared_texture)
        return make_result(OWH_ALREADY_EXISTS, OWH_STAGE_ARGUMENT);
    auto [status, imported] =
        D3D11RawResource::import(texture, p->geometry.width, p->geometry.height, budget);
    if (status.status != OWH_OK) {
        return status;
    }
    p->window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        p->host->class_name.c_str(), L"", WS_POPUP, 0, 0, static_cast<int>(p->geometry.width),
        static_cast<int>(p->geometry.height), nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!p->window)
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_WINDOW, OWH_ERROR_WIN32, GetLastError());
    if (!SetLayeredWindowAttributes(p->window, 0, 0, LWA_ALPHA) ||
        !SetPropA(p->window, "OWOSR", reinterpret_cast<HANDLE>(1)) ||
        !SetPropA(p->window, "owosr.ingameonly", reinterpret_cast<HANDLE>(1))) {
        const auto failure =
            make_result(OWH_SYSTEM_ERROR, OWH_STAGE_WINDOW, OWH_ERROR_WIN32, GetLastError());
        if (DestroyWindow(p->window))
            p->window = nullptr;
        else {
            p->closing = true;
            p->shared_texture = std::move(imported);
        }
        return failure;
    }
    const auto bound = p->adapter->bind_texture(
        p->window, {p->geometry.width, p->geometry.height, p->geometry.left, p->geometry.top},
        imported->token(), p->registration);
    if (bound.status != OWH_OK) {
        if (!DestroyWindow(p->window)) {
            p->closing = true;
            p->shared_texture = std::move(imported);
        } else {
            p->window = nullptr;
            const auto removed = p->adapter->remove_source(p->registration);
            if (removed.status != OWH_OK) {
                p->closing = true;
                p->shared_texture = std::move(imported);
            }
        }
        return bound;
    }
    p->shared_texture = std::move(imported);
    ShowWindow(p->window, SW_SHOWNOACTIVATE);
    return make_result(OWH_OK);
}
uint64_t NativeOverlay::texture_allocation_bytes() const noexcept {
    return impl_->shared_texture ? impl_->shared_texture->allocation_bytes() : 0;
}

std::pair<owh_result, owh_overlay_info_data> NativeOverlay::info() noexcept {
    const auto &p = impl_;
    std::pair<owh_result, owh_overlay_info_data> out{};
    out.first = make_result(OWH_OK);
    bool lost = false;
    if (p->registration.registered &&
        p->host->admission.observed_epoch() != p->registration.epoch) {
        out.first = make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME);
        lost = true;
    }
    if (!lost && p->shared_texture) {
        const auto health = p->shared_texture->health();
        if (health.status != OWH_OK) {
            out.first = health;
            lost = true;
        }
    }
    out.second.size = sizeof(out.second);
    out.second.kind = p->kind;
    out.second.width = p->geometry.width;
    out.second.height = p->geometry.height;
    out.second.state = p->closing                                ? OWH_OVERLAY_CLOSING
                       : lost                                    ? OWH_OVERLAY_LOST
                       : p->kind == OWH_OVERLAY_SHARED_TEXTURE && !p->shared_texture ? OWH_OVERLAY_AWAITING_TEXTURE
                                                                 : OWH_OVERLAY_READY;
    if (lost)
        out.second.last_result = out.first;
    return out;
}

owh_result NativeOverlay::close() noexcept { return impl_->close_on_actor(); }
bool NativeOverlay::needs_cleanup() const noexcept {
    const auto &p = impl_;
    return p->window != nullptr || p->registration.registered || p->shared_texture != nullptr;
}
bool NativeOverlay::closing() const noexcept { return impl_->closing; }

OverlayHost::OverlayHost(LifetimeAdmission &admission) : impl_(std::make_shared<Impl>(admission)) {}
OverlayHost::~OverlayHost() = default;

void OverlayHost::set_work(std::weak_ptr<OverlayWork> work) noexcept {
    std::lock_guard lock(impl_->work_mutex);
    impl_->work_callback = std::move(work);
}
void OverlayHost::clear_work() noexcept {
    {
        std::lock_guard lock(impl_->work_mutex);
        impl_->work_callback.reset();
    }
    if (impl_->executor.running()) {
        try {
            impl_->executor.invoke([] {});
        } catch (...) {
        }
    }
}
void OverlayHost::wake() noexcept { impl_->executor.signal_sources(); }

owh_result OverlayHost::execute(std::function<owh_result()> operation) noexcept {
    if (!operation || !impl_->ensure_actor())
        return make_result(OWH_AGENT_NOT_READY, OWH_STAGE_AGENT_LIFETIME);
    try {
        return impl_->executor.invoke(std::move(operation));
    } catch (...) {
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_ARGUMENT);
    }
}

NativeOverlayResult OverlayHost::create(uint32_t kind, core::Geometry geometry) noexcept {
    if (!valid_geometry(geometry))
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), {}};
    try {
        auto handle = std::shared_ptr<NativeOverlay>(
            new NativeOverlay(std::make_shared<NativeOverlay::Impl>(impl_, geometry, kind)));
        const auto status = handle->impl_->create();
        return {status,
                status.status == OWH_OK || handle->needs_cleanup() ? std::move(handle) : nullptr};
    } catch (...) {
        return {make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION), {}};
    }
}
NativeOverlayResult OverlayHost::create_pixel_buffer(core::Geometry geometry) noexcept {
    return create(OWH_OVERLAY_PIXEL_BUFFER, geometry);
}
NativeOverlayResult OverlayHost::create_shared_texture(core::Geometry geometry) noexcept {
    return create(OWH_OVERLAY_SHARED_TEXTURE, geometry);
}

void OverlayHost::fill_capabilities(owh_overlay_capabilities &caps, bool opt_in) const noexcept {
    // Detours replace entry bytes used by the signature resolver. Reuse the
    // profile validated before those hooks, just as source creation does.
    // This snapshot needs no actor work or changes to the actor's adapter.
    auto probe = impl_->admission.profile_snapshot();
    if (!probe.common) {
        if (const auto agent = GetModuleHandleW(L"OWAgent.dll"))
            probe = resolve_loaded_profile(agent);
        else {
            caps.pixel_buffer_status = caps.shared_texture_status =
                make_result(OWH_AGENT_NOT_READY, OWH_STAGE_PROFILE_RESOLVE);
            return;
        }
    }
    const bool lifecycle = probe.common && probe.lifecycle;
    caps.pixel_buffer_status = probe.pixel_buffer_source && lifecycle
                          ? make_result(OWH_OK)
                          : make_result(OWH_INCOMPATIBLE, OWH_STAGE_PROFILE_RESOLVE);
    caps.shared_texture_status = !probe.shared_texture_source || !lifecycle
                          ? make_result(OWH_INCOMPATIBLE, OWH_STAGE_PROFILE_RESOLVE)
                      : opt_in ? make_result(OWH_OK)
                               : make_result(OWH_OPT_IN_REQUIRED, OWH_STAGE_CAPABILITY);
    if (caps.pixel_buffer_status.status == OWH_OK)
        caps.available |= OWH_CAP_PIXEL_BUFFER;
    if (caps.shared_texture_status.status == OWH_OK || caps.shared_texture_status.status == OWH_OPT_IN_REQUIRED)
        caps.available |= OWH_CAP_D3D11_SHARED_TEXTURE;
    caps.enabled |= caps.pixel_buffer_status.status == OWH_OK ? OWH_CAP_PIXEL_BUFFER : 0;
    caps.enabled |= caps.shared_texture_status.status == OWH_OK && opt_in ? OWH_CAP_D3D11_SHARED_TEXTURE : 0;
}

} // namespace overwolf_hook::overwolf
