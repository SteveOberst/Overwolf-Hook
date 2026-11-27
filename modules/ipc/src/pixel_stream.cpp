#include <windows.h>
#include <algorithm>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <owh/ipc/pixel_stream.hpp>
#include <owh/ipc/shared_lock.hpp>
#include <owh/ipc/shared_region.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/identity.hpp>
#include <owh/platform/result.hpp>
#include <string_view>
#include <winternl.h>
namespace overwolf_hook::ipc {
namespace {
constexpr uint32_t wire_magic = 0x3150574f;
struct Storage {
    std::unique_ptr<ipc::SharedRegion> region;
    Handle wake, completion, peer;
    void *view = nullptr;
    size_t bytes = 0;
    Header *header() const noexcept { return static_cast<Header *>(view); }
    SlotDescriptor *slot(uint32_t i) const noexcept {
        return reinterpret_cast<SlotDescriptor *>(static_cast<std::byte *>(view) +
                                                  slot_table_offset) +
               i;
    }
};
using SharedLock = ipc::SharedLock;
using LockState = ipc::SharedLockStatus;
owh_result lock_failure(LockState state, uint32_t stage) noexcept {
    switch (state) {
    case LockState::busy:
        return make_result(OWH_BUSY, stage);
    case LockState::cancelled:
        return make_result(OWH_CLOSED, stage);
    case LockState::peer_gone:
        return make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME);
    case LockState::corrupt:
        return make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE);
    case LockState::locked:
        break;
    }
    return make_result(OWH_SYSTEM_ERROR, stage);
}
bool layout(uint32_t w, uint32_t h, uint32_t slots, uint32_t policy, uint64_t &image,
            uint64_t &stride, uint64_t &total) noexcept {
    if (!w || !h || w > core::maximum_width || h > core::maximum_height ||
        slots < core::minimum_slots || slots > maximum_slots ||
        (policy != OWH_QUEUE_FIFO && policy != OWH_QUEUE_LATEST_READY))
        return false;
    image = uint64_t(w) * h * 4;
    stride = (image + 63) & ~uint64_t(63);
    total = header_page_bytes + stride * slots;
    return total <= maximum_mapping_bytes;
}
bool valid(const Storage &s, const QueueDescriptor &d) noexcept {
    const auto &h = *s.header();
    uint64_t image = 0, padded = 0, total = 0;
    if (!layout(d.width, d.height, d.slot_count, d.queue_policy, image, padded, total))
        return false;
    return h.magic == wire_magic && h.protocol == protocol_version &&
           h.header_bytes == sizeof(Header) && h.mapping_bytes == total &&
           d.mapping_bytes == total && h.ref == d.ref && h.producer_pid == d.producer_pid &&
           h.consumer_pid == d.consumer_pid && h.producer_started == d.producer_started &&
           h.consumer_started == d.consumer_started && h.width == d.width && h.height == d.height &&
           h.stride == d.width * 4 && h.pixel_format == OWH_PIXEL_BGRA8_UNORM &&
           h.slot_count == d.slot_count && h.queue_policy == d.queue_policy &&
           h.pixels_offset == header_page_bytes && h.slot_pixel_bytes == padded &&
           std::all_of(std::begin(h.reserved), std::end(h.reserved),
                       [](std::byte b) { return b == std::byte{}; });
}
bool valid_slot(const Storage &s, const QueueDescriptor &d, uint32_t i) noexcept {
    const auto &x = *s.slot(i);
    const auto image = uint64_t(d.width) * d.height * 4;
    const auto offset = header_page_bytes + ((image + 63) & ~uint64_t(63)) * i;
    return x.resource_id == d.ref.resource_id && !x.reserved0 && !x.reserved1 &&
           x.pixel_offset == offset && x.pixel_bytes == uint64_t(d.width) * d.height * 4 &&
           offset <= s.bytes && x.pixel_bytes <= s.bytes - offset &&
           std::all_of(std::begin(x.reserved), std::end(x.reserved),
                       [](std::byte b) { return b == std::byte{}; });
}
void finish_locked(SlotDescriptor &x, owh_result r) noexcept {
    x.result_status = r.status;
    x.failure_stage = r.failure_stage;
    x.error_domain = r.error_domain;
    x.native_code = r.native_code;
    x.state = static_cast<LONG>(r.status == OWH_OK ? SlotState::done : SlotState::rejected);
}
void finish_read_locked(Header &header, SlotDescriptor &slot, owh_result result) noexcept {
    finish_locked(slot, result);
    // Later commits must retain their changes, even while this read was in flight.
    // Keeping a superset avoids per-frame history and remains correct for FIFO.
    if (result.status == OWH_OK && slot.sequence == header.sequence_counter)
        header.pending_damage = {};
}
// Inspect types without signaling/resetting the supplied objects.
// https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntqueryobject
bool event_handle(HANDLE handle) noexcept {
    using Query = NTSTATUS(NTAPI *)(HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    const auto query =
        reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject"));
    if (!query)
        return false;
    alignas(void *) std::byte storage[1024]{};
    ULONG bytes = 0;
    if (query(handle, ObjectTypeInformation, storage, sizeof(storage), &bytes) < 0)
        return false;
    const auto &text = reinterpret_cast<PUBLIC_OBJECT_TYPE_INFORMATION *>(storage)->TypeName;
    const auto start = reinterpret_cast<uintptr_t>(storage),
               ptr = reinterpret_cast<uintptr_t>(text.Buffer);
    if (text.Length % sizeof(wchar_t) || ptr < start || ptr - start > sizeof(storage) ||
        text.Length > sizeof(storage) - (ptr - start))
        return false;
    return std::wstring_view(text.Buffer, text.Length / sizeof(wchar_t)) == L"Event";
}
Handle duplicate(HANDLE source, uint64_t value) noexcept {
    HANDLE h = nullptr;
    if (!value || !DuplicateHandle(source, reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value)),
                                   GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return {};
    return Handle(h);
}
} // namespace
uint64_t pixel_allocation_size(uint32_t width, uint32_t height, uint32_t slots,
                               uint32_t policy) noexcept {
    uint64_t image = 0, stride = 0, total = 0;
    return layout(width, height, slots, policy, image, stride, total) ? total : 0;
}
struct PixelProducer::Impl : Storage {
    QueueDescriptor descriptor{};
    bool header_constructed = false;
    mutable std::mutex pending_mutex;
    std::array<uint64_t, maximum_slots> pending_aborts{};

    void retain_abort(uint32_t slot, uint64_t lease) noexcept {
        if (slot >= descriptor.slot_count)
            return;
        std::lock_guard lock(pending_mutex);
        pending_aborts[slot] = lease;
    }

    void flush_aborts_locked() noexcept {
        std::lock_guard lock(pending_mutex);
        for (uint32_t i = 0; i < descriptor.slot_count; ++i) {
            const uint64_t lease = pending_aborts[i];
            if (!lease)
                continue;
            auto &slot = *this->slot(i);
            if (slot.lease_id == lease && slot.state == LONG(SlotState::writing))
                slot.state = LONG(SlotState::free);
            pending_aborts[i] = 0;
        }
    }
};
struct PixelConsumer::Impl : Storage {
    QueueDescriptor descriptor{};
    struct PendingCompletion {
        SlotDescriptor *slot = nullptr;
        uint64_t sequence = 0;
        owh_result outcome{};
        bool pending = false;
    };
    mutable std::mutex pending_mutex;
    std::array<PendingCompletion, maximum_slots> pending_completions{};
    bool pending_loss = false;
    owh_result pending_loss_result{};

    void retain_completion(SlotDescriptor *slot, uint64_t sequence, owh_result outcome) noexcept {
        const auto index = static_cast<size_t>(slot - this->slot(0));
        if (index >= descriptor.slot_count)
            return;
        std::lock_guard lock(pending_mutex);
        auto &pending = pending_completions[index];
        pending.slot = slot;
        pending.sequence = sequence;
        pending.outcome = outcome;
        pending.pending = true;
    }

    void retain_loss(owh_result outcome) noexcept {
        std::lock_guard lock(pending_mutex);
        pending_loss_result = outcome;
        pending_loss = true;
    }

    bool flush_completions_locked() noexcept {
        bool completed = false;
        std::lock_guard lock(pending_mutex);
        for (auto &pending : pending_completions) {
            if (!pending.pending)
                continue;
            if (!pending.slot || pending.slot->state != LONG(SlotState::reading) ||
                pending.slot->sequence != pending.sequence) {
                // The shared lease no longer matches the local completion
                // record, so this stream cannot safely be reused.
                header()->state = generation_lost;
                pending.pending = false;
                continue;
            }
            finish_read_locked(*header(), *pending.slot, pending.outcome);
            pending.pending = false;
            completed = true;
        }
        if (pending_loss) {
            header()->state = generation_lost;
            for (uint32_t i = 0; i < descriptor.slot_count; ++i) {
                auto &slot = *this->slot(i);
                if (slot.state == LONG(SlotState::ready)) {
                    finish_locked(slot, pending_loss_result);
                    completed = true;
                }
            }
            pending_loss = false;
        }
        return completed;
    }

    bool has_pending_completion() const noexcept {
        std::lock_guard lock(pending_mutex);
        return pending_loss ||
               std::any_of(pending_completions.begin(), pending_completions.end(),
                           [](const PendingCompletion &pending) { return pending.pending; });
    }
};
std::pair<owh_result, std::unique_ptr<PixelProducer>>
PixelProducer::create(ResourceRef ref, uint32_t pid, uint64_t time, uint32_t w, uint32_t h,
                      uint32_t slots, uint32_t policy, uint64_t budget) {
    uint64_t image = 0, padded = 0, total = 0;
    if (!ref.session_id || !ref.resource_id || !pid || !time ||
        !layout(w, h, slots, policy, image, padded, total))
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), nullptr};
    if (total > budget)
        return {make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION), nullptr};
    try {
        auto producer = std::unique_ptr<PixelProducer>(new PixelProducer());
        producer->impl_ = std::make_unique<Impl>();
        auto &s = *producer->impl_;
        s.peer.reset(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (!s.peer || platform::process_start(s.peer.get()) != time)
            return {make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME, OWH_ERROR_WIN32,
                                GetLastError()),
                    nullptr};
        s.region = ipc::SharedRegion::create(static_cast<size_t>(total));
        s.wake.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        s.completion.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!s.wake || !s.completion)
            return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_ALLOCATION, OWH_ERROR_WIN32,
                                GetLastError()),
                    nullptr};
        s.view = s.region->data();
        s.bytes = static_cast<size_t>(total);
        if (!s.view)
            return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_ALLOCATION, OWH_ERROR_WIN32,
                                GetLastError()),
                    nullptr};
        const auto own_started = platform::process_start(GetCurrentProcess());
        if (!own_started)
            return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_AGENT_LIFETIME, OWH_ERROR_WIN32,
                                GetLastError()),
                    nullptr};
        s.descriptor = {ref,
                        reinterpret_cast<uint64_t>(s.region->handle()),
                        reinterpret_cast<uint64_t>(s.wake.get()),
                        reinterpret_cast<uint64_t>(s.completion.get()),
                        own_started,
                        time,
                        total,
                        GetCurrentProcessId(),
                        pid,
                        w,
                        h,
                        slots,
                        policy};
        auto *header = ::new (s.view) Header{};
        s.header_constructed = true;
        auto &head = *header;
        head.magic = wire_magic;
        head.protocol = protocol_version;
        head.header_bytes = sizeof(Header);
        head.mapping_bytes = total;
        head.ref = ref;
        head.producer_pid = GetCurrentProcessId();
        head.consumer_pid = pid;
        head.producer_started = own_started;
        head.consumer_started = time;
        head.width = w;
        head.height = h;
        head.stride = w * 4;
        head.pixel_format = OWH_PIXEL_BGRA8_UNORM;
        head.slot_count = slots;
        head.queue_policy = policy;
        head.pixels_offset = header_page_bytes;
        head.slot_pixel_bytes = padded;
        head.state = generation_active;
        head.pending_damage = core::full_region(w, h);
        for (uint32_t i = 0; i < slots; ++i) {
            auto &x = *s.slot(i);
            x.resource_id = ref.resource_id;
            x.pixel_offset = header_page_bytes + padded * i;
            x.pixel_bytes = image;
        }
        return {make_result(OWH_OK), std::move(producer)};
    } catch (const std::bad_alloc &) {
        return {make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION), nullptr};
    }
}
PixelProducer::~PixelProducer() {
    if (!impl_ || !impl_->header_constructed || !impl_->wake)
        return;
    // Destruction is the terminal point for all producer API calls and borrowed
    // writable frames. Publish without taking the shared mutex so teardown can
    // make progress even when the consumer is concurrently closing.
    InterlockedExchange(&impl_->header()->producer_retired, 1);
    SetEvent(impl_->wake.get());
}
QueueDescriptor PixelProducer::descriptor() const noexcept { return impl_->descriptor; }
const Header &PixelProducer::header() const noexcept { return *impl_->header(); }
HANDLE PixelProducer::completion_event() const noexcept { return impl_->completion.get(); }
owh_result PixelProducer::host_status() const noexcept {
    if (!valid(*impl_, impl_->descriptor))
        return make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE);
    auto &mutable_storage = *const_cast<Impl *>(impl_.get());
    SharedLock guard(mutable_storage.header()->mutex, 0, mutable_storage.peer.get());
    const auto lock = guard.status();
    if (lock != LockState::locked)
        return lock_failure(lock, OWH_STAGE_AGENT_LIFETIME);
    mutable_storage.flush_aborts_locked();
    if (WaitForSingleObject(mutable_storage.peer.get(), 0) == WAIT_OBJECT_0) {
        mutable_storage.header()->state = generation_lost;
        return make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME);
    }
    const auto state = mutable_storage.header()->state;
    return make_result(state == generation_active   ? OWH_OK
                       : state == generation_closed ? OWH_CLOSED
                                                    : OWH_AGENT_LOST,
                       OWH_STAGE_AGENT_LIFETIME);
}
void PixelProducer::mark_closed() noexcept {
    if (!impl_)
        return;
    {
        SharedLock guard(impl_->header()->mutex, 500, impl_->peer.get());
        const auto lock = guard.status();
        if (lock != LockState::locked)
            return;
        impl_->header()->state = generation_closed;
    }
    SetEvent(impl_->wake.get());
}
PixelProducer::Acquired PixelProducer::try_acquire() {
    return acquire(false, false);
}
PixelProducer::Acquired PixelProducer::prepare_submit(bool replace_ready) {
    return acquire(true, replace_ready);
}
PixelProducer::Acquired PixelProducer::acquire(bool managed, bool replace_ready) {
    auto &s = *impl_;
    if (!valid(s, s.descriptor))
        return {make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE), {}};
    SharedLock guard(s.header()->mutex, 0, s.peer.get());
    if (guard.status() != LockState::locked)
        return {lock_failure(guard.status(), OWH_STAGE_ALLOCATION), {}};
    s.flush_aborts_locked();
    if (WaitForSingleObject(s.peer.get(), 0) == WAIT_OBJECT_0)
        return {make_result(OWH_AGENT_LOST), {}};
    if (s.header()->state != generation_active)
        return {make_result(s.header()->state == generation_closed ? OWH_CLOSED : OWH_AGENT_LOST), {}};
    std::optional<uint32_t> available, ready;
    for (uint32_t i = 0; i < s.descriptor.slot_count; ++i) {
        if (!valid_slot(s, s.descriptor, i))
            return {make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE), {}};
        const auto state = SlotState(s.slot(i)->state);
        if (managed && state == SlotState::writing)
            return {make_result(OWH_INVALID_ARGUMENT), {}};
        const bool reusable = state == SlotState::free ||
            (managed && (state == SlotState::done || state == SlotState::rejected));
        if (!available && reusable)
            available = i;
        if (!ready && state == SlotState::ready)
            ready = i;
    }
    const auto selected =
        replace_ready && s.descriptor.queue_policy == OWH_QUEUE_LATEST_READY && ready
            ? ready : available;
    if (!selected)
        return {make_result(OWH_BUSY), {}};
    if (s.header()->lease_counter == UINT64_MAX)
        return {make_result(OWH_RESOURCE_EXHAUSTED), {}};
    auto &x = *s.slot(*selected);
    std::optional<Completion> previous;
    if (x.state == LONG(SlotState::ready))
        previous = Completion{x.sequence, make_result(OWH_REPLACED), false, true};
    else if (x.state == LONG(SlotState::done) || x.state == LONG(SlotState::rejected))
        previous = Completion{x.sequence,
                              {static_cast<owh_status>(x.result_status), x.failure_stage,
                               x.error_domain, 0, x.native_code},
                              x.result_status == OWH_OK, true};
    x.state = LONG(SlotState::writing);
    x.lease_id = ++s.header()->lease_counter;
    x.sequence = 0;
    x.result_status = OWH_OK;
    return {make_result(OWH_OK),
            {*selected, x.lease_id,
             {static_cast<std::byte *>(s.view) + x.pixel_offset, static_cast<size_t>(x.pixel_bytes)}},
            previous};
}
owh_result PixelProducer::abort(const Writable &w) {
    auto &s = *impl_;
    SharedLock guard(s.header()->mutex, 50, s.peer.get());
    const auto locked = guard.status();
    if (locked != LockState::locked) {
        s.retain_abort(w.slot, w.lease_id);
        return lock_failure(locked, OWH_STAGE_CONTROL_IO);
    }
    s.flush_aborts_locked();
    if (w.slot >= s.descriptor.slot_count)
        return make_result(OWH_INVALID_ARGUMENT);
    auto &x = *s.slot(w.slot);
    if (x.lease_id != w.lease_id || x.state != LONG(SlotState::writing))
        return make_result(OWH_STALE_GENERATION);
    x.state = LONG(SlotState::free);
    return make_result(OWH_OK);
}
Commit PixelProducer::commit(const Writable &w, const core::PixelRegion *damage) {
    auto &s = *impl_;
    const auto region = damage ? *damage : core::full_region(s.descriptor.width, s.descriptor.height);
    if (!core::valid_region(region, s.descriptor.width, s.descriptor.height))
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), 0};
    if (!valid(s, s.descriptor))
        return {make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE), 0};
    uint64_t sequence = 0;
    {
        SharedLock guard(s.header()->mutex, 50, s.peer.get());
        const auto locked = guard.status();
        if (locked != LockState::locked)
            return {lock_failure(locked, OWH_STAGE_CONTROL_IO), 0};
        s.flush_aborts_locked();
        if (WaitForSingleObject(s.peer.get(), 0) == WAIT_OBJECT_0) {
            s.header()->state = generation_lost;
            return {make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME), 0};
        }
        if (s.header()->state != generation_active)
            return {
                make_result(s.header()->state == generation_closed ? OWH_CLOSED : OWH_AGENT_LOST,
                            OWH_STAGE_AGENT_LIFETIME),
                0};
        if (w.slot >= s.descriptor.slot_count || !valid_slot(s, s.descriptor, w.slot))
            return {make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE), 0};
        auto &x = *s.slot(w.slot);
        if (x.lease_id != w.lease_id || x.state != LONG(SlotState::writing))
            return {make_result(OWH_STALE_GENERATION), 0};
        if (s.header()->sequence_counter == UINT64_MAX)
            return {make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION), 0};
        sequence = ++s.header()->sequence_counter;
        s.header()->pending_damage = core::union_region(s.header()->pending_damage, region);
        x.sequence = sequence;
        x.state = LONG(SlotState::ready);
    }
    if (!signal_wake_(s.wake.get())) {
        const auto error = GetLastError();
        {
            SharedLock guard(s.header()->mutex, 50, s.peer.get());
            const auto relock = guard.status();
            if (relock == LockState::locked) {
                s.header()->state = generation_lost;
                auto &x = *s.slot(w.slot);
                if (x.sequence == sequence && x.state == LONG(SlotState::ready))
                    finish_locked(x, make_result(OWH_AGENT_LOST, OWH_STAGE_CONTROL_IO,
                                                 OWH_ERROR_WIN32, error));
            }
        }
        if (s.completion) {
            SetEvent(s.completion.get());
        }
        return {make_result(OWH_AGENT_LOST, OWH_STAGE_CONTROL_IO, OWH_ERROR_WIN32, error),
                sequence};
    }
    return {make_result(OWH_OK), sequence};
}
std::array<Completion, maximum_slots> PixelProducer::collect_completed(uint32_t &count) {
    count = 0;
    std::array<Completion, maximum_slots> out{};
    auto &s = *impl_;
    if (!valid(s, s.descriptor))
        return out;
    {
        SharedLock guard(s.header()->mutex, 50, s.peer.get());
        const auto locked = guard.status();
        if (locked != LockState::locked)
            return out;
        s.flush_aborts_locked();
        ResetEvent(s.completion.get());
        for (uint32_t i = 0; i < s.descriptor.slot_count; ++i) {
            auto &x = *s.slot(i);
            const LONG state = x.state;
            if (state != LONG(SlotState::done) && state != LONG(SlotState::rejected))
                continue;
            if (!valid_slot(s, s.descriptor, i)) {
                s.header()->state = generation_lost;
                continue;
            }
            out[count++] = {x.sequence,
                            {static_cast<owh_status>(x.result_status), x.failure_stage,
                             x.error_domain, 0, x.native_code},
                            x.result_status == OWH_OK,
                            true};
            x.state = LONG(SlotState::free);
        }
    }
    return out;
}
std::pair<owh_result, std::unique_ptr<PixelConsumer>>
PixelConsumer::import(const QueueDescriptor &d, HANDLE producer, ResourceRef ref, uint32_t w,
                      uint32_t h, uint32_t slots, uint32_t policy, uint64_t budget) {
    uint64_t image = 0, padded = 0, total = 0;
    if (d.ref != ref || !ref.session_id || !ref.resource_id ||
        d.consumer_pid != GetCurrentProcessId() ||
        d.consumer_started != platform::process_start(GetCurrentProcess()) || !producer ||
        GetProcessId(producer) != d.producer_pid ||
        platform::process_start(producer) != d.producer_started || d.width != w || d.height != h ||
        d.slot_count != slots || d.queue_policy != policy ||
        !layout(w, h, slots, policy, image, padded, total) || d.mapping_bytes != total)
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_HANDSHAKE), nullptr};
    if (total > budget)
        return {make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION), nullptr};
    try {
        auto consumer = std::unique_ptr<PixelConsumer>(new PixelConsumer());
        consumer->impl_ = std::make_shared<Impl>();
        auto &s = *consumer->impl_;
        s.descriptor = d;
        HANDLE retained = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), producer, GetCurrentProcess(), &retained, 0,
                             FALSE, DUPLICATE_SAME_ACCESS))
            return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_AGENT_LIFETIME, OWH_ERROR_WIN32,
                                GetLastError()),
                    nullptr};
        s.peer.reset(retained);
        s.region =
            ipc::SharedRegion::import(producer, d.mapping_handle, static_cast<size_t>(total));
        s.wake = duplicate(producer, d.wake_handle);
        s.completion = duplicate(producer, d.completion_handle);
        if (!s.wake || !s.completion)
            return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_CONTROL_IO, OWH_ERROR_WIN32,
                                GetLastError()),
                    nullptr};
        if (!event_handle(s.wake.get()) || !event_handle(s.completion.get()))
            return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_HANDSHAKE), nullptr};
        s.view = s.region->data();
        s.bytes = static_cast<size_t>(total);
        if (!s.view)
            return {
                make_result(OWH_SYSTEM_ERROR, OWH_STAGE_HANDSHAKE, OWH_ERROR_WIN32, GetLastError()),
                nullptr};
        if (!valid(s, d))
            return {make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE), nullptr};
        for (uint32_t i = 0; i < slots; ++i)
            if (!valid_slot(s, d, i))
                return {make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE), nullptr};
        return {make_result(OWH_OK), std::move(consumer)};
    } catch (const std::bad_alloc &) {
        return {make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION), nullptr};
    }
}
PixelConsumer::~PixelConsumer() = default;
HANDLE PixelConsumer::wake_event() const noexcept { return impl_->wake.get(); }
uint64_t PixelConsumer::mapping_bytes() const noexcept { return impl_->bytes; }
const Header &PixelConsumer::header() const noexcept { return *impl_->header(); }
std::pair<owh_result, std::unique_ptr<PixelRead>> PixelConsumer::try_take_ready() {
    auto &s = *impl_;
    if (!valid(s, s.descriptor))
        return {make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE), nullptr};
    bool signal_completion = false;
    std::unique_ptr<PixelRead> read;
    owh_result outcome = make_result(OWH_NO_DATA);
    {
        SharedLock guard(s.header()->mutex, 0, s.peer.get());
        const auto locked = guard.status();
        if (locked != LockState::locked)
            return {lock_failure(locked, OWH_STAGE_SOURCE_PUBLISH), nullptr};
        signal_completion = s.flush_completions_locked();
        if (WaitForSingleObject(s.peer.get(), 0) == WAIT_OBJECT_0) {
            s.header()->state = generation_lost;
            for (uint32_t i = 0; i < s.descriptor.slot_count; ++i) {
                auto &slot = *s.slot(i);
                if (slot.state == LONG(SlotState::ready)) {
                    finish_locked(slot, make_result(OWH_AGENT_LOST));
                    signal_completion = true;
                }
            }
            outcome = make_result(OWH_AGENT_LOST);
        } else if (s.header()->state != generation_active) {
            outcome =
                make_result(s.header()->state == generation_closed ? OWH_CLOSED : OWH_AGENT_LOST,
                            OWH_STAGE_AGENT_LIFETIME);
        } else {
            uint32_t candidate = s.descriptor.slot_count;
            uint64_t selected = s.descriptor.queue_policy == OWH_QUEUE_FIFO ? UINT64_MAX : 0;
            for (uint32_t i = 0; i < s.descriptor.slot_count; ++i) {
                auto &x = *s.slot(i);
                if (x.state == LONG(SlotState::ready) &&
                    (s.descriptor.queue_policy == OWH_QUEUE_FIFO ? x.sequence < selected
                                                                 : x.sequence > selected)) {
                    selected = x.sequence;
                    candidate = i;
                }
            }
            if (candidate != s.descriptor.slot_count) {
                auto &x = *s.slot(candidate);
                x.state = LONG(SlotState::reading);
                if (!valid_slot(s, s.descriptor, candidate) ||
                    !core::valid_region(s.header()->pending_damage,
                                        s.descriptor.width, s.descriptor.height)) {
                    finish_locked(x, make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE));
                    signal_completion = true;
                    outcome = make_result(OWH_CORRUPT, OWH_STAGE_HANDSHAKE);
                } else {
                    if (s.descriptor.queue_policy == OWH_QUEUE_LATEST_READY)
                        for (uint32_t i = 0; i < s.descriptor.slot_count; ++i) {
                            if (i == candidate)
                                continue;
                            auto &o = *s.slot(i);
                            if (o.state != LONG(SlotState::ready))
                                continue;
                            if (o.sequence < selected) {
                                o.state = LONG(SlotState::reading);
                                finish_locked(o, make_result(OWH_REPLACED));
                                signal_completion = true;
                            }
                        }
                    try {
                        read.reset(new PixelRead(impl_, &x, selected));
                        outcome = make_result(OWH_OK);
                    } catch (const std::bad_alloc &) {
                        finish_locked(x, make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION));
                        signal_completion = true;
                        outcome = make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION);
                    }
                }
            }
        }
    }
    if (signal_completion)
        SetEvent(s.completion.get());
    return {outcome, std::move(read)};
}
bool PixelConsumer::has_ready() const noexcept {
    auto &s = *impl_;
    if (s.has_pending_completion())
        return true;
    if (!valid(s, s.descriptor))
        return false;
    bool ready = false;
    {
        SharedLock guard(s.header()->mutex, 0, s.peer.get());
        const auto locked = guard.status();
        if (locked != LockState::locked)
            return true;
        ready = s.header()->state == generation_active &&
                WaitForSingleObject(s.peer.get(), 0) == WAIT_OBJECT_0;
        for (uint32_t i = 0; i < s.descriptor.slot_count; ++i)
            ready = ready || (s.header()->state == generation_active &&
                              s.slot(i)->state == LONG(SlotState::ready));
    }
    return ready || s.has_pending_completion();
}
void PixelConsumer::flush_completions() noexcept {
    auto &s = *impl_;
    if (!s.has_pending_completion() || !valid(s, s.descriptor))
        return;
    bool completed = false;
    {
        SharedLock guard(s.header()->mutex, 0, s.peer.get());
        if (guard.status() != LockState::locked)
            return;
        completed = s.flush_completions_locked();
    }
    if (completed)
        SetEvent(s.completion.get());
}
void PixelConsumer::reject_ready(owh_result r) noexcept {
    auto &s = *impl_;
    bool completed = false;
    {
        SharedLock guard(s.header()->mutex, 50, s.peer.get());
        const auto locked = guard.status();
        if (locked != LockState::locked) {
            s.retain_loss(r);
            SetEvent(s.wake.get());
            return;
        }
        completed = s.flush_completions_locked();
        for (uint32_t i = 0; i < s.descriptor.slot_count; ++i) {
            auto &x = *s.slot(i);
            if (x.state == LONG(SlotState::ready)) {
                x.state = LONG(SlotState::reading);
                finish_locked(x, r);
                completed = true;
            }
        }
    }
    if (completed)
        SetEvent(s.completion.get());
}
void PixelConsumer::mark_lost(owh_result r) noexcept {
    auto &s = *impl_;
    bool completed = false;
    {
        SharedLock guard(s.header()->mutex, 100, s.peer.get());
        const auto locked = guard.status();
        if (locked != LockState::locked) {
            s.retain_loss(r);
            SetEvent(s.wake.get());
            SetEvent(s.completion.get());
            return;
        }
        completed = s.flush_completions_locked();
        s.header()->state = generation_lost;
        for (uint32_t i = 0; i < s.descriptor.slot_count; ++i) {
            auto &slot = *s.slot(i);
            if (slot.state == LONG(SlotState::ready)) {
                slot.state = LONG(SlotState::reading);
                finish_locked(slot, r);
                completed = true;
            }
        }
    }
    SetEvent(s.wake.get());
    if (completed)
        SetEvent(s.completion.get());
}
owh_result PixelConsumer::settle_for_close() {
    auto &s = *impl_;
    bool completed = false;
    owh_result outcome = make_result(OWH_OK);
    {
        SharedLock guard(s.header()->mutex, 50, s.peer.get());
        const auto locked = guard.status();
        if (locked != LockState::locked)
            return lock_failure(locked, OWH_STAGE_CLEANUP);
        completed = s.flush_completions_locked();
        for (uint32_t i = 0; i < s.descriptor.slot_count; ++i) {
            auto &slot = *s.slot(i);
            if (slot.state == LONG(SlotState::ready)) {
                slot.state = LONG(SlotState::reading);
                finish_locked(slot, make_result(OWH_CLOSED, OWH_STAGE_CLEANUP));
                completed = true;
            }
        }
        for (uint32_t i = 0; i < s.descriptor.slot_count; ++i) {
            auto &slot = *s.slot(i);
            const auto state = slot.state;
            const bool producer_retired =
                InterlockedCompareExchange(&s.header()->producer_retired, 0, 0) != 0;
            if (state == LONG(SlotState::writing) &&
                (producer_retired || WaitForSingleObject(s.peer.get(), 0) == WAIT_OBJECT_0)) {
                slot.state = LONG(SlotState::reading);
                finish_locked(slot, make_result(OWH_AGENT_LOST));
                completed = true;
                continue;
            }
            if (state == LONG(SlotState::writing) || state == LONG(SlotState::reading)) {
                outcome = make_result(OWH_BUSY, OWH_STAGE_CLEANUP);
                break;
            }
        }
        if (outcome.status == OWH_OK)
            s.header()->state = generation_closed;
    }
    if (completed)
        SetEvent(s.completion.get());
    if (outcome.status == OWH_OK)
        SetEvent(s.wake.get());
    return outcome;
}
PixelRead::PixelRead(std::shared_ptr<PixelConsumer::Impl> b, SlotDescriptor *s, uint64_t seq)
    : backing_(std::move(b)), slot_(s), sequence_(seq) {
    const auto index = static_cast<size_t>(s - backing_->slot(0));
    const auto bytes = uint64_t(backing_->descriptor.width) * backing_->descriptor.height * 4;
    const auto offset = header_page_bytes + ((bytes + 63) & ~uint64_t(63)) * index;
    pixels_ = {static_cast<const std::byte *>(backing_->view) + offset, static_cast<size_t>(bytes)};
    damage_ = backing_->header()->pending_damage;
}
PixelRead::~PixelRead() {
    if (!done_)
        complete(make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME));
}
uint64_t PixelRead::sequence() const noexcept { return sequence_; }
uint32_t PixelRead::width() const noexcept { return backing_->descriptor.width; }
uint32_t PixelRead::height() const noexcept { return backing_->descriptor.height; }
uint32_t PixelRead::stride() const noexcept { return width() * 4; }
std::span<const std::byte> PixelRead::pixels() const noexcept { return pixels_; }
void PixelRead::complete(owh_result r) noexcept {
    if (done_)
        return;
    {
        SharedLock guard(backing_->header()->mutex, 100, backing_->peer.get());
        const auto locked = guard.status();
        if (locked != LockState::locked) {
            backing_->retain_completion(slot_, sequence_, r);
            done_ = true;
            return;
        }
        if (slot_->state != LONG(SlotState::reading) || slot_->sequence != sequence_) {
            backing_->header()->state = generation_lost;
        } else {
            finish_read_locked(*backing_->header(), *slot_, r);
        }
    }
    SetEvent(backing_->completion.get());
    done_ = true;
}
} // namespace overwolf_hook::ipc
