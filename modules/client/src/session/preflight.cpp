#include "session/attached_session.hpp"
#include "session/target.hpp"
#include "owh/overwolf/signatures/overlay_targets.hpp"
#include "owh/overwolf/signatures/resolver.hpp"
#include <algorithm>
#include <owh/core/types.hpp>
#include <owh/platform/result.hpp>
#include <owh/platform/sha256.hpp>
using namespace overwolf_hook::client::detail;

owh_result __cdecl overwolf_hook::client::AttachedSession::check(
    const owh_client_options *input, owh_client_sanity_report *output) noexcept {
    const owh_client_options defaults = OWH_CLIENT_OPTIONS_INIT;
    if (!input)
        input = &defaults;
    if (!core::valid_options(*input) || !output || output->size != sizeof(*output))
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
    const auto options = *input;
    *output = {};
    output->size = sizeof(*output);
    const auto unchecked = make_result(OWH_UNSUPPORTED, OWH_STAGE_CAPABILITY);
    output->host_status = output->module_status = output->profile_status = output->hash_status =
        unchecked;
    try {
        output->checked = OWH_CHECK_HOST_IDENTITY;
        auto [result, target] = overwolf::select_process(options.process_id, options.process_started);
        output->host_status = result;
        if (result.status != OWH_OK)
            return make_result(OWH_OK);
        output->passed |= OWH_CHECK_HOST_IDENTITY;
        output->process_id = target.pid;
        output->process_started = target.started;
        output->checked |= OWH_CHECK_AGENT_MODULE;
        const auto [base, path] = module_in_process(target.pid, L"OWAgent.dll");
        if (!base || path.empty()) {
            output->module_status = make_result(OWH_NOT_FOUND, OWH_STAGE_PROFILE_RESOLVE);
            return make_result(OWH_OK);
        }
        // Keep one non-writable/non-deletable file handle throughout hashing and
        // scanning so the two observations refer to the same on-disk image.
        Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) {
            output->module_status = make_result(OWH_SYSTEM_ERROR, OWH_STAGE_PROFILE_RESOLVE,
                                                OWH_ERROR_WIN32, GetLastError());
            return make_result(OWH_OK);
        }
        Handle mapping(CreateFileMappingW(file.get(), nullptr, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE,
                                          0, 0, nullptr));
        View image;
        if (mapping)
            image.address = MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0);
        if (!image.address) {
            output->module_status = make_result(OWH_INCOMPATIBLE, OWH_STAGE_PROFILE_RESOLVE,
                                                OWH_ERROR_WIN32, GetLastError());
            return make_result(OWH_OK);
        }
        output->module_status = make_result(OWH_OK);
        output->passed |= OWH_CHECK_AGENT_MODULE;
        output->checked |= OWH_CHECK_AGENT_HASH;
        std::array<uint8_t, 32> digest{};
        if (platform::sha256_file(path, digest)) {
            output->passed |= OWH_CHECK_AGENT_HASH;
            output->hash_status = make_result(OWH_OK);
            std::ranges::copy(digest, output->agent_sha256);
            output->reviewed_build =
                digest == overwolf_hook::overwolf::signatures::overlay::reference_sha256
                    ? OWH_BUILD_REVIEWED
                    : OWH_BUILD_UNREVIEWED;
        } else
            output->hash_status = make_result(OWH_SYSTEM_ERROR, OWH_STAGE_PROFILE_RESOLVE);
        const auto profile =
            overwolf_hook::overwolf::resolve_loaded_profile(static_cast<HMODULE>(image.address));
        constexpr uint32_t targets = OWH_CHECK_COMMON_TARGETS | OWH_CHECK_LIFECYCLE_TARGETS |
                                     OWH_CHECK_PIXEL_BUFFER_TARGETS | OWH_CHECK_SHARED_TEXTURE_TARGETS;
        output->checked |= targets;
        if (profile.common)
            output->passed |= OWH_CHECK_COMMON_TARGETS;
        if (profile.lifecycle)
            output->passed |= OWH_CHECK_LIFECYCLE_TARGETS;
        if (profile.pixel_buffer_source)
            output->passed |= OWH_CHECK_PIXEL_BUFFER_TARGETS;
        if (profile.shared_texture_source)
            output->passed |= OWH_CHECK_SHARED_TEXTURE_TARGETS;
        output->profile_status = (output->passed & targets) == targets
                                     ? make_result(OWH_OK)
                                     : make_result(OWH_INCOMPATIBLE, OWH_STAGE_PROFILE_RESOLVE);
        if (WaitForSingleObject(target.process.get(), 0) != WAIT_TIMEOUT) {
            output->passed &= ~OWH_CHECK_HOST_IDENTITY;
            output->host_status = make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME);
        }
        return make_result(OWH_OK);
    } catch (const std::bad_alloc &) {
        return make_result(OWH_RESOURCE_EXHAUSTED, OWH_STAGE_ALLOCATION);
    } catch (...) {
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_PROFILE_RESOLVE);
    }
}
