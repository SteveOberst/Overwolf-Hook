#pragma once
#include <overwolf_hook/abi/overwolf.h>
#include <overwolf_hook/sdk.hpp>
#include <optional>

namespace overwolf_hook {
/** Local Overwolf installation/process operations; no SDK attachment needed.
 * All returned records are snapshots. Start does not imply overlay readiness.
 */
class OverwolfController final {
  public:
    std::optional<owh_overwolf_installation> installation() const {
        owh_overwolf_installation value{};
        value.size = sizeof(value);
        const auto result = owh_overwolf_installation_query(&value);
        if (result.status == OWH_NOT_FOUND)
            return std::nullopt;
        check(result);
        return value;
    }
    std::optional<owh_overwolf_process> running_process(uint32_t pid = 0,
                                                      uint64_t started = 0) const {
        owh_overwolf_process value{};
        value.size = sizeof(value);
        const auto result = owh_overwolf_process_query(pid, started, &value);
        if (result.status == OWH_NOT_FOUND)
            return std::nullopt;
        check(result);
        return value;
    }
    owh_overwolf_process start(uint32_t timeout_ms = 10000) const {
        owh_overwolf_process value{};
        value.size = sizeof(value);
        check(owh_overwolf_start(timeout_ms, &value));
        return value;
    }
    void stop(const owh_overwolf_process &process, uint32_t timeout_ms = 10000) const {
        check(owh_overwolf_stop(process.process_id, process.process_started, timeout_ms));
    }
};

inline OverwolfController OverwolfHook::overwolf() const { return {}; }
} // namespace overwolf_hook
