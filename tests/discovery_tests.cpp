#include "support/sdk.hpp"
#include <cstring>
#include <iostream>
#include <map>
#include <overwolf_hook/abi/game_state.h>
#include <source_location>
#include <stdexcept>
#include <string>
#include <windows.h>

namespace {
void require(bool test, std::source_location where = std::source_location::current()) {
    if (!test)
        throw std::runtime_error("discovery assertion failed at line " +
                                 std::to_string(where.line()));
}
struct Section {
    HANDLE handle;
    explicit Section(const std::wstring &name)
        : handle(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096,
                                    name.c_str())) {
        require(handle && GetLastError() != ERROR_ALREADY_EXISTS);
    }
    ~Section() { CloseHandle(handle); }
};
std::map<uint32_t, uint32_t> discover() {
    owh_channels *snapshot = nullptr;
    const auto result = owh_game_state_sources(&snapshot);
    if (result.status != OWH_OK || !snapshot)
        throw std::runtime_error("discover status=" + std::to_string(result.status) +
                                 " stage=" + std::to_string(result.failure_stage) +
                                 " domain=" + std::to_string(result.error_domain) +
                                 " code=" + std::to_string(result.native_code));
    struct Owner {
        owh_channels *value;
        ~Owner() { owh_source_list_close(value); }
    } owner{snapshot};
    std::map<uint32_t, uint32_t> output;
    std::pair<uint32_t, uint32_t> previous{};
    for (uint32_t i = 0; i < owh_source_list_count(snapshot); ++i) {
        owh_source_channel channel{};
        require(owh_source_list_at(snapshot, i, &channel).status == OWH_OK);
        const auto identity = std::pair{channel.source_channel, channel.producer_process_id};
        require(identity > previous && channel.kinds && !(channel.kinds & ~3u));
        previous = identity;
        output.emplace(channel.source_channel, channel.kinds);
    }
    owh_source_channel invalid{1, 1};
    require(owh_source_list_at(snapshot, owh_source_list_count(snapshot), &invalid).status ==
            OWH_INVALID_ARGUMENT);
    require(invalid.source_channel == 0 && invalid.kinds == 0);
    return output;
}
} // namespace
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
            owh_state *state = nullptr;
            require(owh_game_state_read(id, 0, &state).status ==
                        OWH_NOT_FOUND &&
                    !state);
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
        owh_channels *invalid = nullptr;
        require(owh_game_state_sources(&invalid).status == OWH_OK && invalid);
        owh_source_list_close(invalid);
        require(owh_game_state_sources(nullptr).status == OWH_INVALID_ARGUMENT);
        std::cout << "PASS exact names/types, sorted flags, missing mutex, independent "
                     "re-enumeration and released handles\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
