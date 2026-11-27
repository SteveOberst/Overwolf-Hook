#pragma once

#include <owh/overwolf/signatures/resolver.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>

namespace overwolf_hook::overwolf {

class LifetimeAdmission;

class AdmissionLease final {
  public:
    AdmissionLease() noexcept = default;
    AdmissionLease(const AdmissionLease &) = delete;
    AdmissionLease &operator=(const AdmissionLease &) = delete;
    AdmissionLease(AdmissionLease &&other) noexcept;
    AdmissionLease &operator=(AdmissionLease &&other) noexcept;
    ~AdmissionLease();

    explicit operator bool() const noexcept { return owner_ != nullptr; }
    uint64_t root() const noexcept { return root_; }
    uint64_t epoch() const noexcept { return epoch_; }

  private:
    friend class LifetimeAdmission;
    AdmissionLease(LifetimeAdmission *owner, uint64_t root, uint64_t epoch) noexcept;
    LifetimeAdmission *owner_ = nullptr;
    uint64_t root_ = 0;
    uint64_t epoch_ = 0;
};

/** Process-local guard for the reviewed Agent generation.
 *
 * Hooks only observe lifecycle entry/exit. They do not replace paint or source
 * publication. Controlled startup installs observers before the first runAgent
 * call returns its resolved pointer. Hot attach explicitly accepts an unobserved
 * initial transition. Later transitions close admission and drain our publishers.
 */
class LifetimeAdmission final {
  public:
    LifetimeAdmission() noexcept = default;
    LifetimeAdmission(const LifetimeAdmission &) = delete;
    LifetimeAdmission &operator=(const LifetimeAdmission &) = delete;

    bool prepare_controlled_start(uint32_t timeout_ms = 2000) noexcept;
    void allow_hot_attach() noexcept { initial_ready_.store(true); }
    void invalidate_readiness() noexcept { initial_ready_.store(false); }
    void run_completed(bool initialized) noexcept { initial_ready_.store(initialized); }
    bool install_lifecycle_hooks(const ResolvedProfile &profile) noexcept;
    bool resolve_profile(HMODULE agent, ResolvedProfile &profile) noexcept;
    bool try_refresh(uint64_t &root_out, uint64_t &epoch_out) noexcept;
    AdmissionLease try_enter() noexcept;
    bool installed() const noexcept { return installed_.load(); }
    uint64_t observed_epoch() const noexcept { return epoch_and_open_.load() >> 1; }
    ResolvedProfile profile_snapshot() const noexcept;

    void lifecycle_enter() noexcept;
    void lifecycle_leave() noexcept;

  private:
    friend class AdmissionLease;
#ifdef OWH_ADMISSION_TEST_FIXTURE
    friend struct AdmissionFixture;
    void (*before_open_fixture_)(void *) noexcept = nullptr;
    void *fixture_context_ = nullptr;
#endif
    void leave() noexcept;
    bool validate_root(const ResolvedProfile &profile, uint64_t &root) const noexcept;

    // Low bit is publication admission; the high bits are the monotonically
    // changing lifecycle epoch. Transitions close and advance in one CAS.
    std::atomic<uint64_t> epoch_and_open_{0};
    std::atomic<uint32_t> active_publishers_{0};
    std::atomic<uint32_t> active_transitions_{0};
    std::atomic<uint64_t> admitted_root_{0};
    std::atomic<bool> installed_{false};
    std::atomic<bool> initial_ready_{false};
    std::mutex refresh_lock_;
    ResolvedProfile profile_{};
};

LifetimeAdmission &process_lifetime_admission() noexcept;

} // namespace overwolf_hook::overwolf
