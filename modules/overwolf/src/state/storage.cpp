#include <algorithm>
#include <cstring>
#include <owh/overwolf/state_storage.hpp>
#include <owh/overwolf/provider_sources.hpp>
#include <owh/platform/handle.hpp>
#include <owh/platform/mutex.hpp>
#include <string>

namespace overwolf_hook::overwolf {
owh_result StateSnapshot::read(uint32_t source_channel, uint32_t timeout_ms,
                               StateSnapshot &output, uint32_t producer_process_id) noexcept {
    return boundary([&] {
        if (!source_channel || timeout_ms > 1000)
            fail(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
        const auto classic = L"ow_info_" + std::to_wstring(source_channel);
        auto name = classic;
        std::string value_key;
        // Retain a classic fallback. Current sources use the exact association
        // and decoding metadata held by Overwolf, including classic-named ones.
        Handle classic_mapping;
        if (!producer_process_id)
            classic_mapping.reset(OpenFileMappingW(FILE_MAP_READ, FALSE, classic.c_str()));
        {
            std::vector<ProviderSource> providers;
            const auto result = provider_sources(providers);
            if (result.status != OWH_OK && !classic_mapping)
                return result;
            if (result.status != OWH_OK) providers.clear();
            const ProviderSource *selected = nullptr;
            for (const auto &source : providers) {
                if (source.source_channel != source_channel || source.info_mapping.empty() ||
                    (producer_process_id && source.producer_process_id != producer_process_id))
                    continue;
                if (selected)
                    return make_result(OWH_INVALID_ARGUMENT, OWH_STAGE_ARGUMENT);
                selected = &source;
            }
            if (selected) {
                name = selected->info_mapping;
                value_key = selected->value_key;
                if (name != classic) classic_mapping.reset();
            } else if (producer_process_id) {
                return make_result(OWH_NOT_FOUND, OWH_STAGE_CONTROL_IO);
            }
        }
        std::vector<std::byte> copy;
        {
            Handle mapping = std::move(classic_mapping);
            if (!mapping) mapping.reset(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()));
            if (!mapping.get()) {
                const DWORD error = GetLastError();
                fail_win32(error == ERROR_FILE_NOT_FOUND ? OWH_NOT_FOUND : OWH_SYSTEM_ERROR, error,
                           OWH_STAGE_CONTROL_IO);
            }
            Handle mutex(
                OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, (name + L"mtx").c_str()));
            if (!mutex.get()) {
                const DWORD error = GetLastError();
                fail_win32(error == ERROR_FILE_NOT_FOUND ? OWH_NOT_FOUND : OWH_SYSTEM_ERROR, error,
                           OWH_STAGE_CONTROL_IO);
            }
            View view;
            view.address = MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0);
            if (!view.address)
                fail_win32(OWH_SYSTEM_ERROR, GetLastError(), OWH_STAGE_CONTROL_IO);
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(view.address, &region, sizeof(region)))
                fail_win32(OWH_SYSTEM_ERROR, GetLastError(), OWH_STAGE_CONTROL_IO);
            const size_t limit = std::min(region.RegionSize, maximum_infodb_bytes);
            copy.resize(limit);
            {
                Lock lock(mutex.get(), timeout_ms);
                if (lock.abandoned())
                    fail(OWH_CORRUPT, OWH_STAGE_CONTROL_IO);
                std::memcpy(copy.data(), view.address, limit);
            }
        }

        StateSnapshot snapshot;
        snapshot.values_ = decode_infodb(copy);
        for (auto &value : snapshot.values_)
            value.bytes = decode_provider_value(value.bytes, value_key);
        snapshot.fields_.reserve(snapshot.values_.size());
        for (const auto &value : snapshot.values_) {
            snapshot.fields_.push_back({value.category.c_str(), value.key.c_str(),
                                        value.bytes.data(), value.ticks,
                                        static_cast<uint32_t>(value.bytes.size()), value.capacity});
        }
        output = std::move(snapshot);
        return ok;
    });
}
} // namespace overwolf_hook::overwolf
