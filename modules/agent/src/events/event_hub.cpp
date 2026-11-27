#include "event_hub.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <owh/platform/identity.hpp>
#include <thread>

namespace overwolf_hook::agent::events {
namespace {
using Key = std::pair<uint64_t, uint64_t>;
Key key(SubscriptionId id) { return {id.session_id, id.resource_id}; }
constexpr size_t ingress_budget = 1024 * 1024;
struct Item {
    core::CapturedEvent event;
    uint64_t generation = 0;
};
constexpr size_t ingress_capacity = std::min<size_t>(16, ingress_budget / sizeof(Item));
static_assert(ingress_capacity && ingress_capacity * sizeof(Item) <= ingress_budget);
struct Ingress {
    std::mutex mutex;
    std::condition_variable ready;
    std::unique_ptr<Item[]> items;
    size_t read = 0, count = 0;
    uint64_t generation = 0;
    bool active = false, stopping = false;
    std::atomic<uint64_t> dropped{0};
    void capture(const core::CapturedEvent &event) noexcept {
        std::unique_lock lock(mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            ++dropped;
            return;
        }
        if (!active)
            return;
        if (count == ingress_capacity) {
            ++dropped;
            return;
        }
        auto &item = items[(read + count) % ingress_capacity];
        item.event = event;
        item.generation = generation;
        ++count;
        lock.unlock();
        ready.notify_one();
    }
    bool pop(Item &item) {
        std::unique_lock lock(mutex);
        ready.wait(lock, [&] { return count || stopping; });
        if (stopping)
            return false;
        item = items[read];
        read = (read + 1) % ingress_capacity;
        --count;
        return true;
    }
    void activate(bool enabled) {
        std::lock_guard lock(mutex);
        active = enabled;
        ++generation;
        read = count = 0;
    }
};
} // namespace
struct EventHub::Impl {
    struct Subscription {
        Handle wake{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
        Handle closed{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        uint64_t qpc_frequency = 0, started_qpc = 0, started_utc_filetime = 0;
    };
    overwolf::EventSource &source;
    Ingress ingress;
    mutable std::mutex mutex;
    std::map<Key, Subscription> subscriptions;
    std::unique_ptr<ipc::EventLog> log;
    std::thread dispatcher;
    // Written only under mutex. Ingress and publication loss form one global
    // cumulative count, stamped at publication rather than per subscriber.
    uint64_t publication_drops = 0;
    explicit Impl(overwolf::EventSource &value) : source(value) {}
    static void captured(void *context, const core::CapturedEvent &event) noexcept {
        static_cast<Impl *>(context)->ingress.capture(event);
    }
    ~Impl() {
        source.shutdown();
        {
            std::lock_guard lock(ingress.mutex);
            ingress.stopping = true;
        }
        ingress.ready.notify_one();
        if (dispatcher.joinable())
            dispatcher.join();
        for (auto &[_, sub] : subscriptions)
            SetEvent(sub.closed.get());
    }
    uint64_t losses() const noexcept { return ingress.dropped.load() + publication_drops; }
    void run() noexcept {
        Item item;
        while (ingress.pop(item)) {
            std::lock_guard lock(mutex);
            if (subscriptions.empty() || item.generation != ingress.generation)
                continue;
            const auto status = log->publish(item.event, losses());
            if (status.status == OWH_OK) {
                for (auto &[_, sub] : subscriptions)
                    SetEvent(sub.wake.get());
            } else if (status.status == OWH_BUSY) {
                ++publication_drops;
            } else {
                // A corrupt/terminal publication failure ends this stream. It
                // cannot be relabeled as temporary pressure or silently retried.
                for (auto &[_, sub] : subscriptions)
                    SetEvent(sub.closed.get());
                source.set_enabled(false);
                ingress.activate(false);
                publication_error = status;
            }
        }
    }
    owh_result publication_error = ok;
    void deactivate_if_empty() {
        if (!subscriptions.empty())
            return;
        source.set_enabled(false); // drains callback exports, never removes hook
        ingress.activate(false);
    }
};
EventHub::EventHub(overwolf::EventSource &source) : impl_(std::make_unique<Impl>(source)) {}
EventHub::~EventHub() = default;
owh_result EventHub::capability() const noexcept { return impl_->source.capability(); }
std::pair<owh_result, SubscriptionStream> EventHub::subscribe(SubscriptionId id) noexcept {
    SubscriptionStream output;
    auto status = boundary([&] {
        if (!id.session_id || !id.resource_id)
            return make_result(OWH_INVALID_ARGUMENT);
        std::lock_guard lock(impl_->mutex);
        if (impl_->publication_error.status != OWH_OK)
            return impl_->publication_error;
        if (impl_->subscriptions.contains(key(id)))
            return make_result(OWH_ALREADY_EXISTS);
        if (impl_->subscriptions.size() >= 64)
            return make_result(OWH_RESOURCE_EXHAUSTED);
        if (!impl_->log) {
            const auto name = "ow_events_" + std::to_string(platform::random_id());
            impl_->log = ipc::EventLog::create(name.c_str());
        }
        if (!impl_->ingress.items)
            impl_->ingress.items = std::make_unique<Item[]>(ingress_capacity);
        if (!impl_->dispatcher.joinable())
            impl_->dispatcher = std::thread([self = impl_.get()] { self->run(); });
        Impl::Subscription sub;
        if (!sub.wake || !sub.closed)
            return make_result(OWH_RESOURCE_EXHAUSTED);
        LARGE_INTEGER frequency{}, now{};
        FILETIME utc{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&now);
        GetSystemTimePreciseAsFileTime(&utc);
        sub.qpc_frequency = frequency.QuadPart;
        sub.started_qpc = now.QuadPart;
        sub.started_utc_filetime = (uint64_t{utc.dwHighDateTime} << 32) | utc.dwLowDateTime;
        output.channel = impl_->log->name;
        output.position = impl_->log->position();
        output.position.capture_loss = impl_->losses();
        output.wake_handle = reinterpret_cast<uint64_t>(sub.wake.get());
        output.closed_handle = reinterpret_cast<uint64_t>(sub.closed.get());
        const bool first = impl_->subscriptions.empty();
        impl_->subscriptions.emplace(key(id), std::move(sub));
        if (first) {
            const auto started = impl_->source.prepare(&Impl::captured, impl_.get());
            if (started.status != OWH_OK) {
                impl_->subscriptions.erase(key(id));
                return started;
            }
            impl_->ingress.activate(true);
            impl_->source.set_enabled(true);
        }
        return ok;
    });
    return {status, status.status == OWH_OK ? std::move(output) : SubscriptionStream{}};
}
owh_result EventHub::query(SubscriptionId id, owh_subscription_info &out) noexcept {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->subscriptions.find(key(id));
    if (it == impl_->subscriptions.end())
        return make_result(OWH_NOT_FOUND);
    const auto &sub = it->second;
    const auto metrics = impl_->source.metrics();
    out = {};
    out.size = sizeof(out);
    out.state = impl_->publication_error.status == OWH_OK ? OWH_SUBSCRIPTION_ACTIVE
                                                          : OWH_SUBSCRIPTION_FAILED;
    out.hook_enabled = metrics.enabled;
    out.warnings = metrics.warnings;
    out.qpc_frequency = sub.qpc_frequency;
    out.started_qpc = sub.started_qpc;
    out.started_utc_filetime = sub.started_utc_filetime;
    out.last_result = impl_->publication_error;
    return ok;
}
owh_result EventHub::close(SubscriptionId id) noexcept {
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->subscriptions.find(key(id));
    if (it == impl_->subscriptions.end())
        return ok;
    SetEvent(it->second.closed.get());
    impl_->subscriptions.erase(it);
    impl_->deactivate_if_empty();
    return ok;
}
bool EventHub::has_children(uint64_t session) const noexcept {
    std::lock_guard lock(impl_->mutex);
    return std::any_of(impl_->subscriptions.begin(), impl_->subscriptions.end(),
                       [session](const auto &entry) { return entry.first.first == session; });
}
owh_result EventHub::cleanup_owner(uint64_t session) noexcept {
    std::lock_guard lock(impl_->mutex);
    for (auto it = impl_->subscriptions.begin(); it != impl_->subscriptions.end();) {
        if (it->first.first == session) {
            SetEvent(it->second.closed.get());
            it = impl_->subscriptions.erase(it);
        } else
            ++it;
    }
    impl_->deactivate_if_empty();
    return ok;
}
} // namespace overwolf_hook::agent::events
