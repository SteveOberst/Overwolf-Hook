#include "profile.hpp"
#include "channel.hpp"
#include "transport/channel.hpp"
#include <overwolf_event_hook/probe.h>
#include <MinHook.h>
#include <bcrypt.h>
#include <atomic>
#include <cstring>

namespace {
using namespace overwolf_event_hook;
namespace profile = overwolf_event_hook::overwolf;
// RCX=self, RDX=output, R8=age. Preserve the uninterpreted R9 register too;
// its decompiler parameter is only propagated to a diagnostic failure path.
using ReadEvents = uintptr_t(__fastcall*)(void*, profile::EventRange*, uintptr_t, uintptr_t);
ReadEvents original = nullptr;
void* target = nullptr;
bool hook_created = false;
bool minhook_initialized = false;
SRWLOCK control = SRWLOCK_INIT;
Handle stop_event, ready_event, worker_thread, owner;
oeh_probe_options options{};
oeh_writer* writer = nullptr;
std::atomic<bool> capture_enabled{false};
std::atomic<uint64_t> calls{0}, seen{0}, forwarded{0}, rejected{0}, filtered{0}, invalid{0}, active{0};
std::atomic<uint32_t> status{OEH_CLOSED}, native_error{0};
thread_local bool recursing = false;

// Keep SEH isolated from C++ automatic objects requiring unwinding.
bool copy_bytes(void* to, const void* from, size_t size) noexcept {
    __try { std::memcpy(to, from, size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uint32_t source_channel(void* self) noexcept {
    uintptr_t service = 0, length = 0, capacity = 0, data = 0;
    const auto base = reinterpret_cast<uintptr_t>(self);
    if (!copy_bytes(&service, reinterpret_cast<void*>(base + profile::reader_service), 8) || !service ||
        !copy_bytes(&length, reinterpret_cast<void*>(service + profile::service_name_length), 8) ||
        !copy_bytes(&capacity, reinterpret_cast<void*>(service + profile::service_name_capacity), 8) ||
        length > 63 || capacity < length) return 0;
    data = service + profile::service_name;
    if (capacity > 15 && !copy_bytes(&data, reinterpret_cast<void*>(data), 8)) return 0;
    char name[64]{};
    if (!data || !copy_bytes(name, reinterpret_cast<void*>(data), length)) return 0;
    return profile::event_channel(std::string_view(name, length));
}

void capture(void* self, profile::EventRange* output) noexcept {
    const uint32_t channel = source_channel(self);
    if (!channel || (options.source_channel != OEH_ALL_CHANNELS && options.source_channel != channel)) {
        ++filtered; return;
    }
    profile::EventRange range{};
    if (!copy_bytes(&range, output, sizeof(range)) || range.end < range.begin ||
        range.reserved_end < range.end || (range.end - range.begin) % profile::raw_event_bytes ||
        range.end - range.begin > profile::raw_event_bytes * profile::maximum_batch) {
        ++invalid; return;
    }
    const size_t count = (range.end - range.begin) / profile::raw_event_bytes;
    seen += count;
    oeh_probe_frame frame{};
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    frame.captured_qpc = static_cast<uint64_t>(now.QuadPart);
    frame.source_channel = channel;
    frame.raw_size = static_cast<uint32_t>(profile::raw_event_bytes);
    for (size_t i = 0; i < count; ++i) {
        if (!copy_bytes(frame.raw, reinterpret_cast<void*>(range.begin + i * profile::raw_event_bytes),
            profile::raw_event_bytes)) { ++invalid; continue; }
        if (oeh_writer_publish(writer, &frame, sizeof(frame)).status == OEH_OK) ++forwarded;
        else ++rejected;
    }
}

uintptr_t __fastcall tapped(void* self, profile::EventRange* output, uintptr_t age, uintptr_t opaque) {
    ++active;
    // Let any original C++ exception continue to the original caller, while
    // balancing our counter. Do not catch/translate host exceptions.
    uintptr_t result;
    try { result = original(self, output, age, opaque); }
    catch (...) { --active; throw; }
    const DWORD saved_error = GetLastError();
    ++calls;
    if (capture_enabled.load() && !recursing && (result & 255)) {
        recursing = true;
        capture(self, output);
        recursing = false;
    }
    --active;
    SetLastError(saved_error);
    return result;
}

uint64_t creation_time(HANDLE process) {
    FILETIME start{}, end{}, kernel{}, user{};
    if (!GetProcessTimes(process, &start, &end, &kernel, &user)) fail(OEH_SYSTEM_ERROR, GetLastError());
    return (uint64_t{start.dwHighDateTime} << 32) | start.dwLowDateTime;
}

bool reviewed_file(HMODULE consumer) {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(consumer, path, 32768)) fail(OEH_SYSTEM_ERROR, GetLastError());
    Handle file(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) fail(OEH_SYSTEM_ERROR, GetLastError());
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        fail(OEH_SYSTEM_ERROR);
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool matches = false;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
        unsigned char bytes[16384]; DWORD count = 0; bool valid = true;
        for (;;) {
            if (!ReadFile(file.get(), bytes, sizeof(bytes), &count, nullptr)) { valid = false; break; }
            if (!count) break;
            if (BCryptHashData(hash, bytes, count, 0) < 0) { valid = false; break; }
        }
        std::array<uint8_t, 32> digest{};
        matches = valid && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0 &&
            digest == profile::consumer_sha256;
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return matches;
}

DWORD WINAPI run(void*) noexcept {
    const auto initialization = boundary([] {
        HMODULE consumer = GetModuleHandleW(L"OWGameEventsConsumer.dll");
        if (!consumer) fail(OEH_NOT_FOUND, GetLastError());
        if (!reviewed_file(consumer)) fail(OEH_INCOMPATIBLE);
        auto* entry = reinterpret_cast<unsigned char*>(consumer) + profile::reader_rva;
        // Also check on re-enable: never overwrite another hook installed since
        // our previous session stopped.
        if (std::memcmp(entry, profile::reader_entry.data(), profile::reader_entry.size()) != 0)
            fail(OEH_INCOMPATIBLE);
        HMODULE pinned;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&run), &pinned) ||
            !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(entry), &pinned)) fail(OEH_SYSTEM_ERROR, GetLastError());
        const auto created = oeh_writer_create(OEH_ABI_VERSION, options.channel, 256, sizeof(oeh_probe_frame), &writer);
        if (created.status != OEH_OK) throw Failure{created};
        if (!minhook_initialized) {
            if (MH_Initialize() != MH_OK) fail(OEH_SYSTEM_ERROR);
            minhook_initialized = true;
        }
        if (!hook_created) {
            target = entry;
            const auto hooked = MH_CreateHook(target, reinterpret_cast<void*>(&tapped), reinterpret_cast<void**>(&original));
            if (hooked != MH_OK) fail(OEH_INCOMPATIBLE, static_cast<DWORD>(hooked));
            hook_created = true;
        }
        capture_enabled = true;
        const auto enabled = MH_EnableHook(target);
        if (enabled != MH_OK) { capture_enabled = false; fail(OEH_SYSTEM_ERROR, static_cast<DWORD>(enabled)); }
        return ok;
    });
    status = static_cast<uint32_t>(initialization.status); native_error = initialization.native_error;
    SetEvent(ready_event.get());
    if (initialization.status == OEH_OK) {
        HANDLE waiters[]{stop_event.get(), owner.get()};
        const DWORD waited = WaitForMultipleObjects(2, waiters, FALSE, INFINITE);
        const DWORD wait_error = waited == WAIT_FAILED ? GetLastError() : 0;
        capture_enabled = false;
        const auto disabled = MH_DisableHook(target);
        if (disabled != MH_OK) {
            status = OEH_SYSTEM_ERROR; native_error = static_cast<uint32_t>(disabled);
            return OEH_SYSTEM_ERROR; // Keep writer/trampoline valid if patch removal failed.
        }
        while (active.load()) Sleep(1);
        status = waited == WAIT_FAILED ? OEH_SYSTEM_ERROR : OEH_CLOSED;
        if (waited == WAIT_FAILED) native_error = wait_error;
    }
    if (writer) {
        oeh_result closed;
        do { closed = oeh_writer_close(writer); if (closed.status == OEH_BUSY) Sleep(1); }
        while (closed.status == OEH_BUSY);
        if (closed.status == OEH_OK) writer = nullptr;
        else { status = static_cast<uint32_t>(closed.status); native_error = closed.native_error; }
    }
    return status.load();
}
struct ControlLock {
    ControlLock() { AcquireSRWLockExclusive(&control); }
    ~ControlLock() { ReleaseSRWLockExclusive(&control); }
};
}

extern "C" OEH_CAPTURE uint32_t __stdcall oeh_probe_start(void* input) {
    const auto result = overwolf_event_hook::boundary([&] {
        ControlLock lock;
        if (worker_thread.get() && WaitForSingleObject(worker_thread.get(), 0) != WAIT_OBJECT_0)
            fail(OEH_ALREADY_EXISTS);
        // A failed disable/close may deliberately retain resources. Do not
        // replace their owner or writer with a second session.
        if (writer) fail(OEH_ALREADY_EXISTS);
        if (!input || !copy_bytes(&options, input, sizeof(options)) || options.size != sizeof(options) ||
            options.abi_version != OEH_ABI_VERSION || !options.owner_process_id ||
            !std::memchr(options.channel, 0, sizeof(options.channel))) fail(OEH_INVALID_ARGUMENT);
        owner = Handle(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, options.owner_process_id));
        if (!owner.get()) fail(OEH_SYSTEM_ERROR, GetLastError());
        if (creation_time(owner.get()) != options.owner_started) fail(OEH_INVALID_ARGUMENT);
        stop_event = Handle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        ready_event = Handle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!stop_event.get() || !ready_event.get()) fail(OEH_SYSTEM_ERROR, GetLastError());
        calls = seen = forwarded = rejected = filtered = invalid = 0;
        status = OEH_BUSY; native_error = 0;
        worker_thread = Handle(CreateThread(nullptr, 0, run, nullptr, 0, nullptr));
        if (!worker_thread.get()) fail(OEH_SYSTEM_ERROR, GetLastError());
        if (WaitForSingleObject(ready_event.get(), 10000) != WAIT_OBJECT_0) {
            SetEvent(stop_event.get()); fail(OEH_BUSY);
        }
        return oeh_result{static_cast<oeh_status>(status.load()), native_error.load()};
    });
    return static_cast<uint32_t>(result.status);
}

extern "C" OEH_CAPTURE uint32_t __stdcall oeh_probe_stop(void*) {
    ControlLock lock;
    if (!worker_thread.get()) return OEH_CLOSED;
    SetEvent(stop_event.get());
    if (WaitForSingleObject(worker_thread.get(), 5000) != WAIT_OBJECT_0) return OEH_BUSY;
    return status.load() == OEH_CLOSED ? OEH_OK : status.load();
}

extern "C" OEH_CAPTURE uint32_t __stdcall oeh_probe_query(void* output) {
    const oeh_probe_statistics sample{calls.load(), seen.load(), forwarded.load(), rejected.load(),
        filtered.load(), invalid.load(), active.load(), status.load(), native_error.load(),
        capture_enabled.load() ? 1u : 0u, 0};
    return output && copy_bytes(output, &sample, sizeof(sample)) ? OEH_OK : OEH_INVALID_ARGUMENT;
}
