#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <owh/overwolf/overlays/lifetime_admission.hpp>
#include <owh/overwolf/overlays/startup_bridge.hpp>
#include <owh/platform/handle.hpp>
#include <stdexcept>
#include <string>
using namespace overwolf_hook;
using namespace overwolf_hook::overwolf;
using namespace std::chrono_literals;
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
namespace {
Handle reached(CreateEventW(nullptr, TRUE, FALSE, nullptr));
Handle resume(CreateEventW(nullptr, TRUE, FALSE, nullptr));
std::atomic<bool> installed{false};
bool pause_lookup = false;
void observed(HMODULE module, FARPROC run) noexcept {
    ResolvedProfile profile{};
    profile.common = profile.lifecycle = true;
    profile.run_agent = reinterpret_cast<uint64_t>(run);
    profile.clear_agent = reinterpret_cast<uint64_t>(GetProcAddress(module, "clearAgent"));
    profile.unhook_agent = reinterpret_cast<uint64_t>(GetProcAddress(module, "unhookAgent"));
    profile.agent_init = reinterpret_cast<uint64_t>(GetProcAddress(module, "fixture_init"));
    profile.agent_clear = reinterpret_cast<uint64_t>(GetProcAddress(module, "fixture_clear"));
    profile.agent_global_slot = reinterpret_cast<uint64_t>(GetProcAddress(module, "fixture_root"));
    installed = process_lifetime_admission().install_lifecycle_hooks(profile);
    SetEvent(reached.get());
    if (pause_lookup)
        WaitForSingleObject(resume.get(), 5000);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        REQUIRE(argc == 4);
        const std::wstring mode = argv[1];
        auto server = LoadLibraryW(argv[2]);
        REQUIRE(server);
        auto load =
            reinterpret_cast<bool (*)(const wchar_t *)>(GetProcAddress(server, "fixture_load"));
        REQUIRE(load);
        if (mode == L"late") {
            auto module = LoadLibraryW(argv[3]);
            REQUIRE(module);
            REQUIRE(!arm_startup_bridge(server, observed));
            REQUIRE(!installed);
            return 0;
        }
        if (mode == L"null")
            SetEnvironmentVariableW(L"OWH_STARTUP_FIXTURE", L"null");
        if (mode == L"throw")
            SetEnvironmentVariableW(L"OWH_STARTUP_FIXTURE", L"throw");
        if (mode == L"failed" || mode == L"missing")
            SetEnvironmentVariableW(L"OWH_STARTUP_FIXTURE", mode.c_str());
        REQUIRE(arm_startup_bridge(server, observed));
        pause_lookup = mode == L"ordering";
        auto loaded = std::async(std::launch::async, [&] { return load(argv[3]); });
        REQUIRE(WaitForSingleObject(reached.get(), 5000) == WAIT_OBJECT_0);
        REQUIRE(installed);
        auto module = GetModuleHandleW(L"OWAgent.dll");
        REQUIRE(module);
        auto *root = reinterpret_cast<const uint64_t *>(GetProcAddress(module, "fixture_root"));
        REQUIRE(root);
        auto &admission = process_lifetime_admission();
        uint64_t admitted = 0, epoch = 0;
        if (pause_lookup) {
            REQUIRE(*root == 0 && !admission.try_refresh(admitted, epoch));
            REQUIRE(loaded.wait_for(10ms) == std::future_status::timeout);
            SetEvent(resume.get());
        }
        if (mode == L"throw") {
            bool threw = false;
            try {
                (void)loaded.get();
            } catch (...) {
                threw = true;
            }
            REQUIRE(threw && *root && !admission.try_refresh(admitted, epoch));
            return 0;
        }
        REQUIRE(loaded.get());
        if (mode == L"null") {
            REQUIRE(*root == 0 && !admission.try_refresh(admitted, epoch));
            return 0;
        }
        if (mode == L"failed" || mode == L"missing") {
            REQUIRE(*root && !admission.try_refresh(admitted, epoch));
            return 0;
        }
        REQUIRE(admission.try_refresh(admitted, epoch) && admitted == *root);
        const auto *init_result =
            reinterpret_cast<const uint64_t *>(GetProcAddress(module, "fixture_last_init"));
        REQUIRE(init_result && *init_result == 0xfedcba9876543201ull);
        auto clear = reinterpret_cast<void (*)()>(GetProcAddress(module, "clearAgent"));
        clear();
        REQUIRE(!admission.try_enter() && !admission.try_refresh(admitted, epoch));
        auto run = reinterpret_cast<void (*)(uint32_t)>(GetProcAddress(module, "runAgent"));
        run(GetCurrentProcessId());
        REQUIRE(admission.try_refresh(admitted, epoch));
        SetEnvironmentVariableW(L"OWH_STARTUP_FIXTURE", L"failed");
        run(GetCurrentProcessId());
        REQUIRE(*root && !admission.try_refresh(admitted, epoch));
        SetEnvironmentVariableW(L"OWH_STARTUP_FIXTURE", nullptr);
        run(GetCurrentProcessId());
        REQUIRE(admission.try_refresh(admitted, epoch));
        auto unhook = reinterpret_cast<void (*)()>(GetProcAddress(module, "unhookAgent"));
        unhook();
        REQUIRE(!admission.try_refresh(admitted, epoch));
        std::cout << "PASS cold lookup ordering and subsequent lifecycle admission\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
