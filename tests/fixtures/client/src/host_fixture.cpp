#include "abi/handles.hpp"
#include "session/attached_session.hpp"
#include "sdk.hpp"
#include <owh/ipc/session_protocol.hpp>
#include <owh/testing/client_fixture.hpp>

#include <cstring>
#include <new>

namespace overwolf_hook::client::test_fixture {
struct Access {
    static owh_result connect_sdk(std::unique_ptr<ipc::MessageConnection> transport,
                                  uint32_t server_pid, uint64_t server_started,
                                  const core::HelloRequest &hello, owh_sdk **output) noexcept {
        if (output)
            *output = nullptr;
        if (!output || !transport || !server_pid || !server_started)
            return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
        try {
            auto connection = std::shared_ptr<AttachedSession>(new AttachedSession());
            connection->transport_ = std::move(transport);
            connection->pid_ = server_pid;
            connection->started_ = server_started;
            connection->agent_info_.size = sizeof(connection->agent_info_);
            connection->agent_info_.abi_version = OWH_ABI_VERSION;
            connection->agent_info_.wire_version = ipc::protocol;
            connection->agent_info_.server_state = OWH_AGENT_READY;
            connection->agent_info_.process_id = server_pid;
            connection->agent_info_.process_started = server_started;
            connection->agent_info_.build_info = hello.build_info;
            const auto greeted = connection->handshake(hello);
            if (greeted.status != OWH_OK)
                return greeted;

            auto sdk = std::unique_ptr<owh_sdk>(new (std::nothrow) owh_sdk());
            if (!sdk)
                return make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION);
            sdk->value.connection_ = std::move(connection);
            *output = sdk.release();
            return make_result(OWH_OK);
        } catch (...) {
            return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_HANDSHAKE);
        }
    }
};

owh_result connect_sdk(std::unique_ptr<ipc::MessageConnection> connection, uint32_t server_pid,
                       uint64_t server_started, const core::HelloRequest &hello,
                       owh_sdk **output) noexcept {
    return Access::connect_sdk(std::move(connection), server_pid, server_started, hello, output);
}
} // namespace overwolf_hook::client::test_fixture
