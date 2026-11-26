#include <overwolf_hook/overwolf_hook.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/identity.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
std::wstring shutdown_event_name() {
    return L"Local\\owh.shutdown-test." + std::to_wstring(GetCurrentProcessId());
}
struct Fixture {
    overwolf_hook::Handle process;
    DWORD pid;
    Fixture(overwolf_hook::Handle handle, DWORD id) : process(std::move(handle)), pid(id) {}
    Fixture(Fixture &&) = default;
    ~Fixture() {
        // Only the exact process created by this test, never an enumerated host.
        if (process && WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.get(), 99);
            WaitForSingleObject(process.get(), 5000);
        }
    }
    uint64_t started() const { return overwolf_hook::platform::process_start(process.get()); }
    bool running() const { return WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT; }
};
Fixture launch(const wchar_t *path, const wchar_t *mode) {
    const auto ready_name = L"Local\\owh.control-test." + std::to_wstring(GetCurrentProcessId());
    overwolf_hook::Handle ready(CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str()));
    require(bool(ready), "create own readiness event");
    std::wstring command = L"\"" + std::wstring(path) + L"\" \"" + ready_name + L"\" \"" +
                           shutdown_event_name() + L"\" " + mode;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    require(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup, &child) != FALSE,
            "start exact owned fixture");
    Fixture fixture{overwolf_hook::Handle(child.hProcess), child.dwProcessId};
    overwolf_hook::Handle thread(child.hThread);
    require(WaitForSingleObject(ready.get(), 5000) == WAIT_OBJECT_0, "fixture ready");
    require(fixture.started() != 0, "fixture creation identity");
    return fixture;
}
}
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 3, "owned main-package and name-only fixture paths");
        owh_overwolf_process info{};
        info.size = sizeof(info) - 1;
        info.process_id = 0x12345678;
        require(owh_overwolf_process_query(GetCurrentProcessId(), 0, &info).status == OWH_INVALID_ARGUMENT &&
                    info.process_id == 0x12345678, "sized output checked before writing");
        info.size = sizeof(info);
        require(owh_overwolf_process_query(GetCurrentProcessId(), 0, &info).status == OWH_NOT_FOUND,
                "reject a process not named Overwolf.exe");
        require(owh_overwolf_start(0, &info).status == OWH_INVALID_ARGUMENT,
                "invalid start timeout rejected without launching anything");
        require(owh_overwolf_stop(GetCurrentProcessId(), 0, 100).status == OWH_INVALID_ARGUMENT,
                "stop requires creation identity");

        {
            auto decoy = launch(argv[2], L"ignore");
            overwolf_hook::OverwolfController controller;
            require(!controller.running_process(decoy.pid, decoy.started()),
                    "name-only Overwolf.exe is not a main-package identity");
            require(owh_overwolf_stop(decoy.pid, decoy.started(), 100).status == OWH_NOT_FOUND &&
                        decoy.running(), "name-only process cannot receive shutdown requests");
        }
        {
            auto fixture = launch(argv[1], L"exit");
            overwolf_hook::OverwolfController controller;
            const auto found = controller.running_process(fixture.pid, fixture.started());
            require(found && found->process_id == fixture.pid &&
                        found->process_started == fixture.started(),
                    "canonical process record through C++ facade, no SDK/Agent");
            controller.stop(*found, 2000);
            require(!fixture.running(), "cooperative event exits windowless process");
            DWORD exit_code = 1;
            require(GetExitCodeProcess(fixture.process.get(), &exit_code) && exit_code == 0,
                    "fixture completed its ordinary shutdown path");
            require(!controller.running_process(fixture.pid, fixture.started()), "absent process is optional");
        }
        {
            auto fixture = launch(argv[1], L"missing");
            const auto result = owh_overwolf_stop(fixture.pid, fixture.started(), 100);
            require(result.status == OWH_UNSUPPORTED && result.failure_stage == OWH_STAGE_OVERWOLF_STOP,
                    "absent cooperative event is unsupported");
            require(fixture.running(), "missing event leaves owned process running");
        }
        {
            auto fixture = launch(argv[1], L"ignore");
            overwolf_hook::Handle request(OpenEventW(SYNCHRONIZE, FALSE, shutdown_event_name().c_str()));
            require(bool(request), "open fixture event for signal observation");
            require(WaitForSingleObject(request.get(), 0) == WAIT_TIMEOUT, "fixture event starts nonsignalled");
            require(owh_overwolf_stop(fixture.pid, fixture.started() + 1, 100).status == OWH_INVALID_ARGUMENT,
                    "wrong creation time cannot request shutdown");
            require(WaitForSingleObject(request.get(), 0) == WAIT_TIMEOUT && fixture.running(),
                    "identity failure neither signals event nor stops process");
            const auto result = owh_overwolf_stop(fixture.pid, fixture.started(), 100);
            require(result.status == OWH_TIMEOUT && result.failure_stage == OWH_STAGE_OVERWOLF_STOP,
                    "accepted shutdown request without process exit times out");
            require(WaitForSingleObject(request.get(), 0) == WAIT_OBJECT_0,
                    "timeout followed actual cooperative event signal");
            require(fixture.running(), "timeout never force terminates process");
        }
        std::cout << "PASS independent Overwolf control, identity and cooperative windowless shutdown\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << "FAIL Overwolf control: " << failure.what() << '\n';
        return 1;
    }
}
