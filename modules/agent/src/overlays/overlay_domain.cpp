#include "overlay_domain.hpp"

#include <owh/platform/result.hpp>

#include <algorithm>
#include <map>
#include <mutex>
#include <variant>
#include <vector>

namespace overwolf_hook::agent {
namespace {
using Key = std::pair<uint64_t, uint64_t>;
Key key(core::ResourceRef ref) noexcept { return {ref.session_id, ref.resource_id}; }
constexpr uint64_t process_texture_budget = core::process_budget_bytes;
} // namespace

struct OverlayDomain::State final : overwolf::OverlayWork,
                                    std::enable_shared_from_this<OverlayDomain::State> {
    struct WaitRegistration {
        std::weak_ptr<State> owner;
        HANDLE handle = nullptr;

        explicit WaitRegistration(std::weak_ptr<State> state) : owner(std::move(state)) {}
        WaitRegistration(const WaitRegistration &) = delete;
        WaitRegistration &operator=(const WaitRegistration &) = delete;
        ~WaitRegistration() { reset(); }

        static VOID CALLBACK callback(PVOID context, BOOLEAN) noexcept {
            auto *registration = static_cast<WaitRegistration *>(context);
            if (registration) {
                if (auto state = registration->owner.lock())
                    state->host.wake();
            }
        }
        bool register_for(HANDLE event) noexcept {
            return event && RegisterWaitForSingleObject(&handle, event, callback, this, INFINITE,
                                                        WT_EXECUTEDEFAULT) != FALSE;
        }
        void reset() noexcept {
            if (handle) {
                UnregisterWaitEx(handle, INVALID_HANDLE_VALUE);
                handle = nullptr;
            }
        }
    };
    struct PixelState {
        std::unique_ptr<ipc::PixelConsumer> consumer;
        std::unique_ptr<ipc::PixelRead> pending_read;
        WaitRegistration wake_registration;

        explicit PixelState(std::unique_ptr<ipc::PixelConsumer> value, std::weak_ptr<State> owner)
            : consumer(std::move(value)), wake_registration(std::move(owner)) {}
    };
    struct TextureState {};

    struct Record {
        core::ResourceRef ref{};
        std::shared_ptr<const core::SessionPolicy> policy;
        std::variant<PixelState, TextureState> resource{TextureState{}};
        std::shared_ptr<overwolf::NativeOverlay> native;
        std::atomic<bool> creating{true};
        std::atomic<bool> closing{false};
        owh_result last = make_result(OWH_OK);
        owh_result cleanup = make_result(OWH_OK);

        PixelState *pixels() noexcept { return std::get_if<PixelState>(&resource); }
        const PixelState *pixels() const noexcept { return std::get_if<PixelState>(&resource); }
    };
    explicit State(overwolf::OverlayHost &h) : host(h) {}
    ~State() override {
        std::map<Key, std::shared_ptr<Record>> remaining;
        {
            std::lock_guard lock(records_mutex);
            remaining.swap(resources);
        }
        remaining.clear(); // PixelState's wait registration joins exactly once in its destructor.
    }
    std::shared_ptr<Record> find(core::ResourceRef ref) const noexcept {
        std::lock_guard lock(records_mutex);
        const auto it = resources.find(key(ref));
        return it == resources.end() ? nullptr : it->second;
    }
    std::vector<std::shared_ptr<Record>> snapshot() const {
        std::vector<std::shared_ptr<Record>> out;
        std::lock_guard lock(records_mutex);
        out.reserve(resources.size());
        for (const auto &[_, record] : resources)
            out.push_back(record);
        return out;
    }
    void erase(const std::shared_ptr<Record> &r) noexcept {
        std::lock_guard lock(records_mutex);
        const auto it = resources.find(key(r->ref));
        if (it != resources.end() && it->second == r)
            resources.erase(it);
    }
    uint64_t texture_bytes_in_use(uint64_t owner, uint64_t *process_total = nullptr) const noexcept {
        uint64_t owner_total = 0, process = 0;
        std::lock_guard lock(records_mutex);
        for (const auto &[_, r] : resources) {
            const auto bytes = r->native ? r->native->texture_allocation_bytes() : 0;
            if (bytes > process_texture_budget - std::min(process, process_texture_budget))
                process = process_texture_budget;
            else
                process += bytes;
            if (r->ref.session_id == owner) {
                if (bytes > r->policy->shared_texture_budget_bytes -
                                std::min(owner_total, r->policy->shared_texture_budget_bytes))
                    owner_total = r->policy->shared_texture_budget_bytes;
                else
                    owner_total += bytes;
            }
        }
        if (process_total)
            *process_total = process;
        return owner_total;
    }
    owh_result admit(const std::shared_ptr<Record> &record) {
        std::lock_guard lock(records_mutex);
        if (resources.contains(key(record->ref)))
            return make_result(OWH_ALREADY_EXISTS);
        uint64_t session_bytes = 0, total_bytes = 0;
        size_t session_count = 0;
        for (const auto &[_, current] : resources) {
            const auto bytes = current->pixels() ? current->pixels()->consumer->mapping_bytes() : 0;
            total_bytes += bytes;
            if (current->ref.session_id == record->ref.session_id) {
                ++session_count;
                session_bytes += bytes;
            }
        }
        const auto bytes = record->pixels() ? record->pixels()->consumer->mapping_bytes() : 0;
        const auto limit = record->policy->pixel_buffer_budget_bytes;
        if (resources.size() >= core::maximum_process_overlays ||
            session_count >= record->policy->maximum_overlays ||
            bytes > limit - std::min(session_bytes, limit) ||
            bytes > core::process_budget_bytes - std::min(total_bytes, core::process_budget_bytes))
            return make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION);
        resources.emplace(key(record->ref), record);
        return ok;
    }
    owh_result create_native(const std::shared_ptr<Record> &record, core::Geometry geometry) {
        bool callback_ran = false;
        const auto status = host.execute([&] {
            callback_ran = true;
            auto native = record->pixels() ? host.create_pixel_buffer(geometry) : host.create_shared_texture(geometry);
            record->native = std::move(native.overlay);
            record->creating.store(false, std::memory_order_release);
            if (native.result.status != OWH_OK) {
                if (record->native)
                    record->closing.store(true, std::memory_order_release);
                else
                    erase(record);
            }
            return native.result;
        });
        if (!callback_ran) {
            record->creating.store(false, std::memory_order_release);
            erase(record);
        }
        host.wake();
        return status;
    }
    void pump(const std::shared_ptr<Record> &r) noexcept {
        if (r->creating.load(std::memory_order_acquire))
            return;
        // Health and epoch facts become persistent domain terminal state only
        // on this actor pump path. info() below merely observes and derives.
        if (r->native && r->last.status == OWH_OK) {
            const auto facts = r->native->info();
            const auto fact_error = facts.first.status != OWH_OK ? facts.first
                                    : facts.second.last_result.status != OWH_OK
                                        ? facts.second.last_result
                                        : make_result(OWH_OK);
            if (fact_error.status != OWH_OK) {
                r->last = fact_error;
                if (auto *pixel = r->pixels()) {
                    if (pixel->pending_read) {
                        pixel->pending_read->complete(fact_error);
                        pixel->pending_read.reset();
                    }
                    pixel->consumer->mark_lost(fact_error);
                }
            }
        }
        auto *pixel = r->pixels();
        if (r->closing.load(std::memory_order_acquire)) {
            if (pixel && pixel->pending_read) {
                pixel->pending_read->complete(make_result(OWH_CLOSED, OWH_STAGE_CLEANUP));
                pixel->pending_read.reset();
            }
            if (pixel) {
                const auto settled = pixel->consumer->settle_for_close();
                if (settled.status != OWH_OK) {
                    r->cleanup = settled;
                    return;
                }
            }
            std::shared_ptr<overwolf::NativeOverlay> native;
            native = r->native;
            if (native && native->needs_cleanup()) {
                const auto closed = native->close();
                if (closed.status != OWH_OK) {
                    r->cleanup = closed;
                    return;
                }
            }
            erase(r);
            return;
        }
        if (!pixel)
            return;
        if (r->last.status != OWH_OK) {
            pixel->consumer->flush_completions();
            return;
        }
        std::shared_ptr<overwolf::NativeOverlay> native;
        native = r->native;
        if (!native)
            return;
        if (!pixel->pending_read) {
            auto [status, read] = pixel->consumer->try_take_ready();
            if (status.status == OWH_NO_DATA || status.status == OWH_BUSY)
                return;
            if (status.status != OWH_OK || !read) {
                r->last = status;
                pixel->consumer->mark_lost(status);
                return;
            }
            pixel->pending_read = std::move(read);
        }
        const overwolf::PixelView view{pixel->pending_read->pixels(), pixel->pending_read->width(),
                                       pixel->pending_read->height(),
                                       pixel->pending_read->stride(), pixel->pending_read->damage()};
        const auto published = native->publish(r->ref, view);
        if (published.status == OWH_BUSY)
            return;
        pixel->pending_read->complete(published);
        pixel->pending_read.reset();
        r->last = published;
        if (published.status != OWH_OK) {
            pixel->consumer->mark_lost(published.status == OWH_AGENT_LOST
                                           ? published
                                           : make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME));
        }
    }
    void process_on_actor() noexcept override {
        try {
            auto records = snapshot();
            if (records.empty())
                return;
            size_t start = cursor % records.size();
            for (size_t n = 0; n < records.size(); ++n)
                pump(records[(start + n) % records.size()]);
            cursor = (start + 1) % records.size();
        } catch (...) { /* Keep ownership and retry after the next actor wake. */
        }
    }
    bool has_pending_work() const noexcept override {
        try {
            const auto records = snapshot();
            for (const auto &r : records)
                if (r->closing.load(std::memory_order_acquire) ||
                    (r->pixels() &&
                     (r->pixels()->pending_read || r->pixels()->consumer->has_ready())))
                    return true;
        } catch (...) {
            return true;
        }
        return false;
    }

    overwolf::OverlayHost &host;
    mutable std::mutex records_mutex;
    std::map<Key, std::shared_ptr<Record>> resources;
    size_t cursor = 0;
};

OverlayDomain::OverlayDomain(overwolf::OverlayHost &host) : state_(std::make_shared<State>(host)) {
    host.set_work(state_);
}
OverlayDomain::~OverlayDomain() {
    // The production root is process-resident. Owned roots may be destroyed in
    // fixtures: retire their native sources before releasing any texture/stream pin.
    // A blocked foreign call or unsafe cleanup cannot be forced to completion.
    {
        std::lock_guard lock(state_->records_mutex);
        for (const auto &[_, record] : state_->resources)
            record->closing.store(true, std::memory_order_release);
    }
    state_->host.wake();
    for (;;) {
        {
            std::lock_guard lock(state_->records_mutex);
            if (state_->resources.empty())
                break;
        }
        Sleep(10);
    }
    // Pass an actor barrier while both State and the Host wrapper are pinned.
    state_->host.clear_work();
    state_.reset();
}

std::pair<owh_result, core::ResourceRef>
OverlayDomain::create_pixel_buffer(core::ResourceRef ref, core::Geometry geometry,
                             const core::PixelBufferRef &q, HANDLE peer,
                             std::shared_ptr<const core::SessionPolicy> policy) {
    std::pair<owh_result, core::ResourceRef> out{
        make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), {}};
    const auto queue =
        core::PixelBufferOverlayOptions{sizeof(core::PixelBufferOverlayOptions), q.slot_count, q.queue_policy};
    if (!ref.session_id || !ref.resource_id || q.ref != ref || !core::valid_geometry(geometry) ||
        !core::valid_queue(queue) || (!policy || !core::valid_policy(*policy)) ||
        q.width != geometry.width || q.height != geometry.height || !peer)
        return out;
    const uint64_t bytes =
        ipc::pixel_allocation_size(q.width, q.height, q.slot_count, q.queue_policy);
    if (!bytes || bytes > policy->pixel_buffer_budget_bytes) {
        out.first = make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION);
        return out;
    }
    auto [import_result, consumer] =
        ipc::PixelConsumer::import(q, peer, ref, q.width, q.height, q.slot_count, q.queue_policy,
                                   policy->pixel_buffer_budget_bytes);
    if (import_result.status != OWH_OK) {
        out.first = import_result;
        return out;
    }
    auto record = std::make_shared<State::Record>();
    record->ref = ref;
    record->policy = policy;
    record->resource.emplace<State::PixelState>(std::move(consumer), state_);
    const auto admitted = state_->admit(record);
    if (admitted.status != OWH_OK)
        return {admitted, {}};
    auto *pixel = record->pixels();
    if (!pixel || !pixel->wake_registration.register_for(pixel->consumer->wake_event())) {
        state_->erase(record);
        out.first = make_result(OWH_SYSTEM_ERROR, OWH_STAGE_ALLOCATION);
        return out;
    }
    const auto created = state_->create_native(record, geometry);
    return {created, created.status == OWH_OK ? ref : core::ResourceRef{}};
}

std::pair<owh_result, core::ResourceRef>
OverlayDomain::create_shared_texture(core::ResourceRef ref, core::Geometry geometry,
                              std::shared_ptr<const core::SessionPolicy> policy) {
    if (!ref.session_id || !ref.resource_id || !core::valid_geometry(geometry) ||
        (!policy || !core::valid_policy(*policy)))
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), {}};
    auto record = std::make_shared<State::Record>();
    record->ref = ref;
    record->policy = policy;
    record->resource.emplace<State::TextureState>();
    const auto admitted = state_->admit(record);
    if (admitted.status != OWH_OK)
        return {admitted, {}};
    const auto created = state_->create_native(record, geometry);
    return {created, created.status == OWH_OK ? ref : core::ResourceRef{}};
}

owh_result OverlayDomain::bind(core::ResourceRef ref, core::LegacySharedTexture texture) noexcept {
    if (!ref.session_id || !ref.resource_id)
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
    auto record = state_->find(ref);
    if (!record)
        return make_result(OWH_STALE_GENERATION, OWH_STAGE_AGENT_LIFETIME);
    if (record->pixels())
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
    if (record->closing.load(std::memory_order_acquire))
        return make_result(OWH_BUSY, OWH_STAGE_CLEANUP);
    return state_->host.execute([state = state_, record, ref, texture] {
        if (record->closing.load(std::memory_order_acquire))
            return make_result(OWH_BUSY, OWH_STAGE_CLEANUP);
        auto native = record->native;
        if (!native)
            return make_result(OWH_AGENT_NOT_READY, OWH_STAGE_AGENT_LIFETIME);
        uint64_t process_bytes = 0;
        const auto session_bytes = state->texture_bytes_in_use(ref.session_id, &process_bytes);
        const auto available =
            std::min(record->policy->shared_texture_budget_bytes -
                         std::min(session_bytes, record->policy->shared_texture_budget_bytes),
                     process_texture_budget - std::min(process_bytes, process_texture_budget));
        const auto status = native->bind(texture, available);
        if (status.status != OWH_OK && native->closing()) {
            record->last = status; // Failed bind retained native resources for retirement.
            record->closing.store(true, std::memory_order_release);
            state->host.wake();
        }
        return status;
    });
}

std::pair<owh_result, owh_overlay_info_data> OverlayDomain::info(core::ResourceRef ref) noexcept {
    auto record = state_->find(ref);
    if (!record)
        return {make_result(OWH_STALE_GENERATION, OWH_STAGE_AGENT_LIFETIME), {}};
    std::pair<owh_result, owh_overlay_info_data> out{};
    const auto dispatched = state_->host.execute([record, ref, &out] {
        auto native = record->native;
        if (!native)
            return make_result(OWH_BUSY, OWH_STAGE_CLEANUP);
        out = native->info();
        out.second.id = ref.resource_id;
        const auto observed_error = out.first.status != OWH_OK ? out.first
                                    : out.second.last_result.status != OWH_OK
                                        ? out.second.last_result
                                        : make_result(OWH_OK);
        if (record->last.status != OWH_OK)
            out.second.last_result = record->last;
        if (record->last.status != OWH_OK || observed_error.status != OWH_OK) {
            out.second.state = OWH_OVERLAY_LOST;
            if (out.second.last_result.status == OWH_OK)
                out.second.last_result = observed_error;
        }
        const bool closing = record->closing.load(std::memory_order_acquire);
        if (closing) {
            out.second.state = OWH_OVERLAY_CLOSING;
            if (out.second.last_result.status == OWH_OK)
                out.second.last_result = record->cleanup;
        }
        return out.first;
    });
    if (out.first.status == 0 && dispatched.status != OWH_OK)
        out.first = dispatched;
    return out;
}

owh_result OverlayDomain::close(core::ResourceRef ref) noexcept {
    auto record = state_->find(ref);
    if (!record)
        return make_result(OWH_STALE_GENERATION, OWH_STAGE_AGENT_LIFETIME);
    record->closing.store(true, std::memory_order_release);
    state_->host.wake();
    return make_result(OWH_OK);
}

owh_overlay_capabilities OverlayDomain::capabilities(core::SessionPolicy policy, uint32_t pid,
                                                     uint64_t started,
                                                     uint64_t session_id) const noexcept {
    owh_overlay_capabilities caps{};
    caps.size = sizeof(caps);
    caps.process_id = pid;
    caps.process_started = started;
    caps.session_id = session_id;
    caps.maximum_overlays = policy.maximum_overlays;
    caps.maximum_width = core::maximum_width;
    caps.maximum_height = core::maximum_height;
    caps.minimum_slots = core::minimum_slots;
    caps.maximum_slots = core::maximum_slots;
    caps.maximum_pixel_buffer_bytes = core::maximum_image_bytes;
    caps.pixel_buffer_budget_bytes = policy.pixel_buffer_budget_bytes;
    caps.shared_texture_budget_bytes = policy.shared_texture_budget_bytes;
    caps.maximum_receipts_per_overlay = policy.maximum_receipts_per_overlay;
    state_->host.fill_capabilities(
        caps, (policy.experimental_features & OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE) != 0);
    return caps;
}

owh_result OverlayDomain::retire(uint64_t session_id) noexcept {
    {
        std::lock_guard lock(state_->records_mutex);
        for (const auto &[_, record] : state_->resources)
            if (record->ref.session_id == session_id)
                record->closing.store(true, std::memory_order_release);
    }
    state_->host.wake();
    std::lock_guard lock(state_->records_mutex);
    for (const auto &[_, r] : state_->resources)
        if (r->ref.session_id == session_id)
            return make_result(OWH_BUSY, OWH_STAGE_CLEANUP);
    return make_result(OWH_OK);
}

uint32_t OverlayDomain::child_count(uint64_t session_id) const noexcept {
    std::lock_guard lock(state_->records_mutex);
    return static_cast<uint32_t>(std::count_if(
        state_->resources.begin(), state_->resources.end(),
        [session_id](const auto &entry) { return entry.second->ref.session_id == session_id; }));
}

} // namespace overwolf_hook::agent
