#pragma once
#include <overwolf_hook/types/discovery.h>
#include <overwolf_hook/types/state.h>
#include <owh/overwolf/info_db.hpp>
#include <owh/platform/result.hpp>
#include <vector>
namespace overwolf_hook::overwolf {
/** Owned passive InfoDB copy. Native field pointers refer to values_ and stay
 * valid for this snapshot's lifetime, including moves. No provider mapping is
 * retained. There is no Agent connection or remote service on this path. */
class StateSnapshot final {
  public:
    StateSnapshot() = default;
    StateSnapshot(const StateSnapshot &) = delete;
    StateSnapshot &operator=(const StateSnapshot &) = delete;
    StateSnapshot(StateSnapshot &&) noexcept = default;
    StateSnapshot &operator=(StateSnapshot &&) noexcept = default;
    static owh_result read(uint32_t source_channel, uint32_t timeout_ms, StateSnapshot &,
                           uint32_t producer_process_id = 0) noexcept;
    const std::vector<owh_state_field> &fields() const noexcept { return fields_; }

  private:
    std::vector<InfoDbValue> values_;
    std::vector<owh_state_field> fields_;
};
owh_result discover_sources(std::vector<owh_source_channel> &) noexcept;
} // namespace overwolf_hook::overwolf
