#include "subscription.hpp"
#include "session/attached_session.hpp"
#include <algorithm>
#include <owh/platform/result.hpp>

namespace overwolf_hook::client {
EventSubscription::~EventSubscription() { (void)close(); }
std::pair<owh_result, std::unique_ptr<EventSubscription>> EventSubscription::create(
    std::shared_ptr<AttachedSession> connection, const owh_event_options *options) noexcept {
    std::unique_ptr<EventSubscription> subscription;
    const auto status = boundary([&] {
        if (!connection || (options && options->size != sizeof(*options)))
            return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
        subscription = std::make_unique<EventSubscription>();
        const auto filter = options ? options->source_channel : OWH_EVENTS_ALL_CHANNELS;
        core::ResourceRef ref{};
        const auto reply = connection->create<core::SubscribeEvents>([&](core::ResourceRef id) {
            ref = id; return core::SubscribeEventsRequest{id, filter, 0};
        });
        if (reply.result.status != OWH_OK) return reply.result;
        subscription->connection_ = std::move(connection); subscription->ref_ = ref;
        const auto end = std::find(std::begin(reply.channel), std::end(reply.channel), '\0');
        if (reply.ref != ref || end == std::end(reply.channel) || end == std::begin(reply.channel)) {
            subscription->connection_->invalidate();
            return make_result(OWH_CORRUPT, OWH_STAGE_CONTROL_IO);
        }
        auto [opened, cursor] = ipc::EventCursor::open(reply.channel,
            {reply.start_sequence, reply.capture_loss}, filter, subscription->connection_->process(),
            reply.wake_handle, reply.closed_handle, options ? options->producer_process_id : 0);
        subscription->cursor_ = std::move(cursor);
        return opened;
    });
    if (status.status != OWH_OK) return {status, nullptr};
    return {status, std::move(subscription)};
}
owh_result EventSubscription::statistics(owh_subscription_info &output) noexcept {
    if (!connection_) return make_result(OWH_CLOSED);
    const auto reply = connection_->events().statistics({ref_});
    if (reply.result.status != OWH_OK) return reply.result;
    if (reply.info.size != sizeof(reply.info) ||
        reply.info.state < OWH_SUBSCRIPTION_STARTING || reply.info.state > OWH_SUBSCRIPTION_FAILED ||
        reply.info.hook_enabled > 1 || !valid_result(reply.info.last_result)) {
        connection_->invalidate(); return make_result(OWH_CORRUPT, OWH_STAGE_CONTROL_IO);
    }
    output = reply.info; return ok;
}
owh_result EventSubscription::poll(owh_event &out) noexcept {
    if (!cursor_) return make_result(OWH_CLOSED);
    if (!connection_->alive()) return make_result(OWH_AGENT_LOST);
    return cursor_->poll(out);
}
owh_result EventSubscription::receive(uint32_t timeout, HANDLE cancellation, owh_event &out) noexcept {
    if (!cursor_) return make_result(OWH_CLOSED);
    if (!connection_->alive()) return make_result(OWH_AGENT_LOST);
    return cursor_->receive(timeout, cancellation, out);
}
owh_result EventSubscription::close() noexcept {
    cursor_.reset();
    if (!connection_) return ok;
    auto connection = std::move(connection_);
    return connection->events().close({ref_}).result;
}
} // namespace overwolf_hook::client
