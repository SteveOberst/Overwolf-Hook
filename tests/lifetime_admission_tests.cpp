#include <owh/overwolf/overlays/lifetime_admission.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

namespace overwolf_hook::overwolf {
struct AdmissionFixture {
    static void initialize(LifetimeAdmission &admission, const ResolvedProfile &profile) {
        admission.allow_hot_attach();
        admission.profile_ = profile;
        admission.installed_.store(true, std::memory_order_release);
        admission.epoch_and_open_.store(0, std::memory_order_release);
        admission.admitted_root_.store(0, std::memory_order_release);
    }
    static uint32_t transitions(const LifetimeAdmission &admission) {
        return admission.active_transitions_.load(std::memory_order_acquire);
    }
    static void pause_before_open(LifetimeAdmission &admission, void (*callback)(void *) noexcept,
                                  void *context) {
        admission.before_open_fixture_ = callback;
        admission.fixture_context_ = context;
    }
};
} // namespace overwolf_hook::overwolf

namespace {
using namespace overwolf_hook::overwolf;

void __declspec(noinline) fixture_run() {}
void __declspec(noinline) fixture_clear() {}
void __declspec(noinline) fixture_unhook() {}
void __declspec(noinline) fixture_init() {}
void __declspec(noinline) fixture_agent_clear() {}

struct Root {
    uint64_t *vtable;
    std::array<std::byte, 0x2b0> pad{};
    uint64_t refresher = 1;
};
static_assert(offsetof(Root, refresher) == 0x2b8);

struct Pause {
    HANDLE reached = nullptr;
    HANDLE release = nullptr;
};
void pause_before_open(void *opaque) noexcept {
    auto &pause = *static_cast<Pause *>(opaque);
    SetEvent(pause.reached);
    WaitForSingleObject(pause.release, 5000);
}

bool require(bool condition, const char *label) {
    std::cout << (condition ? "PASS " : "FAIL ") << label << '\n';
    return condition;
}

overwolf_hook::overwolf::ResolvedProfile fixture_profile(uint64_t &root_slot, Root &root,
                                                      std::array<uint64_t, 5> &vtable) {
    overwolf_hook::overwolf::ResolvedProfile profile{};
    const auto address = [](auto fn) { return reinterpret_cast<uint64_t>(fn); };
    profile.common = profile.lifecycle = true;
    profile.agent_global_slot = reinterpret_cast<uint64_t>(&root_slot);
    profile.agent_init = address(&fixture_init);
    profile.agent_clear = address(&fixture_agent_clear);
    profile.run_agent = address(&fixture_run);
    profile.clear_agent = address(&fixture_clear);
    profile.unhook_agent = address(&fixture_unhook);





    vtable[2] = profile.agent_init;
    vtable[4] = profile.agent_clear;
    root.vtable = vtable.data();
    root.refresher = 1;
    root_slot = reinterpret_cast<uint64_t>(&root);
    return profile;
}

} // namespace

int main() {
    LifetimeAdmission admission;
    std::array<uint64_t, 5> vtable{};
    Root root{};
    uint64_t root_slot = 0;
    const auto profile = fixture_profile(root_slot, root, vtable);
    AdmissionFixture::initialize(admission, profile);

    uint64_t admitted_root = 0, epoch = 0;
    bool ok = require(admission.try_refresh(admitted_root, epoch) &&
                          admitted_root == reinterpret_cast<uint64_t>(&root) && epoch == 0,
                      "explicit hot admission still validates the root");
    auto lease = admission.try_enter();
    ok = require(lease && lease.root() == reinterpret_cast<uint64_t>(&root) && lease.epoch() == 0,
                 "publisher lease carries the admitted root and epoch") &&
         ok;

    HANDLE transition_started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE transition_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE transition_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread transition([&] {
        SetEvent(transition_started);
        admission.lifecycle_enter();
        SetEvent(transition_entered);
        WaitForSingleObject(transition_release, 5000);
        admission.lifecycle_leave();
    });
    WaitForSingleObject(transition_started, 5000);
    Sleep(25);
    ok = require(!admission.try_enter(),
                 "lifecycle transition closes admission before draining publishers") &&
         ok;
    ok = require(WaitForSingleObject(transition_entered, 0) == WAIT_TIMEOUT,
                 "transition waits until the active publisher releases its lease") &&
         ok;
    lease = AdmissionLease{};
    ok = require(WaitForSingleObject(transition_entered, 5000) == WAIT_OBJECT_0,
                 "transition proceeds after lease release") &&
         ok;
    SetEvent(transition_release);
    transition.join();
    uint64_t next_root = 0, next_epoch = 0;
    ok = require(admission.try_refresh(next_root, next_epoch) &&
                     next_root == reinterpret_cast<uint64_t>(&root) && next_epoch > epoch,
                 "same root pointer is admitted as a new lifecycle epoch") &&
         ok;
    auto current = admission.try_enter();
    ok = require(current && current.epoch() == next_epoch,
                 "new publisher lease observes the new epoch") &&
         ok;
    current = AdmissionLease{};

    admission.lifecycle_enter();
    admission.lifecycle_leave();
    Pause pause{CreateEventW(nullptr, TRUE, FALSE, nullptr),
                CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    AdmissionFixture::pause_before_open(admission, &pause_before_open, &pause);
    std::atomic<bool> refresh_result{true};
    std::thread refreshing([&] {
        uint64_t observed_root = 0, observed_epoch = 0;
        refresh_result.store(admission.try_refresh(observed_root, observed_epoch),
                             std::memory_order_release);
    });
    const bool paused = WaitForSingleObject(pause.reached, 5000) == WAIT_OBJECT_0;
    std::thread racing_transition([&] {
        admission.lifecycle_enter();
        admission.lifecycle_leave();
    });
    for (unsigned i = 0; i < 500 && AdmissionFixture::transitions(admission) == 0; ++i)
        Sleep(1);
    const bool transition_started_in_refresh = AdmissionFixture::transitions(admission) != 0;
    SetEvent(pause.release);
    refreshing.join();
    racing_transition.join();
    AdmissionFixture::pause_before_open(admission, nullptr, nullptr);
    ok = require(paused && transition_started_in_refresh &&
                     !refresh_result.load(std::memory_order_acquire) && !admission.try_enter(),
                 "transition racing root publication aborts the old epoch") &&
         ok;
    ok = require(admission.try_refresh(next_root, next_epoch) &&
                     next_root == reinterpret_cast<uint64_t>(&root) && next_epoch > 2,
                 "retry admits the same root only after the transition completes") &&
         ok;

    CloseHandle(pause.reached);
    CloseHandle(pause.release);
    CloseHandle(transition_started);
    CloseHandle(transition_entered);
    CloseHandle(transition_release);
    return ok ? 0 : 1;
}
