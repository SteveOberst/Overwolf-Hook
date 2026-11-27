#pragma once
#include "events/subscription.hpp"
#include "overlays/overlay.hpp"
#include "sdk.hpp"
#include <overwolf_hook/abi/sdk.h>
#include <overwolf_hook/types/discovery.h>
#include <overwolf_hook/types/state.h>
#include <owh/overwolf/state_storage.hpp>
#include <owh/platform/result.hpp>

// Only the ABI adapters know these handle layouts. Domain logic never calls C
// exports or uses C handles as its internal object model.
struct owh_sdk {
    overwolf_hook::client::Sdk value;
};
struct owh_overlay {
    std::unique_ptr<overwolf_hook::client::Overlay> value;
};
struct owh_frame {
    std::unique_ptr<overwolf_hook::client::PixelFrame> value;
};
struct owh_submission {
    std::unique_ptr<overwolf_hook::client::Submission> value;
};
struct owh_cancel {
    overwolf_hook::Handle event;
};
struct owh_subscription {
    std::unique_ptr<overwolf_hook::client::EventSubscription> value;
};
struct owh_state {
    overwolf_hook::overwolf::StateSnapshot value;
};
struct owh_channels {
    std::vector<owh_source_channel> value;
};
namespace overwolf_hook::client::abi {
inline owh_result invalid() noexcept {
    return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
}
template <class Handle, class Value>
owh_result adopt(owh_result result, std::unique_ptr<Value> value, Handle **output) {
    if (result.status != OWH_OK)
        return result;
    auto handle = std::make_unique<Handle>();
    handle->value = std::move(value);
    *output = handle.release();
    return result;
}
} // namespace overwolf_hook::client::abi
