#include <overwolf_hook/discovery.h>
#include <overwolf_hook/state.h>
#include <windows.h>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {
void require(bool test) { if (!test) throw std::runtime_error("discovery assertion failed"); }
struct Section {
    HANDLE handle;
    explicit Section(const std::wstring& name) : handle(CreateFileMappingW(INVALID_HANDLE_VALUE,
        nullptr, PAGE_READWRITE, 0, 4096, name.c_str())) { require(handle && GetLastError() != ERROR_ALREADY_EXISTS); }
    ~Section() { CloseHandle(handle); }
};
std::map<uint32_t, uint32_t> discover() {
    owh_channels* snapshot = nullptr;
    const auto result = owh_channels_discover(OWH_ABI_VERSION, &snapshot);
    require(result.status == OWH_OK && snapshot);
    struct Owner { owh_channels* value; ~Owner() { owh_channels_close(value); } } owner{snapshot};
    std::map<uint32_t, uint32_t> output;
    uint32_t previous = 0;
    for (uint32_t i = 0; i < owh_channels_count(snapshot); ++i) {
        owh_source_channel channel{};
        require(owh_channels_at(snapshot, i, &channel).status == OWH_OK);
        require(channel.source_channel > previous && channel.kinds && !(channel.kinds & ~3u));
        previous = channel.source_channel; output.emplace(channel.source_channel, channel.kinds);
    }
    owh_source_channel invalid{1, 1};
    require(owh_channels_at(snapshot, owh_channels_count(snapshot), &invalid).status == OWH_INVALID_ARGUMENT);
    require(invalid.source_channel == 0 && invalid.kinds == 0);
    return output;
}
}
int main() {
    try {
        const uint32_t id = 3000000000u + GetCurrentProcessId();
        const auto text = std::to_wstring(id);
        require(!discover().contains(id));
        {
            Section info(L"ow_info_" + text);
            require(discover().at(id) == OWH_CHANNEL_INFO);
            {
                Section events(L"ow_events_" + text);
                require(discover().at(id) == (OWH_CHANNEL_INFO | OWH_CHANNEL_EVENTS));
            }
            require(discover().at(id) == OWH_CHANNEL_INFO);
            // Enumeration does not imply a healthy DB/mutex and holds neither.
            owh_state* state = nullptr;
            require(owh_state_read(OWH_ABI_VERSION, id, 0, &state).status == OWH_NOT_FOUND && !state);
        }
        require(!discover().contains(id));
        {
            Section events(L"ow_events_" + text);
            require(discover().at(id) == OWH_CHANNEL_EVENTS);
            Section suffix(L"ow_info_" + text + L"mtx");
            Section overflow(L"ow_info_4294967296");
            Section leading(L"ow_info_0" + text);
            Section zero(L"ow_info_0");
            Section negative(L"ow_info_-1");
            HANDLE fake = CreateEventW(nullptr, TRUE, FALSE, (L"ow_info_" + text).c_str());
            require(fake != nullptr);
            const auto found = discover();
            CloseHandle(fake);
            require(found.at(id) == OWH_CHANNEL_EVENTS && !found.contains(0));
        }
        owh_channels* invalid = nullptr;
        require(owh_channels_discover(99, &invalid).status == OWH_INCOMPATIBLE && !invalid);
        require(owh_channels_discover(1, nullptr).status == OWH_INVALID_ARGUMENT);
        std::cout << "PASS exact names/types, sorted flags, missing mutex, independent re-enumeration and released handles\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
