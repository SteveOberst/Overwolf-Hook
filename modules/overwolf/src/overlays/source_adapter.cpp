#include <algorithm>
#include <array>
#include <cstring>
#include <owh/overwolf/overlays/lifetime_admission.hpp>
#include <owh/overwolf/overlays/native_contract.hpp>
#include <owh/overwolf/overlays/source_adapter.hpp>
#include <owh/overwolf/signatures/overlay_targets.hpp>
#include <owh/platform/result.hpp>

namespace overwolf_hook::overwolf {
namespace {
using namespace native_contract;
using namespace overwolf_hook::overwolf::signatures::overlay;
bool safe_read(uint64_t address, void *out, size_t bytes) noexcept {
    if (!address || !out || !bytes)
        return false;
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), out,
                             bytes, &copied) &&
           copied == bytes;
}
class RegistryLease final {
  public:
    explicit RegistryLease(uint64_t root) noexcept {
        if (!root ||
            root > UINT64_MAX - agent_registry_wrapper_offset - registry_critical_section_offset)
            return;
        const uint64_t address =
            root + agent_registry_wrapper_offset + registry_critical_section_offset;
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<const void *>(address), &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT ||
            address + sizeof(CRITICAL_SECTION) >
                reinterpret_cast<uint64_t>(memory.BaseAddress) + memory.RegionSize)
            return;
        auto *candidate = reinterpret_cast<CRITICAL_SECTION *>(address);
        if (TryEnterCriticalSection(candidate)) {
            section_ = candidate;
            held_ = true;
        }
    }
    ~RegistryLease() {
        if (held_)
            LeaveCriticalSection(section_);
    }
    RegistryLease(const RegistryLease &) = delete;
    RegistryLease &operator=(const RegistryLease &) = delete;
    explicit operator bool() const noexcept { return held_; }

  private:
    CRITICAL_SECTION *section_ = nullptr;
    bool held_ = false;
};
enum class Witness : uint8_t { present, absent, corrupt };
Witness connector_witness(uint64_t root, HWND wanted, uint64_t *token = nullptr) noexcept {
    if (token)
        *token = 0;
    uint32_t count = 0;
    uint64_t list = 0;
    if (!safe_read(root + agent_connector_count_offset, &count, sizeof(count)) ||
        !safe_read(root + agent_connector_list_offset, &list, sizeof(list)) ||
        count > maximum_agent_connectors || (count && !list))
        return Witness::corrupt;
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t connector = 0;
        if (!safe_read(list + uint64_t{i} * sizeof(uint64_t), &connector, sizeof(connector)))
            return Witness::corrupt;
        if (!connector)
            continue;
        HWND hwnd = nullptr;
        if (!safe_read(connector + connector_hwnd_offset, &hwnd, sizeof(hwnd)))
            return Witness::corrupt;
        if (hwnd != wanted)
            continue;
        if (token && !safe_read(connector + connector_shared_texture_offset, token, sizeof(*token)))
            return Witness::corrupt;
        return Witness::present;
    }
    return Witness::absent;
}
} // namespace

SourceAdapter::SourceAdapter(LifetimeAdmission &admission, const ResolvedProfile &profile) noexcept
    : admission_(admission), profile_(profile) {}

owh_result SourceAdapter::register_source(HWND window, Geometry geometry,
                                          Registration &state) noexcept {
    uint64_t root = 0, epoch = 0;
    if (!window || !admission_.try_refresh(root, epoch))
        return make_result(OWH_AGENT_NOT_READY, OWH_STAGE_AGENT_LIFETIME);
    auto admission = admission_.try_enter();
    if (!admission)
        return make_result(OWH_BUSY, OWH_STAGE_AGENT_LIFETIME);
    RegistryLease registry(admission.root());
    if (!registry)
        return make_result(OWH_BUSY, OWH_STAGE_SOURCE_REGISTER);
    return register_locked(admission.root(), admission.epoch(), window, geometry, state);
}

owh_result SourceAdapter::register_locked(uint64_t root, uint64_t epoch, HWND window,
                                          Geometry geometry, Registration &state) noexcept {
    if (!profile_.register_window)
        return make_result(OWH_INCOMPATIBLE, OWH_STAGE_PROFILE_RESOLVE);
    const bool reported = reinterpret_cast<register_window_fn>(profile_.register_window)(
        reinterpret_cast<void *>(root), window);
    const auto witness = connector_witness(root, window);
    // A corrupt witness cannot establish that a failed native return left no
    // connector behind. Keep the attempted registration reachable for retry.
    if (reported || witness != Witness::absent)
        state = {window, epoch, true};
    if (witness != Witness::present)
        return make_result(witness == Witness::corrupt ? OWH_CORRUPT : OWH_SYSTEM_ERROR,
                           OWH_STAGE_SOURCE_REGISTER);
    if (!profile_.set_position || !reinterpret_cast<set_position_fn>(profile_.set_position)(
                                      window, geometry.left, geometry.top))
        return make_result(OWH_SYSTEM_ERROR, OWH_STAGE_WINDOW);
    return make_result(OWH_OK);
}

owh_result SourceAdapter::publish_pixels(HWND window, core::ResourceRef source_id,
                                         const Registration &state,
                                         const overwolf::PixelView &pixels) noexcept {
    if (!source_id.session_id || !source_id.resource_id || !pixels.width || !pixels.height ||
        pixels.stride < pixels.width * 4 ||
        pixels.bytes.size() < static_cast<uint64_t>(pixels.stride) * pixels.height)
        return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_SOURCE_PUBLISH);
    if (!state.registered)
        return make_result(OWH_AGENT_NOT_READY, OWH_STAGE_SOURCE_PUBLISH);
    uint64_t root = 0, epoch = 0;
    if (!admission_.try_refresh(root, epoch))
        return admission_.observed_epoch() == state.epoch
                   ? make_result(OWH_BUSY, OWH_STAGE_AGENT_LIFETIME)
                   : make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME);
    auto admission = admission_.try_enter();
    if (!admission)
        return make_result(OWH_BUSY, OWH_STAGE_AGENT_LIFETIME);
    if (admission.epoch() != state.epoch)
        return make_result(OWH_AGENT_LOST, OWH_STAGE_AGENT_LIFETIME);
    RegistryLease registry(admission.root());
    if (!registry)
        return make_result(OWH_BUSY, OWH_STAGE_SOURCE_PUBLISH);
    const auto witness = connector_witness(admission.root(), state.connector);
    if (witness != Witness::present)
        return make_result(witness == Witness::corrupt ? OWH_CORRUPT : OWH_AGENT_LOST,
                           OWH_STAGE_SOURCE_PUBLISH);
    rect32 damage{0, 0, static_cast<int32_t>(pixels.width), static_cast<int32_t>(pixels.height)};
    if (pixels.damage.width || pixels.damage.height) {
        if (!core::valid_region(pixels.damage, pixels.width, pixels.height))
            return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_SOURCE_PUBLISH);
        damage = {static_cast<int32_t>(pixels.damage.x), static_cast<int32_t>(pixels.damage.y),
                  static_cast<int32_t>(pixels.damage.x + pixels.damage.width),
                  static_cast<int32_t>(pixels.damage.y + pixels.damage.height)};
    }
    uint64_t output = 0;
    const uint64_t outcome = reinterpret_cast<paint_fn>(profile_.source_paint)(
        window, pixels.bytes.data(), static_cast<int32_t>(pixels.width),
        static_cast<int32_t>(pixels.height), &damage, false, false, &output);
    return outcome && output ? make_result(OWH_OK)
                             : make_result(OWH_SYSTEM_ERROR, OWH_STAGE_SOURCE_PUBLISH);
}

owh_result SourceAdapter::bind_texture(HWND window, Geometry geometry, uint64_t token,
                                       Registration &state) noexcept {
    uint64_t root = 0, epoch = 0;
    if (!admission_.try_refresh(root, epoch))
        return make_result(OWH_AGENT_NOT_READY, OWH_STAGE_AGENT_LIFETIME);
    auto admission = admission_.try_enter();
    if (!admission)
        return make_result(OWH_BUSY, OWH_STAGE_AGENT_LIFETIME);
    RegistryLease registry(admission.root());
    if (!registry)
        return make_result(OWH_BUSY, OWH_STAGE_SOURCE_REGISTER);
    const auto registered =
        register_locked(admission.root(), admission.epoch(), window, geometry, state);
    if (registered.status != OWH_OK)
        return registered;
    uint64_t observed = 0;
    if (!profile_.accelerated_paint)
        return make_result(OWH_INCOMPATIBLE, OWH_STAGE_PROFILE_RESOLVE);
    rect32 damage{0, 0, static_cast<int32_t>(geometry.width),
                  static_cast<int32_t>(geometry.height)};
    reinterpret_cast<accelerated_paint_fn>(profile_.accelerated_paint)(
        window, reinterpret_cast<void *>(static_cast<uintptr_t>(token)), &damage, false);
    const auto token_witness = connector_witness(admission.root(), window, &observed);
    return token_witness == Witness::present && observed == token
               ? make_result(OWH_OK)
               : make_result(token_witness == Witness::corrupt ? OWH_CORRUPT : OWH_SYSTEM_ERROR,
                             OWH_STAGE_SOURCE_PUBLISH);
}

owh_result SourceAdapter::refresh_windows(uint64_t root, HWND window) noexcept {
    if (!profile_.refresh_windows || !root)
        return make_result(OWH_AGENT_NOT_READY, OWH_STAGE_CLEANUP);
    reinterpret_cast<refresh_windows_fn>(profile_.refresh_windows)(reinterpret_cast<void *>(root),
                                                                   false, false);
    const auto witness = connector_witness(root, window);
    if (witness == Witness::absent)
        return make_result(OWH_OK);
    return make_result(witness == Witness::corrupt ? OWH_CORRUPT : OWH_BUSY, OWH_STAGE_CLEANUP);
}
owh_result SourceAdapter::remove_source(Registration &state) noexcept {
    if (!state.registered)
        return make_result(OWH_OK);
    uint64_t root = 0, epoch = 0;
    const bool valid = admission_.try_refresh(root, epoch);
    if (!valid) {
        if (admission_.observed_epoch() != state.epoch) {
            state = {};
            return make_result(OWH_OK);
        }
        return make_result(OWH_BUSY, OWH_STAGE_CLEANUP);
    }
    if (epoch != state.epoch) {
        if (admission_.observed_epoch() != state.epoch) {
            state = {};
            return make_result(OWH_OK);
        }
        return make_result(OWH_BUSY, OWH_STAGE_CLEANUP);
    }
    auto admission = admission_.try_enter();
    if (!admission || admission.epoch() != state.epoch)
        return make_result(OWH_BUSY, OWH_STAGE_CLEANUP);
    RegistryLease registry(admission.root());
    if (!registry)
        return make_result(OWH_BUSY, OWH_STAGE_CLEANUP);
    const auto outcome = refresh_windows(admission.root(), state.connector);
    if (outcome.status == OWH_OK)
        state = {};
    return outcome;
}
} // namespace overwolf_hook::overwolf
