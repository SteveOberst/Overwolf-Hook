#include <owh/overwolf/event_source.hpp>

#include <owh/overwolf/signatures/event_resolver.hpp>
#include <owh/overwolf/signatures/event_targets.hpp>
#include <owh/platform/result.hpp>
#include <owh/platform/sha256.hpp>

#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iterator>

namespace overwolf_hook::overwolf {
namespace {
using namespace signatures::events;
using DispatchEvents = void(__fastcall *)(void *, EventRange *);

struct SourceState {
    EventSource::Callback callback = nullptr;
    void *context = nullptr;
    std::atomic<uint64_t> active_exports{0};
    std::atomic<bool> enabled{false};
    std::atomic<uint32_t> warnings{0};
};

// Permanent detour and context. Only the callback's owner can be retired;
// executable code and the original trampoline remain valid until process exit.
SourceState source_state;
DispatchEvents original_dispatcher{};
bool source_claimed = false;
owh_result activation_result = make_result(OWH_OK);
SRWLOCK hook_lock = SRWLOCK_INIT;
SRWLOCK profile_lock = SRWLOCK_INIT;
HMODULE validated_consumer{};
void *validated_target{};
thread_local bool recursing = false;

void stop_export() noexcept {
    source_state.enabled.store(false, std::memory_order_seq_cst);
    while (source_state.active_exports.load(std::memory_order_seq_cst))
        SwitchToThread();
    source_state.callback = nullptr;
    source_state.context = nullptr;
}

bool copy_bytes(void *to, const void *from, size_t size) noexcept {
    __try {
        std::memcpy(to, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint32_t consumer_warnings() noexcept {
    HMODULE consumer = GetModuleHandleW(L"OWGameEventsConsumer.dll");
    if (!consumer)
        return OWH_SUBSCRIPTION_WARNING_HASH_UNAVAILABLE;
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(consumer, path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path))
        return OWH_SUBSCRIPTION_WARNING_HASH_UNAVAILABLE;
    std::array<uint8_t, 32> digest{};
    if (!platform::sha256_file(std::wstring(path), digest))
        return OWH_SUBSCRIPTION_WARNING_HASH_UNAVAILABLE;
    return digest == consumer_sha256 ? 0u : OWH_SUBSCRIPTION_WARNING_UNREVIEWED_CONSUMER;
}

void copy_events(SourceState &state, void *self, EventRange *batch) noexcept {
    uint32_t source = 0, producer = 0;
    const auto delegate = reinterpret_cast<uintptr_t>(self);
    EventRange range{};
    if (!copy_bytes(&source, reinterpret_cast<void *>(delegate + delegate_channel), sizeof(source)) ||
        !copy_bytes(&producer, reinterpret_cast<void *>(delegate + delegate_producer_process_id),
                    sizeof(producer)) ||
        !source || !producer || !copy_bytes(&range, batch, sizeof(range)) || range.end < range.begin ||
        range.reserved_end < range.end || (range.end - range.begin) % raw_event_bytes ||
        range.end - range.begin > raw_event_bytes * maximum_batch) {
        return;
    }
    const size_t count = (range.end - range.begin) / raw_event_bytes;
    for (size_t i = 0; i < count; ++i) {
        core::CapturedEvent event{};
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        event.captured_qpc = static_cast<uint64_t>(now.QuadPart);
        event.source_channel = source;
        event.producer_process_id = producer;
        event.raw_size = static_cast<uint32_t>(raw_event_bytes);
        if (!copy_bytes(event.raw, reinterpret_cast<void *>(range.begin + i * raw_event_bytes),
                        raw_event_bytes)) {
            continue;
        }
        if (state.callback)
            state.callback(state.context, event);
    }
}

void __fastcall tapped_dispatcher(void *self, EventRange *batch) {
    auto &state = source_state;
    const DWORD saved_error = GetLastError();
    if (!recursing && state.enabled.load(std::memory_order_seq_cst)) {
        ++state.active_exports;
        // Close can disable publication between the first check and this pin.
        // The permanent state stays valid; a disabled path never reads callback.
        if (state.enabled.load(std::memory_order_seq_cst)) {
            recursing = true;
            // The native caller reuses/frees its batch after dispatch. Copy it
            // before application callbacks can mutate or retire transient bytes.
            copy_events(state, self, batch);
            recursing = false;
        }
        --state.active_exports;
    }
    SetLastError(saved_error);
    // Preserve native callback registration and invoke it on every path. Its
    // return/last-error/exception behavior remains the original dispatcher's.
    original_dispatcher(self, batch);
}

owh_result resolve_source(void **out = nullptr) noexcept {
    HMODULE consumer = GetModuleHandleW(L"OWGameEventsConsumer.dll");
    if (!consumer)
        return make_result(OWH_AGENT_NOT_READY, OWH_STAGE_PROFILE_RESOLVE, OWH_ERROR_WIN32,
                           GetLastError());
    AcquireSRWLockExclusive(&profile_lock);
    if (validated_consumer != consumer || !validated_target) {
        try {
            validated_target = resolve_event_dispatcher(consumer);
            validated_consumer = consumer;
        } catch (const Failure &failure) {
            validated_target = nullptr;
            validated_consumer = nullptr;
            ReleaseSRWLockExclusive(&profile_lock);
            return failure.result;
        } catch (...) {
            validated_target = nullptr;
            validated_consumer = nullptr;
            ReleaseSRWLockExclusive(&profile_lock);
            return make_result(OWH_INCOMPATIBLE, OWH_STAGE_PROFILE_RESOLVE);
        }
    }
    if (out)
        *out = validated_target;
    ReleaseSRWLockExclusive(&profile_lock);
    return make_result(OWH_OK);
}

owh_result install_hook() noexcept {
    void *entry = nullptr;
    auto result = resolve_source(&entry);
    if (result.status != OWH_OK)
        return result;
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&tapped_dispatcher), &pinned) ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(entry), &pinned))
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_ALLOCATION, OWH_ERROR_WIN32, GetLastError());
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED)
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_SOURCE_REGISTER);
    if (MH_CreateHook(entry, reinterpret_cast<void *>(&tapped_dispatcher),
                      reinterpret_cast<void **>(&original_dispatcher)) != MH_OK)
        return make_result(OWH_INCOMPATIBLE, OWH_STAGE_SOURCE_REGISTER);
    // Even a partial enable failure cannot invalidate code a thread may reach.
    // Retain the trampoline; this process does not attempt removal or recovery.
    if (MH_EnableHook(entry) != MH_OK)
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_SOURCE_REGISTER);
    return make_result(OWH_OK);
}
} // namespace

struct EventSource::Impl {
    bool prepared = false;
};

EventSource::EventSource() : impl_(std::make_unique<Impl>()) {}
EventSource::~EventSource() { shutdown(); }

owh_result EventSource::capability() const noexcept {
    AcquireSRWLockShared(&hook_lock);
    const auto activation = activation_result;
    ReleaseSRWLockShared(&hook_lock);
    return activation.status == OWH_OK ? resolve_source() : activation;
}

owh_result EventSource::prepare(Callback callback, void *context) noexcept {
    if (!callback || !context)
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
    AcquireSRWLockExclusive(&hook_lock);
    owh_result result = make_result(OWH_ALREADY_EXISTS, OWH_STAGE_SOURCE_REGISTER);
    if (impl_->prepared) {
        if (source_state.callback == callback && source_state.context == context) {
            result = make_result(OWH_OK);
        }
    } else if (!source_claimed) {
        // Missing targets can become available later; no patch was attempted.
        result = resolve_source();
        if (result.status == OWH_OK) {
            source_claimed = true;
            activation_result = install_hook();
            result = activation_result;
            if (result.status == OWH_OK) {
                source_state.callback = callback;
                source_state.context = context;
                source_state.warnings.store(consumer_warnings(), std::memory_order_release);
                impl_->prepared = true;
            }
        }
    } else if (activation_result.status != OWH_OK) {
        result = activation_result;
    }
    ReleaseSRWLockExclusive(&hook_lock);
    return result;
}

void EventSource::set_enabled(bool enabled) noexcept {
    if (impl_->prepared) {
        source_state.enabled.store(enabled, std::memory_order_seq_cst);
        if (!enabled)
            while (source_state.active_exports.load(std::memory_order_seq_cst))
                SwitchToThread();
    }
}

EventSourceMetrics EventSource::metrics() const noexcept {
    return {impl_->prepared && source_state.enabled.load(std::memory_order_seq_cst),
            source_state.warnings.load(std::memory_order_acquire)};
}

void EventSource::shutdown() noexcept {
    AcquireSRWLockExclusive(&hook_lock);
    if (impl_->prepared) {
        stop_export();
        impl_->prepared = false;
    }
    ReleaseSRWLockExclusive(&hook_lock);
}

} // namespace overwolf_hook::overwolf
