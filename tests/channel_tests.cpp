#include "capture/overwolf/channel.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>

#define REQUIRE(test) do { if (!(test)) throw std::runtime_error(#test); } while (false)

int main() {
    try {
        using overwolf_hook::overwolf::event_channel;
        REQUIRE(event_channel("ow_events_10054261") == 10054261);
        REQUIRE(event_channel("ow_events_900001") == 900001);
        REQUIRE(event_channel("ow_events_4294967295") == UINT32_MAX);
        for (std::string_view name : {"", "ow_info_10054261", "ow_events_", "ow_events_0",
            "ow_events_-1", "ow_events_+1", "ow_events_1evnt", "ow_events_4294967296",
            "ow_events_999999999999999999999999", "other_ow_events_1", "ow_events_ 1"})
            REQUIRE(event_channel(name) == 0);
        const char embedded_null[] = "ow_events_123\0extra";
        REQUIRE(event_channel(std::string_view(embedded_null, sizeof(embedded_null) - 1)) == 0);
        std::cout << "PASS event channel identity, unrelated services, malformed and overflowing IDs\n";
        return 0;
    } catch (const std::exception& problem) {
        std::cerr << "FAIL " << problem.what() << '\n';
        return 1;
    }
}
