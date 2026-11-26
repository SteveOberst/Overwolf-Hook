#include "overlay.hpp"
#include <algorithm>
#include <owh/core/pixel_region.hpp>
#include <owh/platform/deadline.hpp>
#include <owh/platform/result.hpp>
#include <owh/platform/wait.hpp>

namespace overwolf_hook::client {
namespace {
constexpr uint64_t process_budget = core::process_budget_bytes;
struct ProcessUsage {
    std::mutex mutex;
    uint64_t bytes = 0;
};
ProcessUsage usage;

/** The reservation follows the allocation, including detached write borrows.
 * The process ceiling cannot be bypassed by opening another SDK context. */
struct MappingReservation {
    std::shared_ptr<AttachedSession> connection;
    uint64_t bytes = 0;
    ~MappingReservation() {
        if (bytes) {
            connection->release_mapping(bytes);
            std::lock_guard lock(usage.mutex);
            usage.bytes -= bytes;
        }
    }
    static std::pair<owh_result, std::unique_ptr<MappingReservation>>
    acquire(std::shared_ptr<AttachedSession> connection, uint64_t bytes) {
        auto reservation = std::make_unique<MappingReservation>();
        std::lock_guard lock(usage.mutex);
        if (bytes > process_budget || usage.bytes > process_budget - bytes ||
            !connection->reserve_mapping(bytes))
            return {make_result(OWH_RESOURCE_EXHAUSTED), nullptr};
        usage.bytes += bytes;
        reservation->connection = std::move(connection);
        reservation->bytes = bytes;
        return {make_result(OWH_OK), std::move(reservation)};
    }
};
} // namespace
struct CompletionState {
    std::mutex mutex;
    owh_submission_info info{sizeof(owh_submission_info), OWH_SUBMISSION_PENDING,
                             make_result(OWH_BUSY)};
    std::shared_ptr<OverlayBacking> backing;
    uint64_t sequence = 0;
};
struct OverlayBacking {
    std::shared_ptr<AttachedSession> connection;
    core::ResourceRef ref{};
    owh_overlay_spec spec{};
    uint32_t kind = 0;
    std::unique_ptr<MappingReservation> reservation;
    std::unique_ptr<ipc::PixelProducer> pixels;
    std::mutex mutex;
    std::vector<std::weak_ptr<CompletionState>> receipts;
    bool closed = false;

    static void settle(CompletionState &receipt, uint32_t state, owh_result result) noexcept {
        std::lock_guard lock(receipt.mutex);
        if (receipt.info.state != OWH_SUBMISSION_PENDING)
            return;
        receipt.info.state = state;
        receipt.info.result = result;
        receipt.backing.reset();
    }
    void complete(const ipc::Completion &done) noexcept {
        for (const auto &weak : receipts)
            if (auto receipt = weak.lock(); receipt && receipt->sequence == done.sequence)
                settle(*receipt,
                       done.published                         ? OWH_SUBMISSION_PUBLISHED
                       : done.result.status == OWH_REPLACED   ? OWH_SUBMISSION_REPLACED
                       : done.result.status == OWH_AGENT_LOST ? OWH_SUBMISSION_LOST
                                                             : OWH_SUBMISSION_REJECTED,
                       done.result);
    }
    // Caller pins this backing and holds mutex. Copy outcomes before slots are
    // returned to writers, so receipt history never depends on a reused slot.
    void drain() noexcept {
        std::erase_if(receipts, [](const auto &weak) { return weak.expired(); });
        if (!pixels)
            return;
        uint32_t count = 0;
        const auto done = pixels->collect_completed(count);
        for (uint32_t i = 0; i < count; ++i)
            complete(done[i]);
        const auto health = pixels->host_status();
        if (!connection->alive() ||
            (health.status != OWH_OK && health.status != OWH_CLOSED && health.status != OWH_BUSY))
            for (const auto &weak : receipts)
                if (auto receipt = weak.lock())
                    settle(*receipt, OWH_SUBMISSION_LOST, make_result(OWH_AGENT_LOST));
    }
};

std::pair<owh_result, std::unique_ptr<Overlay>>
Overlay::create(std::shared_ptr<AttachedSession> connection, const owh_overlay_spec &spec,
                const owh_pixel_buffer_overlay_options *input, uint32_t kind) {
    const auto queue =
        core::resolve_queue(input ? input->slot_count : 0, input ? input->policy : 0);
    if (spec.size != sizeof(spec) || !core::valid_geometry(spec) ||
        (input && input->size != sizeof(*input)) || !core::valid_queue(queue))
        return {make_result(OWH_INVALID_ARGUMENT), nullptr};
    if (!connection->alive())
        return {make_result(OWH_AGENT_LOST), nullptr};
    if (kind == OWH_OVERLAY_SHARED_TEXTURE &&
        !(connection->policy().experimental_features & OWH_EXPERIMENTAL_D3D11_SHARED_TEXTURE))
        return {make_result(OWH_OPT_IN_REQUIRED), nullptr};
    auto overlay = std::make_unique<Overlay>();
    auto backing = std::make_shared<OverlayBacking>();
    backing->connection = std::move(connection);
    backing->spec = spec;
    backing->kind = kind;
    const auto bytes = kind == OWH_OVERLAY_PIXEL_BUFFER
        ? ipc::pixel_allocation_size(spec.width, spec.height, queue.slot_count, queue.policy) : 0;
    if (kind == OWH_OVERLAY_PIXEL_BUFFER) {
        if (!bytes) return {make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION), nullptr};
        auto [status, reservation] = MappingReservation::acquire(backing->connection, bytes);
        if (status.status != OWH_OK) return {status, nullptr};
        backing->reservation = std::move(reservation);
    }
    core::CreateSourceResponse reply{};
    if (kind == OWH_OVERLAY_PIXEL_BUFFER) {
        reply = backing->connection->create<core::CreatePixelBufferSource>([&](core::ResourceRef ref) {
            backing->ref = ref;
            auto [result, pixels] = ipc::PixelProducer::create(
                ref, backing->connection->process_id(), backing->connection->process_started(),
                spec.width, spec.height, queue.slot_count, queue.policy, bytes);
            if (result.status != OWH_OK) throw Failure{result};
            backing->pixels = std::move(pixels);
            return core::CreatePixelBufferSourceRequest{ref, spec, backing->pixels->descriptor()};
        });
    } else {
        reply = backing->connection->create<core::CreateSharedTextureSource>([&](core::ResourceRef ref) {
            backing->ref = ref; return core::CreateSharedTextureSourceRequest{ref, spec};
        });
    }
    if (reply.result.status != OWH_OK)
        return {reply.result, nullptr};
    if (reply.ref != backing->ref) {
        backing->connection->invalidate();
        return {make_result(OWH_CORRUPT), nullptr};
    }
    overlay->backing_ = std::move(backing);
    return {make_result(OWH_OK), std::move(overlay)};
}
Overlay::~Overlay() {
    if (backing_)
        (void)close();
}
owh_result Overlay::close() noexcept {
    auto backing = std::move(backing_);
    if (!backing)
        return make_result(OWH_OK);
    {
        std::lock_guard lock(backing->mutex);
        backing->closed = true;
    }
    const auto result = backing->connection->overlays().close({backing->ref}).result;
    if (result.status != OWH_OK)
        backing->connection->invalidate();
    {
        std::lock_guard lock(backing->mutex);
        backing->drain();
    }
    return result;
}
owh_result Overlay::info(owh_overlay_info_data &output) noexcept {
    if (!backing_)
        return make_result(OWH_CLOSED);
    const auto &backing = *backing_;
    const auto reply = backing.connection->overlays().get_info({backing.ref});
    if (reply.result.status != OWH_OK)
        return reply.result;
    const auto &info = reply.info;
    if (info.size != sizeof(info) || info.id != backing.ref.resource_id ||
        info.kind != backing.kind || info.width != backing.spec.width ||
        info.height != backing.spec.height || info.reserved ||
        info.state < OWH_OVERLAY_AWAITING_TEXTURE || info.state > OWH_OVERLAY_CLOSING) {
        backing.connection->invalidate();
        return make_result(OWH_CORRUPT);
    }
    output = info;
    return make_result(OWH_OK);
}
owh_result Overlay::bind(const owh_legacy_shared_texture &texture) noexcept {
    if (!backing_)
        return make_result(OWH_CLOSED);
    if (texture.size != sizeof(texture) || texture.reserved || !texture.legacy_shared_token)
        return make_result(OWH_INVALID_ARGUMENT);
    if (backing_->kind != OWH_OVERLAY_SHARED_TEXTURE)
        return make_result(OWH_UNSUPPORTED);
    return backing_->connection->overlays().bind_texture({backing_->ref, texture}).result;
}
std::pair<owh_result, std::unique_ptr<PixelFrame>> Overlay::acquire() {
    if (!backing_)
        return {make_result(OWH_CLOSED), nullptr};
    auto backing = backing_;
    std::lock_guard lock(backing->mutex);
    if (backing->closed)
        return {make_result(OWH_CLOSED), nullptr};
    if (!backing->pixels)
        return {make_result(OWH_UNSUPPORTED), nullptr};
    backing->drain();
    if (!backing->connection->alive())
        return {make_result(OWH_AGENT_LOST), nullptr};
    auto frame = std::make_unique<PixelFrame>();
    const auto acquired = backing->pixels->try_acquire();
    if (acquired.result.status != OWH_OK)
        return {acquired.result, nullptr};
    frame->backing_ = std::move(backing);
    frame->writable_ = acquired.frame;
    frame->damage_ = core::full_region(frame->backing_->spec.width, frame->backing_->spec.height);
    return {make_result(OWH_OK), std::move(frame)};
}
std::pair<owh_result, std::unique_ptr<PixelFrame>> Overlay::prepare(
    uint64_t source_bytes, uint32_t stride, uint32_t timeout, HANDLE cancellation, bool immediate,
    const owh_pixel_region *damage) {
    if (!backing_)
        return {make_result(OWH_CLOSED), nullptr};
    auto backing = backing_;
    const auto region = damage ? *damage : core::full_region(backing->spec.width, backing->spec.height);
    if (!core::valid_region(region, backing->spec.width, backing->spec.height))
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), nullptr};
    const uint64_t row_bytes = uint64_t(backing->spec.width) * 4;
    if (!stride)
        stride = static_cast<uint32_t>(row_bytes);
    if (stride < row_bytes || source_bytes < (uint64_t(backing->spec.height) - 1) * stride + row_bytes)
        return {make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT), nullptr};
    auto frame = std::make_unique<PixelFrame>();
    const Deadline deadline(timeout);
    for (;;) {
        if (cancellation && WaitForSingleObject(cancellation, 0) == WAIT_OBJECT_0)
            return {make_result(OWH_CANCELLED), nullptr};
        {
            std::lock_guard lock(backing->mutex);
            if (backing->closed)
                return {make_result(OWH_CLOSED), nullptr};
            if (!backing->pixels)
                return {make_result(OWH_UNSUPPORTED), nullptr};
            if (!backing->connection->alive())
                return {make_result(OWH_AGENT_LOST), nullptr};
            backing->drain();
            const auto acquired = backing->pixels->prepare_submit(!immediate);
            if (acquired.result.status == OWH_OK) {
                if (acquired.displaced)
                    backing->complete(*acquired.displaced);
                frame->backing_ = backing;
                frame->writable_ = acquired.frame;
                frame->damage_ = region;
                frame->managed_ = !immediate;
                return {make_result(OWH_OK), std::move(frame)};
            }
            if (acquired.result.status != OWH_BUSY || immediate)
                return {acquired.result, nullptr};
        }
        if (deadline.expired())
            return {make_result(OWH_TIMEOUT), nullptr};
        HANDLE handles[]{backing->pixels->completion_event(), backing->connection->process(), cancellation};
        // READY replacement has no completion signal to wait for; a short retry
        // also covers metadata contention. FIFO wakes immediately on completion.
        const auto waited = wait_any(handles, cancellation ? 3 : 2,
                                     std::min<DWORD>(10, deadline.remaining_ms()));
        if (waited.kind == WaitKind::failed)
            return {make_result(OWH_SYSTEM_ERROR, OWH_STAGE_WAIT, OWH_ERROR_WIN32, waited.error), nullptr};
    }
}
PixelFrame::~PixelFrame() {
    if (backing_ && !consumed_) {
        std::lock_guard lock(backing_->mutex);
        (void)backing_->pixels->abort(writable_);
    }
}
owh_pixel_buffer PixelFrame::view() const noexcept {
    return {sizeof(owh_pixel_buffer), OWH_PIXEL_BGRA8_UNORM,
            writable_.pixels.data(),  writable_.pixels.size(),
            backing_->spec.width,     backing_->spec.height,
            backing_->spec.width * 4, 0};
}
std::pair<owh_result, std::unique_ptr<Submission>> PixelFrame::commit(bool tracked) {
    auto backing = backing_;
    std::lock_guard lock(backing->mutex);
    if (consumed_)
        return {make_result(OWH_CLOSED), nullptr};
    // The frame object is consumed by its ABI owner even when validation fails.
    backing->drain();
    if (backing->closed)
        return {make_result(OWH_CLOSED), nullptr};
    if (!backing->connection->alive())
        return {make_result(OWH_AGENT_LOST), nullptr};
    std::unique_ptr<Submission> receipt;
    if (tracked) {
        if (backing->receipts.size() >= backing->connection->policy().maximum_receipts_per_overlay)
            return {make_result(OWH_RESOURCE_EXHAUSTED), nullptr};
        receipt = std::make_unique<Submission>();
        receipt->state_ = std::make_shared<CompletionState>();
        backing->receipts.reserve(backing->receipts.size() + 1);
    }
    auto committed = backing->pixels->commit(writable_, &damage_);
    if (managed_ && committed.result.status == OWH_BUSY)
        committed.result.status = OWH_TIMEOUT;
    // Even when wake signalling fails, a sequence proves ownership handoff.
    consumed_ = committed.sequence != 0;
    if (committed.result.status != OWH_OK)
        return {committed.result, nullptr};
    if (receipt) {
        receipt->state_->sequence = committed.sequence;
        receipt->state_->backing = backing;
        backing->receipts.emplace_back(receipt->state_);
    }
    return {make_result(OWH_OK), std::move(receipt)};
}
owh_result PixelFrame::set_damage(const owh_pixel_region *damage) noexcept {
    const auto region = damage ? *damage : core::full_region(backing_->spec.width, backing_->spec.height);
    if (!core::valid_region(region, backing_->spec.width, backing_->spec.height))
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
    damage_ = region;
    return make_result(OWH_OK);
}
owh_result Submission::poll(owh_submission_info &output) noexcept {
    std::shared_ptr<OverlayBacking> backing;
    {
        std::lock_guard lock(state_->mutex);
        backing = state_->backing;
    }
    if (backing) {
        std::lock_guard lock(backing->mutex);
        backing->drain();
    }
    {
        std::lock_guard lock(state_->mutex);
        output = state_->info;
    }
    return make_result(OWH_OK);
}
owh_result Submission::wait(uint32_t timeout, HANDLE cancellation,
                            owh_submission_info &output) noexcept {
    const Deadline deadline(timeout);
    for (;;) {
        if (cancellation && WaitForSingleObject(cancellation, 0) == WAIT_OBJECT_0)
            return make_result(OWH_CANCELLED);
        (void)poll(output);
        if (output.state != OWH_SUBMISSION_PENDING)
            return make_result(OWH_OK);
        std::shared_ptr<OverlayBacking> backing;
        {
            std::lock_guard lock(state_->mutex);
            backing = state_->backing;
        }
        if (!backing)
            continue;
        if (deadline.expired())
            return make_result(OWH_TIMEOUT);
        HANDLE handles[]{backing->pixels->completion_event(), backing->connection->process(),
                         cancellation};
        const auto waited =
            wait_any(handles, cancellation ? 3 : 2, std::min<DWORD>(100, deadline.remaining_ms()));
        if (waited.kind == WaitKind::failed)
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_WAIT, OWH_ERROR_WIN32, waited.error);
    }
}
} // namespace overwolf_hook::client
