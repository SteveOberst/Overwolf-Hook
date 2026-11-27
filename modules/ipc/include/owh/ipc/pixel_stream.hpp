#pragma once
#include <windows.h>
#include <array>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <memory>
#include <optional>
#include <overwolf_hook/types/overlays.h>
#include <owh/core/types.hpp>
#include <owh/core/pixel_region.hpp>
#include <span>
namespace overwolf_hook::ipc {
using core::ResourceRef;
inline constexpr uint32_t protocol_version = 2, maximum_slots = core::maximum_slots,
                          header_page_bytes = 4096, slot_table_offset = 512;
inline constexpr uint64_t maximum_image_bytes = core::maximum_image_bytes,
                          maximum_mapping_bytes = core::maximum_mapping_bytes;
enum class SlotState : LONG {
    free = 0,
    writing = 1,
    ready = 2,
    reading = 3,
    done = 4,
    rejected = 5
};
struct alignas(8) Header {
    uint32_t magic;
    uint16_t protocol, header_bytes;
    uint64_t mapping_bytes;
    ResourceRef ref;
    uint32_t producer_pid, consumer_pid;
    uint64_t producer_started, consumer_started;
    uint32_t width, height, stride, pixel_format, slot_count, queue_policy;
    uint64_t pixels_offset, slot_pixel_bytes;
    uint64_t lease_counter = 0, sequence_counter = 0;
    volatile LONG state = 0;
    // Terminal publication: the producer sets this only when its last owner is
    // destroyed, after no producer API or pixel borrow can still write. It lets
    // the consumer safely settle an abandoned writing lease during close.
    volatile LONG producer_retired = 0;
    boost::interprocess::interprocess_mutex mutex;
    // Committed changes not yet covered by a successful latest publication.
    // Protected by mutex; starts full so the first image establishes a baseline.
    core::PixelRegion pending_damage{};
    std::byte reserved[376 - sizeof(boost::interprocess::interprocess_mutex)]{};
};
enum : LONG { generation_active = 1, generation_lost = 2, generation_closed = 3 };
struct alignas(8) SlotDescriptor {
    volatile LONG state;
    uint32_t reserved0;
    uint64_t resource_id, sequence, lease_id, pixel_offset, pixel_bytes;
    uint32_t result_status, error_domain, failure_stage, reserved1;
    uint64_t native_code;
    std::byte reserved[56];
};
/** Producer-local handle values. Receiver duplicates its own references from the
 * authenticated producer process; sender never creates unknown remote handles. */
using QueueDescriptor = core::PixelBufferRef;
struct Completion {
    uint64_t sequence;
    owh_result result;
    bool published, resource_released;
};
struct Commit {
    owh_result result;
    uint64_t sequence;
};
/** Exact validated mapping size; zero means invalid geometry or queue. */
uint64_t pixel_allocation_size(uint32_t width, uint32_t height, uint32_t slots,
                               uint32_t policy) noexcept;
class PixelRead;
class PixelProducer final {
  public:
    static std::pair<owh_result, std::unique_ptr<PixelProducer>>
    create(ResourceRef, uint32_t consumer_pid, uint64_t consumer_started, uint32_t width,
           uint32_t height, uint32_t slots, uint32_t policy, uint64_t budget);
    ~PixelProducer();
    PixelProducer(const PixelProducer &) = delete;
    PixelProducer &operator=(const PixelProducer &) = delete;
    struct Writable {
        uint32_t slot;
        uint64_t lease_id;
        std::span<std::byte> pixels;
    };
    struct Acquired {
        owh_result result;
        Writable frame;
        std::optional<Completion> displaced{};
    };
    Acquired try_acquire();
    /** Managed admission: prefer replacing READY for latest, wait externally
     * for FIFO. A consumer's READING allocation is never modified. Outstanding
     * direct WRITING borrows cannot be mixed with managed submission. */
    Acquired prepare_submit(bool replace_ready = true);
    /** On BUSY, commit leaves the writable lease in the shared slot so this
     * token remains retryable. abort retains a local deferred release. */
    Commit commit(const Writable &, const core::PixelRegion *damage = nullptr);
    owh_result abort(const Writable &);
    owh_result host_status() const noexcept;
    std::array<Completion, maximum_slots> collect_completed(uint32_t &);
    QueueDescriptor descriptor() const noexcept;
    HANDLE completion_event() const noexcept;
    const Header &header() const noexcept;
    void mark_closed() noexcept;

  private:
    Acquired acquire(bool managed, bool replace_ready);
    friend struct WireTestAccess;
    BOOL(WINAPI *signal_wake_)(HANDLE) = &SetEvent;
    PixelProducer() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class PixelConsumer final {
  public:
    static std::pair<owh_result, std::unique_ptr<PixelConsumer>>
    import(const QueueDescriptor &, HANDLE verified_producer, ResourceRef expected, uint32_t width,
           uint32_t height, uint32_t slots, uint32_t policy, uint64_t budget);
    ~PixelConsumer();
    PixelConsumer(const PixelConsumer &) = delete;
    PixelConsumer &operator=(const PixelConsumer &) = delete;
    std::pair<owh_result, std::unique_ptr<PixelRead>> try_take_ready();
    bool has_ready() const noexcept;
    /** Nonblocking actor maintenance, including after terminal publication.
     * A readiness query never completes deferred writes into shared memory. */
    void flush_completions() noexcept;
    owh_result settle_for_close();
    void reject_ready(owh_result) noexcept;
    void mark_lost(owh_result) noexcept;
    HANDLE wake_event() const noexcept;
    uint64_t mapping_bytes() const noexcept;
    const Header &header() const noexcept;

  private:
    PixelConsumer() = default;
    struct Impl;
    std::shared_ptr<Impl> impl_;
    friend class PixelRead;
};
class PixelRead final {
  public:
    ~PixelRead();
    PixelRead(const PixelRead &) = delete;
    PixelRead &operator=(const PixelRead &) = delete;
    uint64_t sequence() const noexcept;
    uint32_t width() const noexcept;
    uint32_t height() const noexcept;
    uint32_t stride() const noexcept;
    std::span<const std::byte> pixels() const noexcept;
    core::PixelRegion damage() const noexcept { return damage_; }
    /** Contended completion is retained by the consumer and retried before
     * the stream can accept/reuse another lease. */
    void complete(owh_result) noexcept;

  private:
    friend class PixelConsumer;
    PixelRead(std::shared_ptr<PixelConsumer::Impl>, SlotDescriptor *, uint64_t);
    std::shared_ptr<PixelConsumer::Impl> backing_;
    SlotDescriptor *slot_;
    uint64_t sequence_;
    std::span<const std::byte> pixels_;
    core::PixelRegion damage_{};
    bool done_ = false;
};
static_assert(sizeof(Header) == 512);
static_assert(sizeof(SlotDescriptor) == 128);
} // namespace overwolf_hook::ipc
