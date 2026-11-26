#include <owh/overwolf/overlays/lifetime_admission.hpp>
#include <owh/overwolf/overlays/native_contract.hpp>
#include <owh/overwolf/overlays/startup_bridge.hpp>
#include <owh/platform/deadline.hpp>

#include <windows.h>
#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace overwolf_hook::overwolf {
namespace {
using RunAgent = native_contract::run_agent_fn;
using AgentLifecycle = native_contract::agent_lifecycle_fn;
using AgentInit = native_contract::agent_init_fn;
RunAgent original_run_agent = nullptr;
AgentInit original_agent_init = nullptr;
AgentLifecycle original_clear_agent = nullptr;
AgentLifecycle original_unhook_agent = nullptr;
std::mutex hook_lock;
bool hook_attempted = false;
thread_local uint32_t lifecycle_depth = 0;
struct RunObservation {
    uint32_t init_calls = 0;
    bool initialized = false;
};
thread_local RunObservation *current_run = nullptr;
HMODULE pinned_agent = nullptr;
HMODULE profile_module = nullptr;
bool profile_cached = false;

bool safe_read(uint64_t address, void *out, size_t bytes) noexcept {
    if (!address || !out || !bytes)
        return false;
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), out,
                             bytes, &copied) &&
           copied == bytes;
}

bool validate_root_fields(const ResolvedProfile &profile, uint64_t &root) noexcept {
    root = 0;
    uint64_t candidate = 0, vtable = 0, init = 0, clear = 0, refresher = 0;
    if (!safe_read(profile.agent_global_slot, &candidate, sizeof(candidate)) || !candidate ||
        !safe_read(candidate, &vtable, sizeof(vtable)) || !vtable ||
        !safe_read(vtable + 0x10, &init, sizeof(init)) ||
        !safe_read(vtable + 0x20, &clear, sizeof(clear)) ||
        !safe_read(candidate + 0x2b8, &refresher, sizeof(refresher)) ||
        init != profile.agent_init || clear != profile.agent_clear || !refresher)
        return false;
    root = candidate;
    return true;
}

void run_lifecycle(RunAgent original, uint32_t argument) {
    const bool outer = lifecycle_depth++ == 0;
    RunObservation observation;
    auto *previous = current_run;
    current_run = &observation;
    const DWORD incoming_error = GetLastError();
    if (outer) {
        process_lifetime_admission().invalidate_readiness();
        process_lifetime_admission().lifecycle_enter();
        SetLastError(incoming_error);
    }
    try {
        original(argument);
    } catch (...) {
        const DWORD outgoing_error = GetLastError();
        current_run = previous;
        --lifecycle_depth;
        if (outer)
            process_lifetime_admission().lifecycle_leave();
        SetLastError(outgoing_error);
        throw;
    }
    const DWORD outgoing_error = GetLastError();
    current_run = previous;
    --lifecycle_depth;
    if (outer) {
        process_lifetime_admission().run_completed(observation.init_calls == 1 &&
                                                   observation.initialized);
        process_lifetime_admission().lifecycle_leave();
    }
    SetLastError(outgoing_error);
}

void lifecycle_void(AgentLifecycle original) {
    const bool outer = lifecycle_depth++ == 0;
    const DWORD incoming_error = GetLastError();
    if (outer) {
        process_lifetime_admission().invalidate_readiness();
        process_lifetime_admission().lifecycle_enter();
        SetLastError(incoming_error);
    }
    try {
        original();
    } catch (...) {
        const DWORD outgoing_error = GetLastError();
        --lifecycle_depth;
        if (outer)
            process_lifetime_admission().lifecycle_leave();
        SetLastError(outgoing_error);
        throw;
    }
    const DWORD outgoing_error = GetLastError();
    --lifecycle_depth;
    if (outer)
        process_lifetime_admission().lifecycle_leave();
    SetLastError(outgoing_error);
}

uint64_t __fastcall agent_init_detour(void *agent, uint32_t argument) {
    const auto result = original_agent_init(agent, argument);
    const DWORD error = GetLastError();
    if (current_run) {
        ++current_run->init_calls;
        current_run->initialized = static_cast<uint8_t>(result) != 0;
    }
    SetLastError(error);
    return result;
}

void __fastcall run_agent_detour(uint32_t argument) { run_lifecycle(original_run_agent, argument); }
void __fastcall clear_agent_detour() { lifecycle_void(original_clear_agent); }
void __fastcall unhook_agent_detour() { lifecycle_void(original_unhook_agent); }

} // namespace

AdmissionLease::AdmissionLease(LifetimeAdmission *owner, uint64_t root, uint64_t epoch) noexcept
    : owner_(owner), root_(root), epoch_(epoch) {}
AdmissionLease::AdmissionLease(AdmissionLease &&other) noexcept
    : owner_(other.owner_), root_(other.root_), epoch_(other.epoch_) {
    other.owner_ = nullptr;
    other.root_ = other.epoch_ = 0;
}
AdmissionLease &AdmissionLease::operator=(AdmissionLease &&other) noexcept {
    if (this == &other)
        return *this;
    if (owner_)
        owner_->leave();
    owner_ = other.owner_;
    root_ = other.root_;
    epoch_ = other.epoch_;
    other.owner_ = nullptr;
    other.root_ = other.epoch_ = 0;
    return *this;
}
AdmissionLease::~AdmissionLease() {
    if (owner_)
        owner_->leave();
}

bool LifetimeAdmission::prepare_controlled_start(uint32_t timeout_ms) noexcept {
    const Deadline deadline(timeout_ms);
    HMODULE server = nullptr;
    while (!(server = GetModuleHandleW(L"OWServer.dll"))) {
        if (GetModuleHandleW(L"OWAgent.dll") || deadline.expired())
            return false;
        Sleep(10);
    }
    return arm_startup_bridge(server, [](HMODULE module, FARPROC original) noexcept {
        auto &admission = process_lifetime_admission();
        ResolvedProfile profile{};
        if (admission.resolve_profile(module, profile) &&
            profile.run_agent == reinterpret_cast<uint64_t>(original))
            (void)admission.install_lifecycle_hooks(profile);
    });
}

bool LifetimeAdmission::install_lifecycle_hooks(const ResolvedProfile &profile) noexcept {
    try {
        std::lock_guard guard(hook_lock);
        if (installed_.load())
            return true;
        if (hook_attempted || !profile.lifecycle || !profile.common || !profile.run_agent ||
            !profile.clear_agent || !profile.unhook_agent || !profile.agent_init)
            return false;
        if (!pinned_agent) {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                    reinterpret_cast<LPCWSTR>(profile.run_agent), &module))
                return false;
            pinned_agent = module;
        }
        // One resident installation attempt. Partial trampolines remain valid;
        // failure requires a process restart, not removal/reinstallation.
        if (!profile_cached) {
            profile_ = profile;
            profile_cached = true;
        }
        hook_attempted = true;
        const auto init_status = MH_Initialize();
        if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED)
            return false;

        const std::array<void *, 4> targets{reinterpret_cast<void *>(profile.agent_init),
                                            reinterpret_cast<void *>(profile.run_agent),
                                            reinterpret_cast<void *>(profile.clear_agent),
                                            reinterpret_cast<void *>(profile.unhook_agent)};
        const std::array<void *, 4> detours{reinterpret_cast<void *>(&agent_init_detour),
                                            reinterpret_cast<void *>(&run_agent_detour),
                                            reinterpret_cast<void *>(&clear_agent_detour),
                                            reinterpret_cast<void *>(&unhook_agent_detour)};
        const std::array<void **, 4> originals{reinterpret_cast<void **>(&original_agent_init),
                                               reinterpret_cast<void **>(&original_run_agent),
                                               reinterpret_cast<void **>(&original_clear_agent),
                                               reinterpret_cast<void **>(&original_unhook_agent)};
        for (size_t i = 0; i < targets.size(); ++i) {
            if (MH_CreateHook(targets[i], detours[i], originals[i]) != MH_OK ||
                MH_QueueEnableHook(targets[i]) != MH_OK)
                return false;
        }
        if (MH_ApplyQueued() != MH_OK)
            return false;
        installed_.store(true);
        return true;
    } catch (...) {
        return false;
    }
}

bool LifetimeAdmission::resolve_profile(HMODULE agent, ResolvedProfile &profile) noexcept {
    try {
        std::lock_guard guard(hook_lock);
        if (profile_cached) {
            if (agent != profile_module)
                return false;
            if (profile_.common && profile_.lifecycle && profile_.pixel_buffer_source &&
                profile_.shared_texture_source) {
                profile = profile_;
                return true;
            }
            const auto refreshed = resolve_loaded_profile(agent);
            if (!refreshed.common)
                return false;
            profile_ = refreshed;
            profile = refreshed;
            return true;
        }
        const auto resolved = resolve_loaded_profile(agent);
        if (!resolved.common)
            return false;
        if (!profile_module || profile_module == agent) {
            profile_module = agent;
            profile_ = resolved;
            profile_cached = true;
        }
        profile = resolved;
        return true;
    } catch (...) {
        return false;
    }
}

ResolvedProfile LifetimeAdmission::profile_snapshot() const noexcept {
#ifdef OWH_ADMISSION_TEST_FIXTURE
    // Owned executables may install their own source callbacks through the
    // private friend fixture. The resident product has no such profile path.
    return profile_;
#else
    try {
        std::lock_guard guard(hook_lock);
        return profile_cached ? profile_ : ResolvedProfile{};
    } catch (...) {
        return {};
    }
#endif
}

bool LifetimeAdmission::try_refresh(uint64_t &root_out, uint64_t &epoch_out) noexcept {
    root_out = epoch_out = 0;
    try {
        std::lock_guard serial(refresh_lock_);
        if (!installed_.load() || !initial_ready_.load() || active_transitions_.load() != 0)
            return false;
        const uint64_t state = epoch_and_open_.load();
        if (state & 1) {
            root_out = admitted_root_.load();
            epoch_out = state >> 1;
            return root_out != 0;
        }
        active_publishers_.fetch_add(1);
        const auto release_reservation = [this] { active_publishers_.fetch_sub(1); };
        if (epoch_and_open_.load() != state || active_transitions_.load() != 0) {
            release_reservation();
            return false;
        }
        uint64_t root = 0;
        if (!validate_root(profile_snapshot(), root) || epoch_and_open_.load() != state ||
            active_transitions_.load() != 0) {
            release_reservation();
            return false;
        }
#ifdef OWH_ADMISSION_TEST_FIXTURE
        if (before_open_fixture_)
            before_open_fixture_(fixture_context_);
#endif
        if (epoch_and_open_.load() != state || active_transitions_.load() != 0) {
            release_reservation();
            return false;
        }
        // The root must become visible before opening the exact epoch. A
        // transition advances state and drains this reservation before calling
        // the original Agent function.
        admitted_root_.store(root);
        uint64_t expected = state;
        const bool opened =
            !(expected & 1) && epoch_and_open_.compare_exchange_strong(expected, state | 1);
        if (!opened)
            admitted_root_.store(0);
        release_reservation();
        if (!opened)
            return false;
        root_out = root;
        epoch_out = state >> 1;
        return true;
    } catch (...) {
        return false;
    }
}

AdmissionLease LifetimeAdmission::try_enter() noexcept {
    const uint64_t state = epoch_and_open_.load();
    if (!(state & 1) || active_transitions_.load() != 0)
        return {};
    active_publishers_.fetch_add(1);
    const uint64_t after = epoch_and_open_.load();
    const uint64_t root = admitted_root_.load();
    if (after != state || !(after & 1) || !root || active_transitions_.load() != 0) {
        active_publishers_.fetch_sub(1);
        return {};
    }
    return AdmissionLease(this, root, state >> 1);
}

void LifetimeAdmission::lifecycle_enter() noexcept {
    active_transitions_.fetch_add(1);
    auto state = epoch_and_open_.load();
    for (;;) {
        const uint64_t closed = ((state >> 1) + 1) << 1;
        if (epoch_and_open_.compare_exchange_weak(state, closed)) {
            admitted_root_.store(0);
            break;
        }
    }
    while (active_publishers_.load() != 0)
        SwitchToThread();
}

void LifetimeAdmission::lifecycle_leave() noexcept {
    auto state = epoch_and_open_.load();
    for (;;) {
        const uint64_t closed = ((state >> 1) + 1) << 1;
        if (epoch_and_open_.compare_exchange_weak(state, closed))
            break;
    }
    active_transitions_.fetch_sub(1);
}

void LifetimeAdmission::leave() noexcept { active_publishers_.fetch_sub(1); }

bool LifetimeAdmission::validate_root(const ResolvedProfile &profile,
                                      uint64_t &root) const noexcept {
    return validate_root_fields(profile, root);
}

LifetimeAdmission &process_lifetime_admission() noexcept {
    static LifetimeAdmission instance;
    return instance;
}
} // namespace overwolf_hook::overwolf
