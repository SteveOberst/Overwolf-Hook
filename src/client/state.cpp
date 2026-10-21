#include <overwolf_hook/state.h>
#include "state/infodb.hpp"
#include "transport/channel.hpp"
#include <algorithm>
#include <cstring>

struct owh_state {
    std::vector<overwolf_hook::InfoField> fields;
};

extern "C" OWH_CLIENT owh_result __cdecl owh_state_read(uint32_t abi,
    uint32_t channel, uint32_t timeout, owh_state** output) {
    if (output) *output = nullptr;
    return overwolf_hook::boundary([&] {
        using namespace overwolf_hook;
        if (!output || !channel || timeout > 1000) fail(OWH_INVALID_ARGUMENT);
        if (abi != OWH_ABI_VERSION) fail(OWH_INCOMPATIBLE);
        const auto name = L"ow_info_" + std::to_wstring(channel);
        // Open only; a reader must never create or retain stale provider storage.
        std::vector<std::byte> copy;
        {
            Handle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()));
            if (!mapping.get()) {
                const DWORD error = GetLastError();
                fail(error == ERROR_FILE_NOT_FOUND ? OWH_NOT_FOUND : OWH_SYSTEM_ERROR, error);
            }
            Handle mutex(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L"mtx").c_str()));
            if (!mutex.get()) {
                const DWORD error = GetLastError();
                fail(error == ERROR_FILE_NOT_FOUND ? OWH_NOT_FOUND : OWH_SYSTEM_ERROR, error);
            }
            View view;
            view.address = MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0);
            if (!view.address) fail(OWH_SYSTEM_ERROR, GetLastError());
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(view.address, &region, sizeof(region))) fail(OWH_SYSTEM_ERROR, GetLastError());
            const size_t limit = std::min(region.RegionSize, maximum_infodb_bytes);
            copy.resize(limit);
            {
                Lock lock(mutex.get(), timeout);
                if (lock.abandoned()) fail(OWH_CORRUPT);
                std::memcpy(copy.data(), view.address, limit);
            }
        }
        auto state = std::make_unique<owh_state>();
        state->fields = decode_infodb(copy);
        *output = state.release();
        return ok;
    });
}

extern "C" OWH_CLIENT uint32_t __cdecl owh_state_count(const owh_state* state) {
    return state ? static_cast<uint32_t>(state->fields.size()) : 0;
}

extern "C" OWH_CLIENT owh_result __cdecl owh_state_at(const owh_state* state,
    uint32_t index, owh_state_field* output) {
    if (output) *output = {};
    if (!state || !output || index >= state->fields.size()) return {OWH_INVALID_ARGUMENT, 0};
    const auto& field = state->fields[index];
    *output = {field.category.c_str(), field.key.c_str(), field.bytes.data(), field.ticks,
        static_cast<uint32_t>(field.bytes.size()), field.capacity};
    return overwolf_hook::ok;
}

extern "C" OWH_CLIENT void __cdecl owh_state_close(owh_state* state) { delete state; }
