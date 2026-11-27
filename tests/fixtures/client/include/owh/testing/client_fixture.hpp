#pragma once

#include <memory>
#include <overwolf_hook/abi/sdk.h>
#include <owh/core/service_contracts.hpp>
#include <owh/ipc/session_transport.hpp>

#ifdef OWH_CLIENT_OVERLAY_TEST_FIXTURE
namespace overwolf_hook::client::test_fixture {
owh_result connect_sdk(std::unique_ptr<overwolf_hook::ipc::MessageConnection> connection,
                       uint32_t server_pid, uint64_t server_started,
                       const overwolf_hook::core::HelloRequest &hello, owh_sdk **output) noexcept;
}
#endif
