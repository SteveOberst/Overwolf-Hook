#include "handles.hpp"
#include <overwolf_hook/abi/events.h>
using namespace overwolf_hook;
using namespace overwolf_hook::client::abi;
extern "C" {
OWH_CLIENT void __cdecl owh_event_options_init(owh_event_options *options) {
    if (options)
        *options = OWH_EVENT_OPTIONS_INIT;
}
OWH_CLIENT owh_result __cdecl owh_event_query_capabilities(owh_sdk *sdk,
                                                           owh_event_capabilities *out) {
    if (!sdk || !out || out->size != sizeof(*out))
        return invalid();
    auto [result, connection] = sdk->value.session();
    return result.status == OWH_OK ? connection->event_capabilities(*out) : result;
}
OWH_CLIENT owh_result __cdecl owh_event_subscribe(owh_sdk *sdk, const owh_event_options *options,
                                                  owh_subscription **out) {
    if (out)
        *out = nullptr;
    if (!sdk || !out)
        return invalid();
    return boundary([&] {
        auto [result, connection] = sdk->value.session();
        if (result.status != OWH_OK)
            return result;
        auto [created, value] = client::EventSubscription::create(std::move(connection), options);
        return adopt(created, std::move(value), out);
    });
}
OWH_CLIENT owh_result __cdecl owh_subscription_query(owh_subscription *sub,
                                                     owh_subscription_info *out) {
    return sub && out && out->size == sizeof(*out) ? sub->value->statistics(*out) : invalid();
}
OWH_CLIENT owh_result __cdecl owh_subscription_close(owh_subscription *raw) {
    std::unique_ptr<owh_subscription> sub(raw);
    return sub ? sub->value->close() : invalid();
}
OWH_CLIENT owh_result __cdecl owh_subscription_poll(owh_subscription *sub, owh_event *out) {
    return sub && out ? sub->value->poll(*out) : invalid();
}
OWH_CLIENT owh_result __cdecl owh_subscription_receive(owh_subscription *sub,
    const owh_wait_options *options, owh_event *out) {
    if (!sub || !out || (options && options->size != sizeof(*options))) return invalid();
    return sub->value->receive(options ? options->timeout_ms : OWH_WAIT_INFINITE,
        options && options->cancellation ? options->cancellation->event.get() : nullptr, *out);
}
}
