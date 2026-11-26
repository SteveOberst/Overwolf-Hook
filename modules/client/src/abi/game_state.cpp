#include "handles.hpp"
#include <overwolf_hook/abi/game_state.h>
using namespace overwolf_hook;
using namespace overwolf_hook::client::abi;
extern "C" {
OWH_CLIENT owh_result __cdecl owh_game_state_sources(owh_channels **out) {
    if (out)
        *out = nullptr;
    if (!out)
        return invalid();
    return boundary([&] {
        auto list = std::make_unique<owh_channels>();
        const auto result = overwolf::discover_sources(list->value);
        if (result.status == OWH_OK)
            *out = list.release();
        return result;
    });
}
OWH_CLIENT uint32_t __cdecl owh_source_list_count(const owh_channels *list) {
    return list ? static_cast<uint32_t>(list->value.size()) : 0;
}
OWH_CLIENT owh_result __cdecl owh_source_list_at(const owh_channels *list, uint32_t index,
                                                 owh_source_channel *out) {
    if (out)
        *out = {};
    if (!list || !out || index >= list->value.size())
        return invalid();
    *out = list->value[index];
    return ok;
}
OWH_CLIENT void __cdecl owh_source_list_close(owh_channels *list) { delete list; }
OWH_CLIENT owh_result __cdecl owh_game_state_read(uint32_t channel, uint32_t timeout,
                                                  owh_state **out) {
    return owh_game_state_read_from(channel, 0, timeout, out);
}
OWH_CLIENT owh_result __cdecl owh_game_state_read_from(uint32_t channel, uint32_t producer,
    uint32_t timeout, owh_state **out) {
    if (out)
        *out = nullptr;
    if (!out)
        return invalid();
    return boundary([&] {
        auto snapshot = std::make_unique<owh_state>();
        const auto result = overwolf::StateSnapshot::read(channel, timeout, snapshot->value, producer);
        if (result.status == OWH_OK)
            *out = snapshot.release();
        return result;
    });
}
OWH_CLIENT uint32_t __cdecl owh_snapshot_count(const owh_state *snapshot) {
    return snapshot ? static_cast<uint32_t>(snapshot->value.fields().size()) : 0;
}
OWH_CLIENT owh_result __cdecl owh_snapshot_at(const owh_state *snapshot, uint32_t index,
                                              owh_state_field *out) {
    if (out)
        *out = {};
    if (!snapshot || !out || index >= snapshot->value.fields().size())
        return invalid();
    *out = snapshot->value.fields()[index];
    return ok;
}
OWH_CLIENT void __cdecl owh_snapshot_close(owh_state *snapshot) { delete snapshot; }
}
