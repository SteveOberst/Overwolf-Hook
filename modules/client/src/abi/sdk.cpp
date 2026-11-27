#include "handles.hpp"
#include "BuildInfo.hpp"
#include <overwolf_hook/abi/sdk.h>
using namespace overwolf_hook;
using namespace overwolf_hook::client::abi;
extern "C" {
OWH_CLIENT uint32_t __cdecl owh_sdk_abi_version() { return OWH_ABI_VERSION; }
OWH_CLIENT owh_result __cdecl owh_sdk_build_info(owh_build_info *output) {
    if (!output)
        return invalid();
    *output = build_info::value;
    return ok;
}
OWH_CLIENT void __cdecl owh_client_options_init(owh_client_options *options) {
    if (options)
        *options = OWH_CLIENT_OPTIONS_INIT;
}
OWH_CLIENT owh_result __cdecl owh_sdk_open(owh_sdk **output) {
    if (output)
        *output = nullptr;
    if (!output)
        return invalid();
    return boundary([&] {
        *output = new owh_sdk;
        return ok;
    });
}
OWH_CLIENT void __cdecl owh_sdk_close(owh_sdk *sdk) { delete sdk; }
OWH_CLIENT owh_result __cdecl owh_sdk_attach(owh_sdk *sdk, const owh_client_options *options) {
    return sdk ? sdk->value.attach(options) : invalid();
}
OWH_CLIENT owh_result __cdecl owh_sdk_detach(owh_sdk *sdk) {
    return sdk ? sdk->value.detach() : invalid();
}
OWH_CLIENT owh_result __cdecl owh_sdk_check(const owh_client_options *options,
                                            owh_client_sanity_report *out) {
    return client::AttachedSession::check(options, out);
}
OWH_CLIENT owh_result __cdecl owh_sdk_probe_agent(const owh_client_options *options,
                                                  owh_agent_info *out) {
    return client::AttachedSession::probe(options, out);
}
OWH_CLIENT owh_result __cdecl owh_sdk_agent_info(owh_sdk *sdk, owh_agent_info *out) {
    if (!sdk || !out || out->size != sizeof(*out))
        return invalid();
    auto [result, connection] = sdk->value.session();
    if (result.status == OWH_OK)
        *out = connection->agent_info();
    return result;
}
OWH_CLIENT owh_result __cdecl owh_sdk_ping(owh_sdk *sdk, uint32_t timeout) {
    if (!sdk)
        return invalid();
    auto [result, connection] = sdk->value.session();
    return result.status == OWH_OK ? connection->ping(timeout) : result;
}
}
