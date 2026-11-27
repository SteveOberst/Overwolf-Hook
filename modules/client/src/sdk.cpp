#include "sdk.hpp"
#include <owh/platform/result.hpp>
namespace overwolf_hook::client {
owh_result Sdk::attach(const owh_client_options *options) noexcept {
    try {
        if (connection_)
            return make_result(OWH_ALREADY_EXISTS);
        auto [result, value] = AttachedSession::attach(options);
        if (result.status == OWH_OK)
            connection_ = std::move(value);
        return result;
    } catch (...) {
        return make_result(OWH_SYSTEM_ERROR);
    }
}
owh_result Sdk::detach() noexcept {
    auto previous = std::move(connection_);
    return previous ? previous->close() : make_result(OWH_OK);
}
std::pair<owh_result, std::shared_ptr<AttachedSession>> Sdk::session() const noexcept {
    if (!connection_)
        return {make_result(OWH_AGENT_NOT_READY), {}};
    if (!connection_->alive())
        return {make_result(OWH_AGENT_LOST), {}};
    return {make_result(OWH_OK), connection_};
}
} // namespace overwolf_hook::client
