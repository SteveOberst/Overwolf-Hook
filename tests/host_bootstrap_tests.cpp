#include <windows.h>
#include "session/bootstrap_payload.hpp"
#include "support/sdk.hpp"
#include <BuildInfo.hpp>
#include <overwolf_hook/abi/overlays.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 2, "paired runtime path supplied");
        overwolf_hook::client::detail::AgentPayload payload;
        require(overwolf_hook::client::detail::parse_agent_payload(argv[1], payload),
                "runtime bootstrap and build-info exports match the generated Client build info");
        require(overwolf_hook::client::detail::compatible_build_info(payload.build_info) &&
                    payload.build_info.build_number == overwolf_hook::build_info::value.build_number &&
                    std::memcmp(payload.build_info.sdk_version, overwolf_hook::build_info::value.sdk_version,
                                sizeof(payload.build_info.sdk_version)) == 0,
                "exported Agent metadata matches version and build number");
        auto other_build = payload.build_info;
        other_build.build_number ^= 1;
        require(!overwolf_hook::client::detail::compatible_build_info(other_build),
                "resident bootstrap gate rejects a mismatched build number");
        require(owh_sdk_abi_version() == 1, "ABI bootstrap export");
        owh_client_options options{};
        options.size = sizeof(options);
        options.abi_version = OWH_ABI_VERSION;
        options.process_id = GetCurrentProcessId();
        owh_client_sanity_report report{};
        report.size = sizeof(report);
        const auto checked = owh_sdk_check(&options, &report);
        require(checked.status == OWH_OK, "check failures are diagnostic report data");
        require(report.checked == OWH_CHECK_HOST_IDENTITY && report.passed == 0,
                "unreachable module/profile checks remain untested");
        require(report.host_status.status == OWH_NOT_FOUND &&
                    report.reviewed_build == OWH_BUILD_UNREVIEWED,
                "preflight rejects non-Overwolf identity without attaching");
        report.size -= 4;
        report.passed = 0x12345678;
        require(owh_sdk_check(&options, &report).status ==
                        OWH_INVALID_ARGUMENT &&
                    report.passed == 0x12345678,
                "preflight output size is checked before writing");
        const auto denied = owh_sdk_attach(owh_test::passive_sdk(), &options);
        require(denied.status == OWH_NOT_FOUND &&
                    denied.failure_stage == OWH_STAGE_TARGET_DISCOVERY,
                "exact executable selection rejects an owned fixture before active access");
        options.experimental_features = 0x80000000u;
        owh_agent_info agent{};
        agent.size = sizeof(agent);
        require(owh_sdk_probe_agent(&options, &agent).status ==
                    OWH_INVALID_ARGUMENT,
                "probe uses the same canonical option validation as attach");
        report.size = sizeof(report);
        require(owh_sdk_check(&options, &report).status ==
                    OWH_INVALID_ARGUMENT,
                "preflight uses the same canonical option validation as attach");
        options.experimental_features = 0;
        options.attach_mode = 0xffffffffu;
        require(owh_sdk_attach(owh_test::passive_sdk(), &options).status == OWH_INVALID_ARGUMENT,
                "unknown attach mode rejected before process operations");
        options.attach_mode = OWH_ATTACH_HOT;
        require(owh_sdk_attach(owh_test::passive_sdk(), &options).status == OWH_NOT_FOUND,
                "HOT rejects a non-Overwolf fixture without starting a process");
        options.size -= sizeof(uint32_t);
        require(owh_sdk_attach(owh_test::passive_sdk(), &options).status == OWH_INVALID_ARGUMENT,
                "invalid sized options rejected before target discovery");
        owh_overlay_capabilities small{};
        small.size = sizeof(small) - sizeof(uint32_t);
        small.available = 0x12345678;
        const auto bad_output = owh_overlay_query_capabilities(
            reinterpret_cast<owh_sdk *>(static_cast<uintptr_t>(1)), &small);
        require(bad_output.status == OWH_INVALID_ARGUMENT && small.available == 0x12345678,
                "capability output size is checked before writing");
        require(owh_sdk_detach(nullptr).status == OWH_INVALID_ARGUMENT, "null detach is rejected");
        std::cout << "PASS paired ABI payload/export parsing, host identity rejection and sized "
                     "output guard\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL host bootstrap: " << error.what() << '\n';
        return 1;
    }
}
